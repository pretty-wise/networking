#pragma once
#include "common/reliability.h"
#include <cstdint>
#include <sys/socket.h>

class Server {
public:
  struct Endpoint {
    sockaddr_storage m_address;
    enum class State {
      Undefined,
      Connecting,
      Connected
    } m_state = State::Undefined;
    uint16_t m_client_salt;
    uint16_t m_server_salt;
    uint32_t m_last_recv_time;
    uint32_t m_last_send_time;
    Reliability m_reliability;
  };

public:
  Server(uint16_t &port, uint16_t num_endpoints);
  ~Server();
  void Update();

  uint32_t FindEndpoint(const sockaddr_storage &address);
  uint32_t AddEndpoint(const sockaddr_storage &address, uint16_t client_salt);
  void RemoveEndpoint(uint32_t index);

private:
  uint32_t m_timeout;
  uint16_t m_endpoint_capacity;
  uint16_t m_endpoint_count;
  Endpoint *m_endpoints;

  int m_socket;
};