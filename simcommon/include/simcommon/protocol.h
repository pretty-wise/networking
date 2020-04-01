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

  // todo(kstasik): pack this better
  uint32_t m_movement_count;
  entityid_t m_entities[SIMSERVER_ENTITY_CAPACITY];
  entitymovement_t m_movement[SIMSERVER_ENTITY_CAPACITY];
};