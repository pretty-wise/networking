#pragma once
#include "simcommon/simulation.h"
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
  static const uint32_t MAX_ENTITY_COUNT = 4;

  MessageType m_type;
  frameid_t m_frame_id;

  // todo(kstasik): avoid sending 3 configuration values
  // in every update
  uint64_t m_start_time;
  uint64_t m_frame_duration;
  uint64_t m_start_frame;

  uint8_t m_cmdbuffer_size;

  // todo(kstasik): improved entity creation needed here.
  // for now one client can control only one entity. and the id of this entity
  // is sent every frame. I need to add support for multiple entities and avoid
  // sending the ids of those entities in every frame.
  // or maybe simply always allow just one local entity and send this
  // information along with simulation configuration
  entityid_t m_local_entity;

  uint32_t m_entity_count;
};

struct EntityUpdate {
  entityid_t m_id;
  entitymovement_t m_movement;
};