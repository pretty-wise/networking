#include "netclient/netclient.h"
#include "netcommon/log.h"
#include "netcommon/netsimulator.h"
#include "netcommon/protocol.h"
#include "netcommon/reliability.h"
#include "netcommon/socket.h"
#include "utils/time.h"

struct nc_client {
  int m_socket;
  sockaddr_storage m_local;
  sockaddr_storage m_server;
  int m_state;
  uint16_t m_client_salt;
  uint16_t m_server_salt;

  uint32_t m_timeout;
  uint32_t m_last_recv_time;
  uint32_t m_last_send_time;
  Reliability m_reliability;

  netsimulator *m_simulator;

  void (*m_state_cb)(int32_t state, void *user_data);
  void (*m_packet_cb)(sequence_t id, void *user_data);
  int (*m_send_cb)(uint16_t id, void *buffer, uint32_t nbytes, void *user_data);
  int (*m_recv_cb)(uint16_t id, const void *buffer, uint32_t nbytes,
                   void *user_data);
  void *m_user_data;

  enum TimeSyncState { None, Running, Done };
  static const uint32_t MAX_TIME_SYNC_SAMPLES = 32;

  struct TimeSyncSample {
    uint32_t m_schedule_time;
    uint64_t m_request_send_time;
    uint64_t m_request_recv_time;
    uint64_t m_response_send_time;
    uint64_t m_response_recv_time;
  };

  TimeSyncState m_time_sync_state;
  TimeSyncSample m_time_sync_samples[MAX_TIME_SYNC_SAMPLES];
  int64_t m_server_time_offset;
};

static void start_time_sync(nc_client *client) {
  uint32_t t = get_time_ms();
  uint32_t dt = 100;
  client->m_time_sync_state = nc_client::TimeSyncState::Running;
  for(uint32_t i = 0; i < nc_client::MAX_TIME_SYNC_SAMPLES; ++i) {
    client->m_time_sync_samples[i].m_schedule_time = t + dt * i;
  }
  client->m_server_time_offset = 0;
}

static void finish_time_sync(nc_client *client) {
  // todo: discard the outliers, select best candidates only
  // todo: time sync is skewed by the update tick.
  //       server should wait for the system event about receiving a packet
  //		 that would make m_responseSendTime and m_requestRecvTime be
  // actually different 		 and more precise

  client->m_time_sync_state = nc_client::TimeSyncState::Done;

  int64_t totalDiff = 0;

  for(uint32_t i = 0; i < nc_client::MAX_TIME_SYNC_SAMPLES; ++i) {
    const auto &sample = client->m_time_sync_samples[i];

    uint64_t rtt = sample.m_response_recv_time - sample.m_request_send_time;
    uint64_t localTime = sample.m_request_send_time + (rtt / 2);
    uint64_t serverTime =
        sample.m_request_recv_time +
        (sample.m_response_send_time - sample.m_request_recv_time);
    int64_t diff = localTime - serverTime;
    totalDiff += diff;
  }

  int64_t clockOffset = totalDiff / nc_client::MAX_TIME_SYNC_SAMPLES;
  client->m_server_time_offset = clockOffset;
}

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
  client->m_simulator = config->simulator;

  create_udp_addr("127.0.0.1", src_port, &client->m_local);

  netclient_connect(client, config->server_address, config->server_port);
  return client;
}

void netclient_destroy(nc_client *context) {
  if(context->m_state == NETCLIENT_STATE_CONNECTED) {
    netclient_disconnect(context);
  }
  close_socket(context->m_socket);
  delete context;
}

static bool netclient_send(nc_client *context, void *buffer, uint32_t nbytes) {
  if(context->m_simulator) {
    netsimulator_send(context->m_simulator, buffer, nbytes, context->m_local,
                      context->m_server);
    return true;
  } else {
    int error;
    size_t sent = socket_send(context->m_socket, &context->m_server,
                              (const void *)buffer, nbytes, &error);
    LOG_TRANSPORT_DBG("sent %lubytes from %lubytes. error: %d", sent, nbytes,
                      error);
    return sent == nbytes;
  }
}

static void netclient_recv(nc_client *context, uint8_t *buffer, uint32_t nbytes,
                           const sockaddr_storage &src) {
  PacketHeader *header = (PacketHeader *)buffer;
  if(header->m_protocol_id != game_protocol_id) {
    LOG_TRANSPORT_WAR("discarding unknown protocol");
    return;
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
        context->m_state_cb(NETCLIENT_STATE_CONNECTING, context->m_user_data);
      context->m_state = NETCLIENT_STATE_CONNECTING;
      context->m_last_recv_time = get_time_ms();
    }
  } else if(header->m_type == PacketType::Payload &&
            (context->m_state == NETCLIENT_STATE_CONNECTING ||
             context->m_state == NETCLIENT_STATE_CONNECTED)) {
    auto *packet = (PayloadPacket *)buffer;
    if(context->m_state == NETCLIENT_STATE_CONNECTING) {
      if(context->m_state_cb)
        context->m_state_cb(NETCLIENT_STATE_CONNECTED, context->m_user_data);
      context->m_state = NETCLIENT_STATE_CONNECTED;
      LOG_TRANSPORT_INF("connected");
      start_time_sync(context);
    }
    if(context->m_reliability.IsStale(packet->m_sequence)) {
      LOG_TRANSPORT_WAR("stale packet %d", packet->m_sequence);
    } else {
      int result = context->m_recv_cb(
          packet->m_sequence, buffer + sizeof(PayloadPacket),
          nbytes - sizeof(PayloadPacket), context->m_user_data);
      if(result != 0) {
        LOG_TRANSPORT_WAR("received: PacketType::Payload. read error %d",
                          packet->m_sequence);
      } else {
        sequence_bitmask_t acks = context->m_reliability.Ack(
            packet->m_sequence, packet->m_ack, packet->m_ack_bitmask);

        for(int i = 0; i < 32; ++i) {
          if((acks & (1 << i)) != 0) {
            context->m_packet_cb(packet->m_ack + i, context->m_user_data);
          }
        }

        context->m_last_recv_time = get_time_ms();

        LOG_TRANSPORT_DBG("received: PacketType::Payload seq %d. ack: %d",
                          packet->m_sequence, packet->m_ack);
      }
    }
  } else if(header->m_type == PacketType::TimeSync) {
    auto *packet = (TimeSync *)buffer;
    packet->m_response_recv_time = get_time_us();

    if(packet->m_id < nc_client::MAX_TIME_SYNC_SAMPLES) {
      nc_client::TimeSyncSample &sample =
          context->m_time_sync_samples[packet->m_id];
      sample.m_request_send_time = packet->m_request_send_time;
      sample.m_request_recv_time = packet->m_request_recv_time;
      sample.m_response_send_time = packet->m_response_send_time;
      sample.m_response_recv_time = packet->m_response_recv_time;
      sample.m_schedule_time = 0;
    }
  }
}

void netclient_update(nc_client *context) {

  if(context->m_state == NETCLIENT_STATE_DISCONNECTED)
    return;

  size_t nbytes = 1280;
  uint8_t buffer[nbytes];
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
                       nbytes - sizeof(PayloadPacket), context->m_user_data);
  }

  if(!netclient_send(context, buffer, nbytes)) {
    LOG_TRANSPORT_ERR("socket send error: %d", error);
  } else {
    context->m_last_send_time = get_time_ms();
  }

  // send time sync request
  if(context->m_time_sync_state == nc_client::TimeSyncState::Running) {
    auto *header = (TimeSync *)buffer;
    header->m_protocol_id = game_protocol_id;
    header->m_type = PacketType::TimeSync;

    uint32_t numPending = 0;
    for(uint32_t i = 0; i < nc_client::MAX_TIME_SYNC_SAMPLES; ++i) {
      nc_client::TimeSyncSample &sample = context->m_time_sync_samples[i];
      if(sample.m_schedule_time == 0)
        continue;

      numPending++;
      if(sample.m_schedule_time <= get_time_ms()) {
        header->m_id = i;
        header->m_request_send_time = get_time_us();
        header->m_request_recv_time = 0;
        header->m_response_send_time = 0;
        header->m_response_recv_time = 0;
        netclient_send(context, buffer, sizeof(TimeSync));
        sample.m_schedule_time = get_time_ms() + 500;
      }
    }

    if(numPending == 0) {
      finish_time_sync(context);
    }
  }

  if(context->m_simulator) {
    const uint32_t max_received = 8;
    uint8_t *buffer[max_received];
    uint32_t nbytes[max_received];
    sockaddr_storage src[max_received];
    uint32_t num_received =
        netsimulator_recv(context->m_simulator, context->m_local, buffer,
                          nbytes, src, max_received);
    for(uint32_t i = 0; i < num_received; ++i) {
      netclient_recv(context, buffer[i], nbytes[i], src[i]);
      free(buffer[i]);
    }
  } else {
    sockaddr_storage source;
    size_t received = 0;
    do {
      received = socket_receive(context->m_socket, (void *)buffer, nbytes,
                                &source, &error);
      if(received == -1) {
        LOG_TRANSPORT_ERR("error %d", error);
      } else if(received > 0) {
        netclient_recv(context, buffer, nbytes, source);
      }
    } while(received > 0 && received != -1);
  }

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
    if(!netclient_send(context, buffer, nbytes)) {
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

  info->last_rtt = context->m_reliability.m_last_rtt;
  info->smoothed_rtt = context->m_reliability.m_smoothed_rtt;

  info->rtt_log = context->m_reliability.m_rtt_log.Begin();
  info->smoothed_rtt_log = context->m_reliability.m_smoothed_rtt_log.Begin();
  info->rtt_log_size = context->m_reliability.m_rtt_log.Size();

  if(context->m_time_sync_state == nc_client::TimeSyncState::None) {
    info->server_time_offset = -1;
  } else if(context->m_time_sync_state == nc_client::TimeSyncState::Running) {
    info->server_time_offset = -2;
  } else {
    info->server_time_offset = context->m_server_time_offset;
  }
  return 0;
}
