#pragma once
#include "netcommon/sequence.h"
#include <cstdint>

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
