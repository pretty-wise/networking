#include "netserver/netserver.h"
#include "server.h"

void *netserver_create(uint16_t *port, uint16_t num_endpoints) {
  Server *server = new Server(*port, num_endpoints);
  return server;
}
void netserver_destroy(void *context) {
  Server *server = (Server *)context;
  delete server;
}
void netserver_update(void *context) {
  Server *server = (Server *)context;
  server->Update();
}