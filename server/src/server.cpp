#include "server.h"
#include "common/log.h"
#include "common/packet.h"
#include "common/socket.h"
#include "common/time.h"

#include <algorithm>
#include <cstdio>
#include <netdb.h>
#include <unistd.h>

// todo(kstasik): expose those to application
static int read_func(sequence_t id, const void *buffer, size_t nbytes) {
  return 1;
}
static void ack_func(sequence_t ack) {}

Server::Server(uint16_t &port, uint16_t num_endpoints)
    : m_endpoint_capacity(num_endpoints), m_endpoint_count(0), m_timeout(5000) {
  m_endpoints = new Endpoint[num_endpoints];
  for(auto i = 0; i < num_endpoints; ++i) {
    Endpoint &e = m_endpoints[i];
    e.m_state = Endpoint::State::Undefined;
  }
  m_socket = open_socket(&port);
  if(m_socket == -1) {
    LOG_TRANSPORT_ERR("startup error");
    return;
  }
  LOG_TRANSPORT_INF("server listening on port %d.", port);
}

Server::~Server() {
  close(m_socket);
  delete[] m_endpoints;
}

void Server::Update() {
  size_t nbytes = 1280;
  char buffer[nbytes];
  sockaddr_storage source;
  int error = 0;

  size_t received = 0;
  do {
    received = socket_receive(m_socket, buffer, nbytes, &source, &error);
    if(received == -1) {
      LOG_TRANSPORT_DBG("error %d", error);
    } else if(received > 0) {
      char host[1024];
      char service[20];
      getnameinfo((const sockaddr *)&source, source.ss_len, host, sizeof(host),
                  service, sizeof(service), 0);
      LOG_TRANSPORT_DBG("received %lubytes from %s:%s", received, host,
                        service);

      PacketHeader *header = (PacketHeader *)buffer;
      if(header->m_protocol_id != game_protocol_id) {
        LOG_TRANSPORT_WAR("discarding unknown protocol");
        continue;
      }

      uint32_t endpoint_index = FindEndpoint(source);
      if(header->m_type == PacketType::Request && endpoint_index == -1) {
        auto *packet = (ConnectionRequestPacket *)buffer;
        endpoint_index = AddEndpoint(source, packet->m_client_salt);
        if(endpoint_index == -1) {
          LOG_TRANSPORT_WAR("cannot add endpoint");
          continue;
        }
        LOG_TRANSPORT_DBG("received: PacketType::Request");
      }

      if(endpoint_index == -1) {
        LOG_TRANSPORT_DBG("unknown endpoint");
        continue;
      }

      Endpoint &e = m_endpoints[endpoint_index];
      e.m_last_recv_time = get_time_ms();

      if(header->m_type == PacketType::Establish &&
         e.m_state == Endpoint::State::Connecting) {
        auto *packet = (ConnectionEstablishPacket *)buffer;
        LOG_TRANSPORT_DBG("received: PacketType::Establish");
        if(packet->m_key == (e.m_client_salt ^ e.m_server_salt)) {
          e.m_state = Endpoint::State::Connected;
          LOG_TRANSPORT_INF("client connected");
        }
      } else if(header->m_type == PacketType::Disconnect) {
        LOG_TRANSPORT_INF("client gracefully disconnected");
        RemoveEndpoint(endpoint_index);
      } else if(header->m_type == PacketType::Payload &&
                e.m_state == Endpoint::State::Connected) {
        auto *packet = (PayloadPacket *)buffer;
        if(e.m_reliability.OnReceived(
               packet->m_sequence, packet->m_ack, packet->m_ack_bitmask,
               buffer + sizeof(PayloadPacket), nbytes - sizeof(PayloadPacket),

               read_func, ack_func)) {
          // dispatch
          LOG_TRANSPORT_DBG("received: PacketType::Payload seq %d. ack: %d",
                            packet->m_sequence, packet->m_ack);
        }
      }
    }
  } while(received > 0 && received != -1);

  for(auto i = 0; i < m_endpoint_capacity; ++i) {
    Endpoint &endpoint = m_endpoints[i];
    if(endpoint.m_state == Endpoint::State::Connecting) {
      auto *header = (ConnectionResponsePacket *)buffer;
      header->m_protocol_id = game_protocol_id;
      header->m_type = PacketType::Response;
      header->m_client_salt = endpoint.m_client_salt;
      header->m_server_salt = endpoint.m_server_salt;
      size_t sent =
          socket_send(m_socket, &endpoint.m_address, buffer, nbytes, &error);
      if(sent == -1) {
        LOG_TRANSPORT_ERR("send error %d", error);
      }
      LOG_TRANSPORT_DBG("sent: PacketType::Response");
    } else if(endpoint.m_state == Endpoint::State::Connected) {
      auto *header = (PayloadPacket *)buffer;
      header->m_protocol_id = game_protocol_id;
      header->m_type = PacketType::Payload;
      header->m_sequence = endpoint.m_reliability.GenerateNewSequenceId(
          &header->m_ack, &header->m_ack_bitmask);
      // todo(kstasik): add data
      size_t sent =
          socket_send(m_socket, &endpoint.m_address, buffer, nbytes, &error);
      if(sent == -1) {
        LOG_TRANSPORT_ERR("send error %d", error);
      }
      endpoint.m_last_send_time = get_time_ms();
      LOG_TRANSPORT_DBG("sent: PacketType::Payload");
    }
  }

  // timeout
  for(auto i = 0; i < m_endpoint_capacity; ++i) {
    Endpoint &endpoint = m_endpoints[i];
    if(endpoint.m_state != Endpoint::State::Undefined) {
      uint32_t time = get_time_ms() - endpoint.m_last_recv_time;

      if(time > m_timeout) {
        LOG_TRANSPORT_INF("client timed out");
        RemoveEndpoint(i);
      }
    }
  }
}

uint32_t Server::FindEndpoint(const sockaddr_storage &address) {
  for(auto i = 0; i < m_endpoint_capacity; ++i) {
    if(sockaddr_compare(&m_endpoints[i].m_address, &address) == 0) {
      return i;
    }
  }
  return -1;
}

uint32_t Server::AddEndpoint(const sockaddr_storage &address,
                             uint16_t client_salt) {
  if(m_endpoint_count == m_endpoint_capacity) {
    return -1;
  }
  for(auto i = 0; i < m_endpoint_capacity; ++i) {
    Endpoint &e = m_endpoints[i];
    if(e.m_state == Endpoint::State::Undefined) {
      e.m_address = address;
      e.m_state = Endpoint::State::Connecting;
      e.m_client_salt = client_salt;
      e.m_server_salt = m_socket; // todo(kstasik): generate it better
      e.m_last_recv_time = get_time_ms();
      ++m_endpoint_count;
      return i;
    }
  }
  return -1;
}
void Server::RemoveEndpoint(uint32_t index) {
  Endpoint &e = m_endpoints[index];
  e = {};
  --m_endpoint_count;
}