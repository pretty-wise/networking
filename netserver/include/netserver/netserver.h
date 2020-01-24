#include <stdint.h>

extern "C" {
void *netserver_create(uint16_t *port, uint16_t num_endpoints);
void netserver_destroy(void *context);
void netserver_update(void *context);
}