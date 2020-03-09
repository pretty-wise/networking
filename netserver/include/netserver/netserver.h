#include <stdint.h>

extern "C" {
struct ns_server *netserver_create(uint16_t *port, uint16_t num_endpoints,
                                   struct netsimulator *simulator);
void netserver_destroy(struct ns_server *context);
void netserver_update(struct ns_server *context);
}