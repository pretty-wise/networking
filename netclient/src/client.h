#pragma once
#include "netcommon/reliability.h"
#include "netcommon/socket.h"
#include <cstdint>

class Client {
public:
  Client(const char *server_address, uint16_t server_port);
  ~Client();
  void Update();

  bool Connect(const char *server_address, uint16_t server_port);
  void Disconnect();
  bool IsConnected() const;
  bool IsDisconnected() const;

private:
  int m_socket;
  sockaddr_storage m_server;
  enum class State { Disconnected, Discovering, Connecting, Connected } m_state;
  uint16_t m_client_salt;
  uint16_t m_server_salt;

  uint32_t m_timeout; // todo(kstasik): configure this
  uint32_t m_last_recv_time;
  uint32_t m_last_send_time;
  Reliability m_reliability;
};