#pragma once
#include "netclient/netclient.h"
#include "netcommon/reliability.h"
#include "netcommon/socket.h"
#include <cstdint>

class Client {
public:
  typedef void (*state_callback_t)(int32_t, void *);
  Client(const char *server_address, uint16_t server_port, uint32_t timeout,
         state_callback_t state_callback, void *user_data);
  ~Client();
  void Update();

  bool Connect(const char *server_address, uint16_t server_port);
  void Disconnect();
  bool IsConnected() const;
  bool IsDisconnected() const;

  bool GetTransportInfo(nc_transport_info &info);

private:
  enum class State { Disconnected, Discovering, Connecting, Connected };

private:
  void SetState(State s);

private:
  int m_socket;
  sockaddr_storage m_server;
  State m_state;
  uint16_t m_client_salt;
  uint16_t m_server_salt;

  uint32_t m_timeout;
  uint32_t m_last_recv_time;
  uint32_t m_last_send_time;
  Reliability m_reliability;

  state_callback_t m_state_cb;
  void *m_user_data;
};