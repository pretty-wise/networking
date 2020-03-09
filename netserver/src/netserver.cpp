#include "netserver/netserver.h"
#include "netcommon/log.h"
#include "netcommon/netsimulator.h"
#include "netcommon/packet.h"
#include "netcommon/reliability.h"
#include "netcommon/socket.h"
#include "netcommon/time.h"

#include <algorithm>
#include <cstdio>
#include <netdb.h>
#include <unistd.h>

// todo(kstasik): expose those to application
static int read_func(sequence_t id, const void *buffer, uint32_t nbytes) {
  return 0;
}
static void ack_func(sequence_t ack, void *) {}

struct Endpoint {
  sockaddr_storage m_address;
  enum class State {
    Undefined,
    Connecting,
    Connected
  } m_state = State::Undefined;
  uint16_t m_client_salt;
  uint16_t m_server_salt;
  uint32_t m_last_recv_time;
  uint32_t m_last_send_time;
  Reliability m_reliability;
};

struct ns_server {
  uint32_t m_timeout; // todo(kstasik): cofigure this
  uint16_t m_endpoint_capacity;
  uint16_t m_endpoint_count;
  Endpoint *m_endpoints;
  netsimulator *m_simulator;

  sockaddr_storage m_local;
  int m_socket;
};

static uint32_t AddEndpoint(ns_server *context, const sockaddr_storage &address,
                            uint16_t client_salt) {
  if(context->m_endpoint_count == context->m_endpoint_capacity) {
    return -1;
  }
  for(auto i = 0; i < context->m_endpoint_capacity; ++i) {
    Endpoint &e = context->m_endpoints[i];
    if(e.m_state == Endpoint::State::Undefined) {
      e.m_address = address;
      e.m_state = Endpoint::State::Connecting;
      e.m_client_salt = client_salt;
      e.m_last_recv_time = get_time_ms();
      e.m_server_salt = e.m_last_recv_time; // todo(kstasik): generate it better
      ++context->m_endpoint_count;
      return i;
    }
  }
  return -1;
}

static void RemoveEndpoint(ns_server *context, uint32_t index) {
  Endpoint &e = context->m_endpoints[index];

  e.m_address = {};
  e.m_state = Endpoint::State::Undefined;
  e.m_client_salt = 0;
  e.m_server_salt = 0;
  e.m_last_recv_time = 0;
  e.m_last_send_time = 0;
  e.m_reliability.Reset();

  --context->m_endpoint_count;
}

static uint32_t FindEndpoint(ns_server *context,
                             const sockaddr_storage &address) {
  for(auto i = 0; i < context->m_endpoint_capacity; ++i) {
    if(sockaddr_compare(&context->m_endpoints[i].m_address, &address) == 0) {
      return i;
    }
  }
  return -1;
}

struct ns_server *netserver_create(uint16_t *port, uint16_t num_endpoints,
                                   struct netsimulator *simulator) {
  int socket = open_socket(port);
  if(socket == -1) {
    LOG_TRANSPORT_ERR("startup error");
    return nullptr;
  }

  LOG_TRANSPORT_INF("server listening on port %d.", *port);

  ns_server *server = new ns_server;
  server->m_endpoint_capacity = num_endpoints;
  server->m_endpoint_count = 0;
  server->m_timeout = 5000;
  server->m_simulator = simulator;
  server->m_socket = socket;

  server->m_endpoints = new Endpoint[num_endpoints];
  for(auto i = 0; i < num_endpoints; ++i) {
    Endpoint &e = server->m_endpoints[i];
    e.m_state = Endpoint::State::Undefined;
  }

  create_udp_addr("127.0.0.1", *port, &server->m_local);

  Reliability::Test();
  return server;
}
void netserver_destroy(struct ns_server *context) {
  close(context->m_socket);
  delete[] context->m_endpoints;
  delete context;
}

static bool netserver_send(struct ns_server *context,
                           const sockaddr_storage &dst, uint8_t *buffer,
                           uint32_t nbytes) {
  if(context->m_simulator) {
    netsimulator_send(context->m_simulator, buffer, nbytes, context->m_local,
                      dst);
    return true;
  } else {
    int error;
    size_t sent = socket_send(context->m_socket, &dst, buffer, nbytes, &error);
    if(sent == -1) {
      LOG_TRANSPORT_ERR("send error %d", error);
    }
    return sent == nbytes;
  }
}

static void netserver_recv(struct ns_server *context, uint8_t *buffer,
                           uint32_t nbytes, const sockaddr_storage &src) {

  PacketHeader *header = (PacketHeader *)buffer;
  if(header->m_protocol_id != game_protocol_id) {
    LOG_TRANSPORT_WAR("discarding unknown protocol");
    return;
  }

  uint32_t endpoint_index = FindEndpoint(context, src);
  if(header->m_type == PacketType::Request && endpoint_index == -1) {
    auto *packet = (ConnectionRequestPacket *)buffer;
    endpoint_index = AddEndpoint(context, src, packet->m_client_salt);
    if(endpoint_index == -1) {
      LOG_TRANSPORT_WAR("cannot add endpoint");
      return;
    }
    LOG_TRANSPORT_DBG("received: PacketType::Request");
  }

  if(endpoint_index == -1) {
    LOG_TRANSPORT_DBG("unknown endpoint");
    return;
  }

  Endpoint &e = context->m_endpoints[endpoint_index];

  if(header->m_type == PacketType::Establish &&
     e.m_state == Endpoint::State::Connecting) {
    auto *packet = (ConnectionEstablishPacket *)buffer;
    LOG_TRANSPORT_DBG("received: PacketType::Establish");
    if(packet->m_key == (e.m_client_salt ^ e.m_server_salt)) {
      e.m_state = Endpoint::State::Connected;
      e.m_last_recv_time = get_time_ms();
      LOG_TRANSPORT_INF("client connected");
    }
  } else if(header->m_type == PacketType::Disconnect) {
    LOG_TRANSPORT_INF("client gracefully disconnected");
    RemoveEndpoint(context, endpoint_index);
  } else if(header->m_type == PacketType::Payload &&
            e.m_state == Endpoint::State::Connected) {
    auto *packet = (PayloadPacket *)buffer;
    if(e.m_reliability.IsStale(packet->m_sequence)) {
      LOG_TRANSPORT_WAR("received stale packet %d", packet->m_sequence);
    } else {
      int result = read_func(packet->m_sequence, buffer + sizeof(PayloadPacket),
                             nbytes - sizeof(PayloadPacket));
      if(result != 0) {
        LOG_TRANSPORT_WAR("failed to read: %d", packet->m_sequence);
      } else {
        e.m_reliability.Ack(packet->m_sequence, packet->m_ack,
                            packet->m_ack_bitmask, ack_func, nullptr);

        e.m_last_recv_time = get_time_ms();

        LOG_TRANSPORT_DBG("received: PacketType::Payload seq %d. ack: %d",
                          packet->m_sequence, packet->m_ack);
      }
    }
  }
}

void netserver_update(struct ns_server *context) {

  if(context->m_simulator) {
    const uint32_t max_recv = 8;
    uint8_t *buffer[max_recv];
    uint32_t nbytes[max_recv];
    sockaddr_storage src[max_recv];

    uint32_t num_received = netsimulator_recv(
        context->m_simulator, context->m_local, buffer, nbytes, src, max_recv);

    for(uint32_t i = 0; i < num_received; ++i) {
      netserver_recv(context, buffer[i], nbytes[i], src[i]);
      free(buffer[i]);
    }
  } else {
    size_t nbytes = 1280;
    uint8_t buffer[nbytes];
    sockaddr_storage source;
    int error = 0;

    size_t received = 0;
    do {
      received =
          socket_receive(context->m_socket, buffer, nbytes, &source, &error);
      if(received == -1) {
        LOG_TRANSPORT_DBG("error %d", error);
      } else if(received > 0) {
        char host[1024];
        char service[20];
        getnameinfo((const sockaddr *)&source, source.ss_len, host,
                    sizeof(host), service, sizeof(service), 0);
        LOG_TRANSPORT_DBG("received %lubytes from %s:%s", received, host,
                          service);

        netserver_recv(context, buffer, nbytes, source);
      }
    } while(received > 0 && received != -1);
  }

  size_t nbytes = 1280;
  uint8_t buffer[nbytes];

  // outgoing
  for(auto i = 0; i < context->m_endpoint_capacity; ++i) {
    Endpoint &endpoint = context->m_endpoints[i];
    if(endpoint.m_state == Endpoint::State::Connecting) {
      auto *header = (ConnectionResponsePacket *)buffer;
      header->m_protocol_id = game_protocol_id;
      header->m_type = PacketType::Response;
      header->m_client_salt = endpoint.m_client_salt;
      header->m_server_salt = endpoint.m_server_salt;
      if(netserver_send(context, endpoint.m_address, buffer, nbytes)) {
        LOG_TRANSPORT_DBG("sent: PacketType::Response");
      }
    } else if(endpoint.m_state == Endpoint::State::Connected) {
      auto *header = (PayloadPacket *)buffer;
      header->m_protocol_id = game_protocol_id;
      header->m_type = PacketType::Payload;
      header->m_sequence = endpoint.m_reliability.GenerateNewSequenceId(
          &header->m_ack, &header->m_ack_bitmask);
      // todo(kstasik): add data
      if(netserver_send(context, endpoint.m_address, buffer, nbytes)) {
        LOG_TRANSPORT_DBG("sent: PacketType::Payload");
      }
      endpoint.m_last_send_time = get_time_ms();
    }
  }

  // timeout
  for(auto i = 0; i < context->m_endpoint_capacity; ++i) {
    Endpoint &endpoint = context->m_endpoints[i];
    if(endpoint.m_state != Endpoint::State::Undefined) {
      uint32_t time = get_time_ms() - endpoint.m_last_recv_time;

      if(time > context->m_timeout) {
        LOG_TRANSPORT_INF("client timed out");
        RemoveEndpoint(context, i);
      }
    }
  }
}