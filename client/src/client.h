#include "common/socket.h"
#include <cstdint>

class Client {
public:
  Client(const char *server_address, uint16_t server_port);
  void Update();

  void Connect();
  void Disconnect();
  bool IsConnected() const;

private:
  int m_socket;
  sockaddr_storage m_server;
  enum class State {
    Disconnected,
    Discovering,
    Connecting,
    Connected,
    Disconnecting
  } m_state;
  uint16_t m_client_salt;
  uint16_t m_server_salt;
};