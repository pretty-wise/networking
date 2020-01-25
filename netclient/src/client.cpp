#include "client.h"
#include "netcommon/log.h"
#include "netcommon/packet.h"
#include <cstdio>

// todo(kstasik): expose those to application
static int read_func(sequence_t id, const void *buffer, size_t nbytes) {
  return 1;
}
static void ack_func(sequence_t ack) {}
static void nack_func(sequence_t ack) {}

Client::Client(const char *server_address, uint16_t server_port,
               uint32_t timeout, state_callback_t state_cb, void *user_data)
    : m_state(State::Disconnected), m_last_recv_time(0), m_last_send_time(0),
      m_timeout(timeout), m_socket(0), m_state_cb(state_cb),
      m_user_data(user_data) {

  uint16_t src_port = 0;
  m_socket = open_socket(&src_port);
  if(-1 == m_socket) {
    return;
  }
  LOG_TRANSPORT_INF("started at port %d", src_port);
  Connect(server_address, server_port);
}

Client::~Client() { close_socket(m_socket); }

void Client::Update() {
  if(m_state == State::Disconnected)
    return;

  size_t nbytes = 1280;
  char buffer[nbytes];
  int error = 0;

  if(m_state == State::Discovering) {
    auto *header = (ConnectionRequestPacket *)buffer;
    header->m_protocol_id = game_protocol_id;
    header->m_type = PacketType::Request;
    header->m_client_salt = m_client_salt;
    LOG_TRANSPORT_DBG("sent: PacketType::Request");
  } else if(m_state == State::Connecting) {
    auto *header = (ConnectionEstablishPacket *)buffer;
    header->m_protocol_id = game_protocol_id;
    header->m_type = PacketType::Establish;
    header->m_key = m_client_salt ^ m_server_salt;
    LOG_TRANSPORT_DBG("sent: PacketType::Establish");
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
  LOG_TRANSPORT_DBG("sent %lubytes from %lubytes. error: %d", sent, nbytes,
                    error);
  if(sent == -1) {
    LOG_TRANSPORT_ERR("socket send error: %d", error);
  } else {
    m_last_send_time = get_time_ms();
  }

  sockaddr_storage source;
  size_t received = 0;
  do {
    received =
        socket_receive(m_socket, (void *)buffer, nbytes, &source, &error);
    if(received == -1) {
      LOG_TRANSPORT_ERR("error %d", error);
    } else if(received > 0) {
      PacketHeader *header = (PacketHeader *)buffer;
      if(header->m_protocol_id != game_protocol_id) {
        LOG_TRANSPORT_WAR("discarding unknown protocol");
        continue;
      }

      m_last_recv_time = get_time_ms();

      if(header->m_type == PacketType::Response &&
         m_state == State::Discovering) {
        auto *packet = (ConnectionResponsePacket *)buffer;
        if(packet->m_client_salt != m_client_salt) {
          LOG_TRANSPORT_WAR("salt mismatch. sent %u, received %u",
                            m_client_salt, packet->m_client_salt);
        } else {
          m_server_salt = packet->m_server_salt;
          LOG_TRANSPORT_DBG("received: PacketType::Response");
          SetState(State::Connecting);
        }
      } else if(header->m_type == PacketType::Payload &&
                (m_state == State::Connecting || m_state == State::Connected)) {
        auto *packet = (PayloadPacket *)buffer;
        if(m_state == State::Connecting) {
          SetState(State::Connected);
          LOG_TRANSPORT_INF("connected");
        }
        if(m_reliability.OnReceived(
               packet->m_sequence, packet->m_ack, packet->m_ack_bitmask,
               buffer + sizeof(PayloadPacket), nbytes - sizeof(PayloadPacket),
               read_func, ack_func, nack_func)) {
          // dispatch
          LOG_TRANSPORT_DBG("received: PacketType::Payload seq %d. ack: %d",
                            packet->m_sequence, packet->m_ack);
        } else {
          LOG_TRANSPORT_DBG("received: PacketType::Payload. read error");
        }
      }
    }
  } while(received > 0 && received != -1);

  // timeout
  uint32_t time_since_last_msg = get_time_ms() - m_last_recv_time;
  if(m_state != State::Disconnected && time_since_last_msg > m_timeout) {
    LOG_TRANSPORT_INF("connection timed out");
    close_socket(m_socket);
    m_socket = 0;
    SetState(State::Disconnected);
    m_reliability.Reset();
  }
}

bool Client::Connect(const char *server_address, uint16_t server_port) {
  if(-1 == create_udp_addr(server_address, server_port, &m_server)) {
    return false;
  }

  SetState(State::Discovering);
  m_client_salt = (int16_t)get_time_ms();

  // reset values to prevent instant timeout
  m_last_recv_time = get_time_ms();
  m_last_send_time = m_last_recv_time;
  return true;
}

void Client::Disconnect() {
  if(m_state == State::Disconnected)
    return;

  size_t nbytes = 1280;
  char buffer[nbytes];
  int error = 0;

  SetState(State::Disconnected);
  m_reliability.Reset();

  auto *header = (ConnectionDisconnectPacket *)buffer;
  header->m_protocol_id = game_protocol_id;
  header->m_type = PacketType::Disconnect;
  header->m_key = m_client_salt ^ m_server_salt;
  LOG_TRANSPORT_DBG("sent: PacketType::Disconnect");

  // best effort disconnection
  int num_disconnect_packets = 10;
  for(int i = 0; i < num_disconnect_packets; ++i) {
    size_t sent =
        socket_send(m_socket, &m_server, (const void *)buffer, nbytes, &error);
    LOG_TRANSPORT_DBG("sent %lubytes from %lubytes. error: %d", sent, nbytes,
                      error);
    if(sent == -1) {
      LOG_TRANSPORT_ERR("socket send error: %d", error);
    } else {
      m_last_send_time = get_time_ms();
    }
  }
}

bool Client::IsConnected() const { return m_state == State::Connected; }

bool Client::IsDisconnected() const { return m_state == State::Disconnected; }

bool Client::GetTransportInfo(nc_transport_info &info) {
  if(!IsConnected()) {
    return false;
  }
  info.last_received = m_reliability.GetLastRecvId();
  info.last_sent = m_reliability.GetLastSendId();
  info.last_acked = m_reliability.GetLastAckedId();
  info.last_acked_bitmask = m_reliability.GetLastAckedIdBitmask();
  return true;
}

void Client::SetState(State s) {
  if(m_state_cb)
    m_state_cb((int)s, m_user_data);
  m_state = s;
}