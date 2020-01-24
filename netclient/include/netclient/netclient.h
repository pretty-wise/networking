#include <stdint.h>

extern "C" {
void *netclient_create(const char *address, uint16_t port);
void netclient_destroy(void *context);
void netclient_update(void *context);
}