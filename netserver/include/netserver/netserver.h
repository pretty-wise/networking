#include <stdint.h>

#define NETSERVER_STATE_ENDPOINT_CONNECTED 2
#define NETSERVER_STATE_ENDPOINT_DISCONNECTED 3

extern "C" {

struct ns_endpoint;

struct ns_config {
  uint16_t port;
  uint16_t num_endpoints;
  struct netsimulator *simulator;

  void (*state_callback)(uint32_t state, ns_endpoint *endpoint,
                         void *user_data);
  void (*packet_callback)(uint16_t id, void *user_data);
  int (*send_callback)(uint16_t id, void *buffer, uint32_t nbytes);
  int (*recv_callback)(uint16_t id, const void *buffer, uint32_t nbytes);
  void *user_data;
};

struct ns_server *netserver_create(ns_config *config);
void netserver_destroy(struct ns_server *context);
void netserver_update(struct ns_server *context);
}