#include "netserver/netserver.h"
#include "netcommon/log.h"
#include "netcommon/netsimulator.h"
#include "netcommon/protocol.h"
#include "netcommon/reliability.h"
#include "netcommon/socket.h"
#include "utils/time.h"

#include <algorithm>
#include <cstdio>
#include <netdb.h>
#include <unistd.h>

// todo(kstasik): expose those to application
static int read_func(sequence_t id, const void *buffer, uint32_t nbytes) {
  return 0;
}
static void ack_func(sequence_t ack, void *) {}

struct ns_endpoint {
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
  ns_endpoint *m_endpoints;
  netsimulator *m_simulator;

  sockaddr_storage m_local;
  int m_socket;

  void (*m_state_cb)(uint32_t state, ns_endpoint *e, void *user_data);
  void (*m_ack_cb)(uint16_t id, ns_endpoint *e, void *user_data);
  int (*m_send_cb)(uint16_t id, void *buffer, uint32_t nbytes, ns_endpoint *dst,
                   void *user_data);
  int (*m_recv_cb)(uint16_t id, const void *buffer, uint32_t nbytes,
                   ns_endpoint *src, void *user_data);
  void *m_user_data;
};

static uint32_t AddEndpoint(ns_server *context, const sockaddr_storage &address,
                            uint16_t client_salt) {
  if(context->m_endpoint_count == context->m_endpoint_capacity) {
    return -1;
  }
  for(auto i = 0; i < context->m_endpoint_capacity; ++i) {
    ns_endpoint &e = context->m_endpoints[i];
    if(e.m_state == ns_endpoint::State::Undefined) {
      e.m_address = address;
      e.m_state = ns_endpoint::State::Connecting;
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
  ns_endpoint &e = context->m_endpoints[index];

  bool notifyRemoval = e.m_state == ns_endpoint::State::Connected;

  e.m_address = {};
  e.m_state = ns_endpoint::State::Undefined;
  e.m_client_salt = 0;
  e.m_server_salt = 0;
  e.m_last_recv_time = 0;
  e.m_last_send_time = 0;
  e.m_reliability.Reset();

  --context->m_endpoint_count;

  if(notifyRemoval) {
    context->m_state_cb(NETSERVER_STATE_ENDPOINT_DISCONNECTED, &e,
                        context->m_user_data);
  }
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

struct ns_server *netserver_create(ns_config *config) {
  if(!config)
    return nullptr;

  int socket = open_socket(&config->port);
  if(socket == -1) {
    LOG_TRANSPORT_ERR("startup error");
    return nullptr;
  }

  LOG_TRANSPORT_INF("server listening on port %d.", config->port);

  ns_server *server = new ns_server;
  server->m_endpoint_capacity = config->num_endpoints;
  server->m_endpoint_count = 0;
  server->m_timeout = 5000;
  server->m_simulator = config->simulator;
  server->m_socket = socket;
  server->m_ack_cb = config->ack_callback;
  server->m_send_cb = config->send_callback;
  server->m_recv_cb = config->recv_callback;
  server->m_state_cb = config->state_callback;
  server->m_user_data = config->user_data;

  server->m_endpoints = new ns_endpoint[server->m_endpoint_capacity];
  for(auto i = 0; i < server->m_endpoint_capacity; ++i) {
    ns_endpoint &e = server->m_endpoints[i];
    e.m_state = ns_endpoint::State::Undefined;
  }

  create_udp_addr("127.0.0.1", config->port, &server->m_local);

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

  ns_endpoint &e = context->m_endpoints[endpoint_index];

  if(header->m_type == PacketType::Establish &&
     e.m_state == ns_endpoint::State::Connecting) {
    auto *packet = (ConnectionEstablishPacket *)buffer;
    LOG_TRANSPORT_DBG("received: PacketType::Establish");
    if(packet->m_key == (e.m_client_salt ^ e.m_server_salt)) {
      e.m_state = ns_endpoint::State::Connected;
      e.m_last_recv_time = get_time_ms();
      LOG_TRANSPORT_INF("client connected");

      context->m_state_cb(NETSERVER_STATE_ENDPOINT_CONNECTED, &e,
                          context->m_user_data);
    }
  } else if(header->m_type == PacketType::Disconnect) {
    LOG_TRANSPORT_INF("client gracefully disconnected");
    RemoveEndpoint(context, endpoint_index);
  } else if(header->m_type == PacketType::Payload &&
            e.m_state == ns_endpoint::State::Connected) {
    auto *packet = (PayloadPacket *)buffer;
    if(e.m_reliability.IsStale(packet->m_sequence)) {
      LOG_TRANSPORT_WAR("received stale packet %d", packet->m_sequence);
    } else {
      int result = read_func(packet->m_sequence, buffer + sizeof(PayloadPacket),
                             nbytes - sizeof(PayloadPacket));
      if(result != 0) {
        LOG_TRANSPORT_WAR("failed to read: %d", packet->m_sequence);
      } else {
        sequence_bitmask_t acks = e.m_reliability.Ack(
            packet->m_sequence, packet->m_ack, packet->m_ack_bitmask);

        for(int i = 0; i < 32; ++i) {
          if((acks & (1 << i)) != 0) {
            context->m_ack_cb(packet->m_ack + i, &e, context->m_user_data);
          }
        }

        context->m_recv_cb(packet->m_sequence, buffer + sizeof(PayloadPacket),
                           nbytes - sizeof(PayloadPacket), &e,
                           context->m_user_data);

        e.m_last_recv_time = get_time_ms();

        LOG_TRANSPORT_DBG("received: PacketType::Payload seq %d. ack: %d",
                          packet->m_sequence, packet->m_ack);
      }
    }
  } else if(header->m_type == PacketType::TimeSync) {
    auto *packet = (TimeSync *)buffer;

    const uint32_t outnbytes = 1280;
    uint8_t outBuffer[outnbytes];

    auto *output = (TimeSync *)outBuffer;
    output->m_protocol_id = game_protocol_id;
    output->m_type = PacketType::TimeSync;
    output->m_id = packet->m_id;
    output->m_request_send_time = packet->m_request_send_time;
    output->m_request_recv_time = get_time_us();
    output->m_response_send_time = get_time_us();
    output->m_response_recv_time = 0;
    netserver_send(context, e.m_address, outBuffer, sizeof(TimeSync));
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
    ns_endpoint &endpoint = context->m_endpoints[i];
    if(endpoint.m_state == ns_endpoint::State::Connecting) {
      auto *header = (ConnectionResponsePacket *)buffer;
      header->m_protocol_id = game_protocol_id;
      header->m_type = PacketType::Response;
      header->m_client_salt = endpoint.m_client_salt;
      header->m_server_salt = endpoint.m_server_salt;
      if(netserver_send(context, endpoint.m_address, buffer, nbytes)) {
        LOG_TRANSPORT_DBG("sent: PacketType::Response");
      }
    } else if(endpoint.m_state == ns_endpoint::State::Connected) {
      auto *header = (PayloadPacket *)buffer;
      header->m_protocol_id = game_protocol_id;
      header->m_type = PacketType::Payload;
      header->m_sequence = endpoint.m_reliability.GenerateNewSequenceId(
          &header->m_ack, &header->m_ack_bitmask);

      nbytes = context->m_send_cb(
          header->m_sequence, buffer + sizeof(PayloadPacket),
          nbytes - sizeof(PayloadPacket), &endpoint, context->m_user_data);

      if(netserver_send(context, endpoint.m_address, buffer,
                        nbytes + sizeof(PayloadPacket))) {
        LOG_TRANSPORT_DBG("sent: PacketType::Payload");
      }
      endpoint.m_last_send_time = get_time_ms();
    }
  }

  // timeout
  for(auto i = 0; i < context->m_endpoint_capacity; ++i) {
    ns_endpoint &endpoint = context->m_endpoints[i];
    if(endpoint.m_state != ns_endpoint::State::Undefined) {
      uint32_t time = get_time_ms() - endpoint.m_last_recv_time;

      if(time > context->m_timeout) {
        LOG_TRANSPORT_INF("client timed out");
        RemoveEndpoint(context, i);
      }
    }
  }
}

int netserver_rtt_info(ns_server *context, ns_endpoint **endpoints,
                       uint32_t *averaged_rtt, uint32_t *frame_rtt,
                       uint32_t *endpoint_count) {
  if(!context || !endpoints || !endpoint_count)
    return -1;

  if(*endpoint_count < context->m_endpoint_count)
    return -2;

  uint32_t index = 0;
  for(uint32_t eidx = 0; eidx < context->m_endpoint_count; ++eidx) {
    if(context->m_endpoints[eidx].m_state == ns_endpoint::State::Connected) {
      endpoints[index] = &context->m_endpoints[eidx];
      averaged_rtt[index] =
          context->m_endpoints[eidx].m_reliability.m_smoothed_rtt;
      frame_rtt[index] = context->m_endpoints[eidx].m_reliability.m_last_rtt;
      ++index;
    }
  }

  *endpoint_count = context->m_endpoint_count;

  return 0;
}

int netserver_transport_info(ns_server *context, ns_endpoint *endpoint,
                             ns_transport_info *info) {
  if(!context || !endpoint || !info)
    return -1;

  auto index = 0;
  do {
    if(&context->m_endpoints[index] == endpoint) {
      if(endpoint->m_state != ns_endpoint::State::Connected)
        return -2;
      else
        break;
    }
  } while(++index < context->m_endpoint_count);

  if(index == context->m_endpoint_count)
    return -3;

  info->endpoint = endpoint;

  info->last_received = endpoint->m_reliability.GetLastRecvId();
  info->last_sent = endpoint->m_reliability.GetLastSendId();
  info->last_acked = endpoint->m_reliability.GetLastAckedId();
  info->last_acked_bitmask = endpoint->m_reliability.GetLastAckedIdBitmask();

  info->last_rtt = endpoint->m_reliability.m_last_rtt;
  info->smoothed_rtt = endpoint->m_reliability.m_smoothed_rtt;

  info->rtt_log = endpoint->m_reliability.m_rtt_log.Begin();
  info->smoothed_rtt_log = endpoint->m_reliability.m_smoothed_rtt_log.Begin();
  info->rtt_log_size = endpoint->m_reliability.m_rtt_log.Size();

  return 0;
}