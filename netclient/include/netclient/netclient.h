#pragma once
#include <stdint.h>

#define NETCLIENT_STATE_DISCONNECTED 0
#define NETCLIENT_STATE_DISCOVERING 1
#define NETCLIENT_STATE_CONNECTING 2
#define NETCLIENT_STATE_CONNECTED 3

#define NETCLIENT_PACKET_ACK 0
#define NETCLIENT_PACKET_NACK 1

extern "C" {

struct nc_client;

struct nc_config {
  const char *server_address;
  uint16_t server_port;
  int32_t timeout;

  void (*state_callback)(int32_t state, void *user_data);
  void (*packet_callback)(uint16_t id, int32_t type, void *user_data);
  int (*send_callback)(uint16_t id, void *buffer, uint32_t nbytes);
  int (*recv_callback)(uint16_t id, const void *buffer, uint32_t nbytes);
  void *user_data;
};

void netclient_make_default(nc_config *config);

nc_client *netclient_create(const nc_config *config);

int netclient_disconnect(nc_client *context);

int netclient_connect(nc_client *context, const char *addr, uint16_t port);

void netclient_destroy(nc_client *context);

void netclient_update(nc_client *context);

struct nc_transport_info {
  int32_t last_sent;
  int32_t last_received;
  int32_t last_acked;
  int32_t last_acked_bitmask;
  float *rtt;
  float *smoothed_rtt;
  uint32_t rtt_size;
};

int netclient_transport_info(nc_client *context, nc_transport_info *info);
}