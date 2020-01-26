#include "netclient/netclient.h"
#include "client.h"

void netclient_make_default(nc_config *config) {
  config->timeout = 5000;
  config->state_callback = nullptr;
  config->user_data = nullptr;
  config->packet_callback = nullptr;
  config->send_callback = nullptr;
  config->recv_callback = nullptr;
}

void *netclient_create(const nc_config *config) {
  if(!config)
    return nullptr;

  if(!config->send_callback || !config->recv_callback) {
    return nullptr;
  }

  Client *client = new Client(config->server_address, config->server_port,
                              config->timeout, config->state_callback,
                              config->packet_callback, config->send_callback,
                              config->recv_callback, config->user_data);
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

int netclient_disconnect(void *context) {
  if(!context)
    return -1;
  Client *client = (Client *)context;
  if(client->IsDisconnected()) {
    return -2;
  }
  client->Disconnect();
  return 0;
}

int netclient_connect(void *context, const char *addr, uint16_t port) {
  if(!context)
    return -1;
  Client *client = (Client *)context;
  if(!client->IsDisconnected()) {
    return -2;
  }
  if(!client->Connect(addr, port)) {
    return -3;
  }
  return 0;
}

int netclient_transport_info(void *context, nc_transport_info *info) {
  if(!context || !info)
    return -1;
  Client *client = (Client *)context;

  if(!client->GetTransportInfo(*info)) {
    return -2;
  }
  return 0;
}