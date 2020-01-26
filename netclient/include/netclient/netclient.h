#pragma once
#include <stdint.h>

#define NETCLIENT_STATE_DISCONNECTED 0
#define NETCLIENT_STATE_DISCOVERING 1
#define NETCLIENT_STATE_CONNECTING 2
#define NETCLIENT_STATE_CONNECTED 3

#define NETCLIENT_PACKET_ACK 0
#define NETCLIENT_PACKET_NACK 1

extern "C" {

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

void *netclient_create(const nc_config *config);

int netclient_disconnect(void *context);

int netclient_connect(void *context, const char *addr, uint16_t port);

void netclient_destroy(void *context);

void netclient_update(void *context);

struct nc_transport_info {
  int32_t last_sent;
  int32_t last_received;
  int32_t last_acked;
  int32_t last_acked_bitmask;
};

int netclient_transport_info(void *context, nc_transport_info *info);
}