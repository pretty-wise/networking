#include "netclient/netclient.h"
#include "client.h"

void *netclient_create(const char *address, uint16_t port) {
  Client *client = new Client(address, port);
  return client;
}
void netclient_destroy(void *context) {
  Client *client = (Client *)context;
  delete client;
}
void netclient_update(void *context) {
  Client *client = (Client *)context;
  client->Update();
}