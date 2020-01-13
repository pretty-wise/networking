#include <cstdint>
#include <sys/socket.h>

struct Endpoint {
  sockaddr_storage m_address;
  uint32_t m_nbytes_received;
  uint8_t m_state;
};