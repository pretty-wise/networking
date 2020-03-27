#pragma once
#include "simcommon/types.h"
#include <cstdint>

enum class MessageType : uint8_t {
  Command = 1, // client  -> server
  Update = 2,  // client <-  server
};

struct SimulationMessage {
  MessageType m_type;
};

struct CommandMessage {
  MessageType m_type;
  frameid_t m_frame_id;
  uint32_t m_buttons;
};

struct UpdateMessage {
  MessageType m_type;
  frameid_t m_frame_id;
  // todo(kstasik): avoid sending 3 configuration values
  // in every update
  uint64_t m_start_time;
  uint64_t m_frame_duration;
  uint64_t m_start_frame;
};