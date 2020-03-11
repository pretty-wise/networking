#include <stdint.h>

#define NETSERVER_STATE_ENDPOINT_CONNECTED 2
#define NETSERVER_STATE_ENDPOINT_DISCONNECTED 3

extern "C" {

struct ns_endpoint;
struct ns_server;

struct ns_config {
  uint16_t port;
  uint16_t num_endpoints;
  struct netsimulator *simulator;

  void (*state_callback)(uint32_t state, ns_endpoint *endpoint,
                         void *user_data);
  void (*ack_callback)(uint16_t id, void *user_data, ns_endpoint *e);
  int (*send_callback)(uint16_t id, void *buffer, uint32_t nbytes,
                       ns_endpoint *dst);
  int (*recv_callback)(uint16_t id, const void *buffer, uint32_t nbytes,
                       ns_endpoint *src);
  void *user_data;
};

ns_server *netserver_create(ns_config *config);

void netserver_destroy(ns_server *context);

void netserver_update(ns_server *context);

struct ns_transport_info {
  ns_endpoint *endpoint;

  int32_t last_sent;
  int32_t last_received;
  int32_t last_acked;
  int32_t last_acked_bitmask;

  uint32_t last_rtt;
  uint32_t smoothed_rtt;

  float *rtt_log;
  float *smoothed_rtt_log;
  uint32_t rtt_log_size;
};

int netserver_transport_info(ns_server *context, ns_endpoint *endpoint,
                             ns_transport_info *info);
}