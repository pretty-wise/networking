#include "netclient/netclient.h"
#include "netcommon/log.h"
#include "netcommon/packet.h"
#include "netcommon/reliability.h"
#include "netcommon/socket.h"
#include "netcommon/time.h"

typedef void (*state_callback_t)(int32_t, void *);
typedef void (*packet_callback_t)(sequence_t, int32_t type, void *);
typedef int (*send_callback_t)(uint16_t id, void *buffer, uint32_t nbytes);
typedef int (*recv_callback_t)(uint16_t id, const void *buffer,
                               uint32_t nbytes);

struct nc_client {
  int m_socket;
  sockaddr_storage m_server;
  int m_state;
  uint16_t m_client_salt;
  uint16_t m_server_salt;

  uint32_t m_timeout;
  uint32_t m_last_recv_time;
  uint32_t m_last_send_time;
  Reliability m_reliability;

  state_callback_t m_state_cb;
  packet_callback_t m_packet_cb;
  send_callback_t m_send_cb;
  recv_callback_t m_recv_cb;
  void *m_user_data;
};

void netclient_make_default(nc_config *config) {
  config->timeout = 5000;
  config->state_callback = nullptr;
  config->user_data = nullptr;
  config->packet_callback = nullptr;
  config->send_callback = nullptr;
  config->recv_callback = nullptr;
}

nc_client *netclient_create(const nc_config *config) {
  if(!config)
    return nullptr;

  if(!config->send_callback || !config->recv_callback) {
    return nullptr;
  }

  uint16_t src_port = 0;
  int socket = open_socket(&src_port);
  if(-1 == socket) {
    return nullptr;
  }

  LOG_TRANSPORT_INF("started at port %d", src_port);

  nc_client *client = new nc_client{};
  client->m_state = NETCLIENT_STATE_DISCONNECTED;
  client->m_last_recv_time = 0;
  client->m_last_send_time = 0;
  client->m_timeout = config->timeout;
  client->m_socket = socket;
  client->m_state_cb = config->state_callback;
  client->m_packet_cb = config->packet_callback;
  client->m_send_cb = config->send_callback;
  client->m_recv_cb = config->recv_callback;
  client->m_user_data = config->user_data;

  netclient_connect(client, config->server_address, config->server_port);
  return client;
}

void netclient_destroy(nc_client *context) {
  close_socket(context->m_socket);
  delete context;
}

void netclient_update(nc_client *context) {

  if(context->m_state == NETCLIENT_STATE_DISCONNECTED)
    return;

  size_t nbytes = 1280;
  char buffer[nbytes];
  int error = 0;

  if(context->m_state == NETCLIENT_STATE_DISCOVERING) {
    auto *header = (ConnectionRequestPacket *)buffer;
    header->m_protocol_id = game_protocol_id;
    header->m_type = PacketType::Request;
    header->m_client_salt = context->m_client_salt;
    LOG_TRANSPORT_DBG("sent: PacketType::Request");
  } else if(context->m_state == NETCLIENT_STATE_CONNECTING) {
    auto *header = (ConnectionEstablishPacket *)buffer;
    header->m_protocol_id = game_protocol_id;
    header->m_type = PacketType::Establish;
    header->m_key = context->m_client_salt ^ context->m_server_salt;
    LOG_TRANSPORT_DBG("sent: PacketType::Establish");
  } else if(context->m_state == NETCLIENT_STATE_CONNECTED) {
    auto *header = (PayloadPacket *)buffer;
    header->m_protocol_id = game_protocol_id;
    header->m_type = PacketType::Payload;
    header->m_sequence = context->m_reliability.GenerateNewSequenceId(
        &header->m_ack, &header->m_ack_bitmask);
    context->m_send_cb(header->m_sequence, buffer + sizeof(PayloadPacket),
                       nbytes - sizeof(PayloadPacket));
  }

  size_t sent = socket_send(context->m_socket, &context->m_server,
                            (const void *)buffer, nbytes, &error);
  LOG_TRANSPORT_DBG("sent %lubytes from %lubytes. error: %d", sent, nbytes,
                    error);
  if(sent == -1) {
    LOG_TRANSPORT_ERR("socket send error: %d", error);
  } else {
    context->m_last_send_time = get_time_ms();
  }

  sockaddr_storage source;
  size_t received = 0;
  do {
    received = socket_receive(context->m_socket, (void *)buffer, nbytes,
                              &source, &error);
    if(received == -1) {
      LOG_TRANSPORT_ERR("error %d", error);
    } else if(received > 0) {
      PacketHeader *header = (PacketHeader *)buffer;
      if(header->m_protocol_id != game_protocol_id) {
        LOG_TRANSPORT_WAR("discarding unknown protocol");
        continue;
      }

      if(header->m_type == PacketType::Response &&
         context->m_state == NETCLIENT_STATE_DISCOVERING) {
        auto *packet = (ConnectionResponsePacket *)buffer;
        if(packet->m_client_salt != context->m_client_salt) {
          LOG_TRANSPORT_WAR("salt mismatch. sent %u, received %u",
                            context->m_client_salt, packet->m_client_salt);
        } else {
          context->m_server_salt = packet->m_server_salt;
          LOG_TRANSPORT_DBG("received: PacketType::Response");
          if(context->m_state_cb)
            context->m_state_cb(NETCLIENT_STATE_CONNECTING,
                                context->m_user_data);
          context->m_state = NETCLIENT_STATE_CONNECTING;
          context->m_last_recv_time = get_time_ms();
        }
      } else if(header->m_type == PacketType::Payload &&
                (context->m_state == NETCLIENT_STATE_CONNECTING ||
                 context->m_state == NETCLIENT_STATE_CONNECTED)) {
        auto *packet = (PayloadPacket *)buffer;
        if(context->m_state == NETCLIENT_STATE_CONNECTING) {
          if(context->m_state_cb)
            context->m_state_cb(NETCLIENT_STATE_CONNECTED,
                                context->m_user_data);
          context->m_state = NETCLIENT_STATE_CONNECTED;
          LOG_TRANSPORT_INF("connected");
        }
        if(context->m_reliability.IsStale(packet->m_sequence)) {
          LOG_TRANSPORT_WAR("stale packet %d", packet->m_sequence);
        } else {
          int result = context->m_recv_cb(packet->m_sequence,
                                          buffer + sizeof(PayloadPacket),
                                          nbytes - sizeof(PayloadPacket));
          if(result != 0) {
            LOG_TRANSPORT_WAR("received: PacketType::Payload. read error %d",
                              packet->m_sequence);
          } else {
            context->m_reliability.Ack(
                packet->m_sequence, packet->m_ack, packet->m_ack_bitmask,
                context->m_packet_cb, context->m_user_data);

            context->m_last_recv_time = get_time_ms();

            LOG_TRANSPORT_DBG("received: PacketType::Payload seq %d. ack: %d",
                              packet->m_sequence, packet->m_ack);
          }
        }
      }
    }
  } while(received > 0 && received != -1);

  // timeout
  uint32_t time_since_last_msg = get_time_ms() - context->m_last_recv_time;
  if(context->m_state != NETCLIENT_STATE_DISCONNECTED &&
     time_since_last_msg > context->m_timeout) {
    LOG_TRANSPORT_INF("connection timed out");
    if(context->m_state_cb)
      context->m_state_cb(NETCLIENT_STATE_DISCONNECTED, context->m_user_data);
    context->m_state = NETCLIENT_STATE_DISCONNECTED;
    context->m_reliability.Reset();
  }
}

int netclient_disconnect(nc_client *context) {
  if(!context)
    return -1;

  if(context->m_state == NETCLIENT_STATE_DISCONNECTED) {
    return -2;
  }

  size_t nbytes = 1280;
  char buffer[nbytes];
  int error = 0;

  if(context->m_state_cb)
    context->m_state_cb(NETCLIENT_STATE_DISCONNECTED, context->m_user_data);
  context->m_state = NETCLIENT_STATE_DISCONNECTED;

  context->m_reliability.Reset();

  auto *header = (ConnectionDisconnectPacket *)buffer;
  header->m_protocol_id = game_protocol_id;
  header->m_type = PacketType::Disconnect;
  header->m_key = context->m_client_salt ^ context->m_server_salt;
  LOG_TRANSPORT_DBG("sent: PacketType::Disconnect");

  // best effort disconnection
  int num_disconnect_packets = 10;
  for(int i = 0; i < num_disconnect_packets; ++i) {
    size_t sent = socket_send(context->m_socket, &context->m_server,
                              (const void *)buffer, nbytes, &error);
    LOG_TRANSPORT_DBG("sent %lubytes from %lubytes. error: %d", sent, nbytes,
                      error);
    if(sent == -1) {
      LOG_TRANSPORT_ERR("socket send error: %d", error);
    } else {
      context->m_last_send_time = get_time_ms();
    }
  }
  return 0;
}

int netclient_connect(nc_client *context, const char *addr, uint16_t port) {
  if(!context)
    return -1;

  if(context->m_state != NETCLIENT_STATE_DISCONNECTED)
    return -2;

  if(-1 == create_udp_addr(addr, port, &context->m_server)) {
    return -3;
  }
  if(context->m_state_cb)
    context->m_state_cb(NETCLIENT_STATE_DISCOVERING, context->m_user_data);
  context->m_state = NETCLIENT_STATE_DISCOVERING;

  context->m_client_salt = (int16_t)get_time_ms();

  // reset values to prevent instant timeout
  context->m_last_recv_time = get_time_ms();
  context->m_last_send_time = context->m_last_recv_time;
  return 0;
}

int netclient_transport_info(nc_client *context, nc_transport_info *info) {
  if(!context || !info)
    return -1;

  if(context->m_state != NETCLIENT_STATE_CONNECTED)
    return -2;

  info->last_received = context->m_reliability.GetLastRecvId();
  info->last_sent = context->m_reliability.GetLastSendId();
  info->last_acked = context->m_reliability.GetLastAckedId();
  info->last_acked_bitmask = context->m_reliability.GetLastAckedIdBitmask();
  info->rtt = context->m_reliability.m_rtt_log.Begin();
  info->smoothed_rtt = context->m_reliability.m_smoothed_rtt_log.Begin();
  info->rtt_size = context->m_reliability.m_rtt_log.Size();
  return 0;
}