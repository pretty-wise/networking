#include "client.h"
#include "common/packet.h"
#include <cstdio>

// todo(kstasik): expose those to application
static int read_func(sequence_t id, const void *buffer, size_t nbytes) {
  return 1;
}
static void ack_func(sequence_t ack) {}

Client::Client(const char *server_address, uint16_t server_port)
    : m_state(State::Disconnected) {
  if(-1 == create_udp_addr(server_address, server_port, &m_server)) {
    return;
  }

  uint16_t src_port = 0;
  m_socket = open_socket(&src_port);
  if(-1 == m_socket) {
    return;
  }

  printf("src: %d, dst: %d\n", src_port, server_port);
  m_state = State::Discovering;
  m_client_salt = 1234; // todo(kstasik): better salt
}

void Client::Update() {
  size_t nbytes = 1280;
  char buffer[nbytes];
  int error = 0;

  if(m_state == State::Discovering) {
    auto *header = (ConnectionRequestPacket *)buffer;
    header->m_protocol_id = game_protocol_id;
    header->m_type = PacketType::Request;
    header->m_client_salt = m_client_salt;
    printf("sent: PacketType::Request\n");
  } else if(m_state == State::Connecting) {
    auto *header = (ConnectionEstablishPacket *)buffer;
    header->m_protocol_id = game_protocol_id;
    header->m_type = PacketType::Establish;
    header->m_key = m_client_salt ^ m_server_salt;
    printf("sent: PacketType::Establish\n");
  } else if(m_state == State::Connected) {
    auto *header = (PayloadPacket *)buffer;
    header->m_protocol_id = game_protocol_id;
    header->m_type = PacketType::Payload;
    header->m_sequence = m_reliability.GenerateNewSequenceId(
        &header->m_ack, &header->m_ack_bitmask);
    // todo(kstasik): add payload
  }

  size_t sent =
      socket_send(m_socket, &m_server, (const void *)buffer, nbytes, &error);
  printf("sent %lubytes from %lubytes. error: %d\n", sent, nbytes, error);
  if(sent == -1) {
    printf("socket send error: %d\n", error);
  }
  sockaddr_storage source;
  size_t received = 0;
  do {
    received =
        socket_receive(m_socket, (void *)buffer, nbytes, &source, &error);
    if(received == -1) {
      printf("error %d\n", error);
    } else if(received > 0) {
      PacketHeader *header = (PacketHeader *)buffer;
      if(header->m_protocol_id != game_protocol_id) {
        printf("discarding unknown protocol\n");
        continue;
      }
      if(header->m_type == PacketType::Response &&
         m_state == State::Discovering) {
        auto *packet = (ConnectionResponsePacket *)buffer;
        if(packet->m_client_salt != m_client_salt) {
          printf("salt mismatch. sent %u, received %u\n", m_client_salt,
                 packet->m_client_salt);
        } else {
          m_server_salt = packet->m_server_salt;
          printf("received: PacketType::Response\n");
          m_state = State::Connecting;
        }
      } else if(header->m_type == PacketType::Payload &&
                (m_state == State::Connecting || m_state == State::Connected)) {
        auto *packet = (PayloadPacket *)buffer;
        m_state = State::Connected;
        if(m_reliability.OnReceived(
               packet->m_sequence, packet->m_ack, packet->m_ack_bitmask,
               buffer + sizeof(PayloadPacket), nbytes - sizeof(PayloadPacket),
               read_func, ack_func)) {
          // dispatch
          printf("received: PacketType::Payload seq %d. ack: %d\n",
                 packet->m_sequence, packet->m_ack);
        } else {
          printf("received: PacketType::Payload. read error\n");
        }
      }
    }
  } while(received > 0 && received != -1);
}

void Client::Connect() {}
void Client::Disconnect() {}
bool Client::IsConnected() const { return m_state == State::Connected; }