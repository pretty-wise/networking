#pragma once
#include <cstdint>
#include <limits>

typedef uint16_t sequence_t;
typedef uint32_t sequence_bitmask_t;

inline bool IsMoreRecent(sequence_t a, sequence_t b) {
  return ((a > b) && (a - b <= std::numeric_limits<sequence_t>::max() / 2)) ||
         ((b > a) && (b - a > std::numeric_limits<sequence_t>::max() / 2));
}

inline bool IsLessRecent(sequence_t a, sequence_t b) {
  return IsMoreRecent(b, a);
}

inline sequence_t Distance(sequence_t a, sequence_t b) {
  // return a - b;
  return (a >= b) ? a - b
                  : (std::numeric_limits<sequence_t>::max() - b) + a + 1;
}

enum class PacketType : uint8_t {
  Request = 1,    // client  -> server
  Response = 2,   // client <- server
  Establish = 3,  // client  -> server
  Payload = 4,    // client <-> server
  Disconnect = 5, // client  -> server
};

const uint32_t game_protocol_id = 0x1234;

struct PacketHeader {
  uint32_t m_protocol_id;
  PacketType m_type;
};

struct ConnectionRequestPacket {
  uint32_t m_protocol_id;
  PacketType m_type;
  uint32_t m_client_salt;
};

struct ConnectionResponsePacket {
  uint32_t m_protocol_id;
  PacketType m_type;
  uint32_t m_client_salt;
  uint32_t m_server_salt;
};

struct ConnectionEstablishPacket {
  uint32_t m_protocol_id;
  PacketType m_type;
  uint32_t m_key;
};

struct ConnectionDisconnectPacket {
  uint32_t m_protocol_id;
  PacketType m_type;
  uint32_t m_key;
};

struct PayloadPacket {
  uint32_t m_protocol_id;
  PacketType m_type;
  sequence_t m_sequence;
  sequence_t m_ack;
  sequence_bitmask_t m_ack_bitmask;
};
