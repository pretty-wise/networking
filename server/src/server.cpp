#include "server.h"
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
  m_endpoints = new ServerEndpoint[num_endpoints];
  for(auto i = 0; i < num_endpoints; ++i) {
    ServerEndpoint &e = m_endpoints[i];
    e.m_state = ServerEndpoint::State::Undefined;
  }
  m_socket = open_socket(&port);
  if(m_socket == -1) {
    printf("startup error\n");
    return;
  }
  printf("server listening on port %d.\n", port);
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
      printf("error %d\n", error);
    } else if(received > 0) {
      char host[1024];
      char service[20];
      getnameinfo((const sockaddr *)&source, source.ss_len, host, sizeof(host),
                  service, sizeof(service), 0);
      printf("received %lubytes from %s:%s\n", received, host, service);

      PacketHeader *header = (PacketHeader *)buffer;
      if(header->m_protocol_id != game_protocol_id) {
        printf("discarding unknown protocol\n");
        continue;
      }

      uint32_t endpoint_index = FindEndpoint(source);
      if(header->m_type == PacketType::Request && endpoint_index == -1) {
        auto *packet = (ConnectionRequestPacket *)buffer;
        endpoint_index = AddEndpoint(source, packet->m_client_salt);
        if(endpoint_index == -1) {
          printf("cannot add endpoint\n");
          continue;
        }
        printf("received: PacketType::Request\n");
      }

      if(endpoint_index == -1) {
        printf("unknown endpoint\n");
        continue;
      }

      ServerEndpoint &e = m_endpoints[endpoint_index];
      e.m_last_recv_time = get_time_ms();

      if(header->m_type == PacketType::Establish &&
         e.m_state == ServerEndpoint::State::Connecting) {
        auto *packet = (ConnectionEstablishPacket *)buffer;
        printf("received: PacketType::Establish\n");
        if(packet->m_key == (e.m_client_salt ^ e.m_server_salt)) {
          e.m_state = ServerEndpoint::State::Connected;
          printf("client connected\n");
        }
      } else if(header->m_type == PacketType::Payload &&
                e.m_state == ServerEndpoint::State::Connected) {
        auto *packet = (PayloadPacket *)buffer;
        if(e.m_reliability.OnReceived(
               packet->m_sequence, packet->m_ack, packet->m_ack_bitmask,
               buffer + sizeof(PayloadPacket), nbytes - sizeof(PayloadPacket),

               read_func, ack_func)) {
          // dispatch
          printf("received: PacketType::Payload seq %d. ack: %d\n",
                 packet->m_sequence, packet->m_ack);
        }
      }
    }
  } while(received > 0 && received != -1);

  for(auto i = 0; i < m_endpoint_capacity; ++i) {
    ServerEndpoint &endpoint = m_endpoints[i];
    if(endpoint.m_state == ServerEndpoint::State::Connecting) {
      auto *header = (ConnectionResponsePacket *)buffer;
      header->m_protocol_id = game_protocol_id;
      header->m_type = PacketType::Response;
      header->m_client_salt = endpoint.m_client_salt;
      header->m_server_salt = endpoint.m_server_salt;
      size_t sent =
          socket_send(m_socket, &endpoint.m_address, buffer, nbytes, &error);
      if(sent == -1) {
        printf("send error %d\n", error);
      }
      printf("sent: PacketType::Response\n");
    } else if(endpoint.m_state == ServerEndpoint::State::Connected) {
      auto *header = (PayloadPacket *)buffer;
      header->m_protocol_id = game_protocol_id;
      header->m_type = PacketType::Payload;
      header->m_sequence = endpoint.m_reliability.GenerateNewSequenceId(
          &header->m_ack, &header->m_ack_bitmask);
      // todo(kstasik): add data
      size_t sent =
          socket_send(m_socket, &endpoint.m_address, buffer, nbytes, &error);
      if(sent == -1) {
        printf("send error %d\n", error);
      }
      endpoint.m_last_send_time = get_time_ms();
      printf("sent: PacketType::Payload\n");
    }
  }

  // timeout
  for(auto i = 0; i < m_endpoint_capacity; ++i) {
    ServerEndpoint &endpoint = m_endpoints[i];
    if(endpoint.m_state != ServerEndpoint::State::Undefined) {
      uint32_t time = get_time_ms() - endpoint.m_last_recv_time;

      if(time > m_timeout) {
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
    ServerEndpoint &e = m_endpoints[i];
    if(e.m_state == ServerEndpoint::State::Undefined) {
      e.m_address = address;
      e.m_state = ServerEndpoint::State::Connecting;
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
  printf("removing endpoint %d\n", index);
  ServerEndpoint &e = m_endpoints[index];
  e = {};
  --m_endpoint_count;
}