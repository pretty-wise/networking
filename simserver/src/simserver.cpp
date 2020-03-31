#include "simserver/simserver.h"
#include "simcommon/protocol.h"
#include "simcommon/simulation.h"
#include "utils/circularbuffer.h"
#include "utils/time.h"
#include <assert.h>

struct serversim_t {
  frameid_t head;

  entityid_t m_entity_id[SIMSERVER_ENTITY_CAPACITY];
  entitymovement_t m_entity_movement[SIMSERVER_ENTITY_CAPACITY];
  uint32_t m_entity_count;

  entityid_t m_remote[SIMSERVER_PEER_CAPACITY];
  simcmd_t m_prev_cmd[SIMSERVER_PEER_CAPACITY];
};

struct ss_simulation {
  ss_config config;

  serversim_t *simulation;
  uint64_t last_update_time;
  uint64_t time_acc;

  struct PeerData {
    PeerData() : input_buffer(128), buffer_size_log(128) {}

    void Reset() {
      input_buffer.Clear();
      buffer_size_log.Clear();
    }
    entityid_t remote_entity = 0;

    struct FrameInput {
      frameid_t frame;
      simcmd_t cmd;
    };
    CircularBuffer<FrameInput> input_buffer;
    CircularBuffer<float> buffer_size_log;
  };

  simpeer_t *peer_id[SIMSERVER_PEER_CAPACITY];
  PeerData peer_data[SIMSERVER_PEER_CAPACITY];
};

static uint32_t peer_count(ss_simulation *sim) {
  uint32_t count = 0;
  for(int i = 0; i < SIMSERVER_PEER_CAPACITY; ++i) {
    if(sim->peer_id[i] != nullptr)
      ++count;
  }
  return count;
}

static bool find_frame_cmd(ss_simulation::PeerData &info, frameid_t frame,
                           simcmd_t *result) {
  for(auto *it = info.input_buffer.Begin(); it != info.input_buffer.End();
      ++it) {
    if(it->frame == frame) {
      *result = it->cmd;
      return true;
    }
  }

  // of the input is not found duplicate the last available
  // input
  if(info.input_buffer.Size() > 0) {
    *result = (info.input_buffer.End() - 1)->cmd;
  }
  return false;
}

static uint32_t collect_cmds(ss_simulation *sim, frameid_t frame,
                             entityid_t entities[SIMSERVER_PEER_CAPACITY],
                             simcmd_t cmds[SIMSERVER_PEER_CAPACITY]) {
  uint32_t num_entities = 0;
  for(uint32_t i = 0; i < SIMSERVER_PEER_CAPACITY; ++i) {
    ss_simulation::PeerData &info = sim->peer_data[i];
    if(sim->peer_id[i] != nullptr && info.remote_entity != 0) {
      entities[num_entities] = info.remote_entity;

      simcmd_t peer_cmd = {};
      if(!find_frame_cmd(info, frame, &peer_cmd)) {
        // send feedback
      }

      cmds[num_entities] = peer_cmd;
      num_entities++;
    }
  }
  return num_entities;
}

static void remove_stale_cmds(ss_simulation *sim) {
  for(uint32_t i = 0; i < SIMSERVER_PEER_CAPACITY; ++i) {
    if(sim->peer_id[i] == nullptr)
      continue;

    auto &peer = sim->peer_data[i];
    peer.buffer_size_log.PushBack((float)peer.input_buffer.Size());

    while(peer.input_buffer.Size() > 0) {
      auto *input = peer.input_buffer.Begin();
      if(input->frame < sim->simulation->head) {
        peer.input_buffer.PopFront(1);
      } else {
        break;
      }
    }
  }
}

static uint32_t find_peer(ss_simulation *sim, simpeer_t *peer) {
  for(uint32_t i = 0; i < SIMSERVER_PEER_CAPACITY; ++i) {
    if(sim->peer_id[i] == peer) {
      return i;
    }
  }
  return -1;
}

static uint32_t find_entity(serversim_t *sim, entityid_t id) {
  for(uint32_t i = 0; i < SIMSERVER_ENTITY_CAPACITY; ++i) {
    if(sim->m_entity_id[i] == id) {
      return i;
    }
  }
  return -1;
}

static entityid_t add_entity(serversim_t *sim) {
  assert(sim != nullptr);

  static entityid_t entity_generator = 0;

  // generate entity for a joining peer
  entityid_t id = 0;
  while(id == 0) {
    id = ++entity_generator;
  }

  uint32_t i = sim->m_entity_count;

  sim->m_entity_id[i] = id;
  sim->m_entity_movement[i] = {};
  sim->m_entity_count++;
  return id;
}

static void remove_entity(serversim_t *sim, entityid_t id) {
  assert(id != 0);
  assert(sim != nullptr);

  uint32_t entity_idx = find_entity(sim, id);
  if(entity_idx == -1)
    return;

  sim->m_entity_id[entity_idx] = 0;
  sim->m_entity_movement[entity_idx] = {};
  --sim->m_entity_count;
}

static entityid_t add_remote_entity(serversim_t *sim) {
  entityid_t id = add_entity(sim);
  for(uint32_t i = 0; i < SIMSERVER_PEER_CAPACITY; ++i) {
    if(sim->m_remote[i] != 0) {
      sim->m_remote[i] = id;
      sim->m_prev_cmd[i] = {};
      break;
    }
  }
  return id;
}

static uint32_t find_entity_owner(serversim_t *sim, entityid_t id) {
  for(uint32_t i = 0; i < SIMSERVER_PEER_CAPACITY; ++i) {
    if(sim->m_remote[i] == id) {
      return i;
    }
  }
  return -1;
}

static void remove_remote_entity(serversim_t *sim, entityid_t id) {
  for(uint32_t i = 0; i < SIMSERVER_PEER_CAPACITY; ++i) {
    if(sim->m_remote[i] == id) {
      sim->m_remote[i] = 0;
      sim->m_prev_cmd[i] = {};
    }
  }
  remove_entity(sim, id);
}

static void add_peer(ss_simulation *sim, uint32_t index, simpeer_t *peer) {
  entityid_t new_entity = add_remote_entity(sim->simulation);

  sim->peer_id[index] = peer;
  sim->peer_data[index].Reset();
  sim->peer_data[index].remote_entity = new_entity;
}

static void remove_peer(ss_simulation *sim, uint32_t index) {
  entityid_t entity_id = sim->peer_data[index].remote_entity;
  remove_remote_entity(sim->simulation, entity_id);
  sim->peer_data[index].Reset();

  sim->peer_id[index] = nullptr;
}

static void step_server_simulation(serversim_t *sim,
                                   entityid_t entities[SIMSERVER_PEER_CAPACITY],
                                   simcmd_t cmds[SIMSERVER_PEER_CAPACITY],
                                   uint32_t num_entities) {

  // create inputs
  siminput_t inputs[SIMSERVER_PEER_CAPACITY];
  for(int i = 0; i < SIMSERVER_PEER_CAPACITY; ++i) {
    inputs[i].previous = {};
    inputs[i].current = cmds[i];
    if(sim->m_remote[i] == entities[i])
      inputs[i].previous = sim->m_prev_cmd[i];
  }

  sim->head += 1;

  uint32_t remote_idx = 0;
  while(entities[remote_idx] != 0) {
    uint32_t idx = find_entity(sim, entities[remote_idx]);
    if(idx != -1) {
      entitymovement_t previous = sim->m_entity_movement[idx];
      step_movement(sim->m_entity_movement[idx], previous, inputs[remote_idx]);
    }
    remote_idx++;
  };

  // store cmds for the next frame input
  for(int i = 0; i < SIMSERVER_PEER_CAPACITY; ++i) {
    sim->m_remote[i] = entities[i];
    sim->m_prev_cmd[i] = cmds[i];
  }
}

void simserver_make_default(ss_config *config) {
  config->frame_duration = 32 * 1000;
  config->start_frame = 0;
  config->start_time = get_time_us();
}

ss_simulation *simserver_create(ss_config *config) {
  ss_simulation *sim = new ss_simulation{};

  for(int i = 0; i < SIMSERVER_PEER_CAPACITY; ++i) {
    sim->peer_id[i] = nullptr;
    sim->peer_data[i].Reset();
  }

  if(config) {
    sim->config = *config;
    simserver_start(sim, config);
  }

  return sim;
}

void simserver_destroy(ss_simulation *sim) {
  if(sim->simulation) {
    simserver_stop(sim);
  }
  delete sim;
}

int simserver_start(ss_simulation *sim, ss_config *config) {
  if(sim->simulation)
    return -1;

  if(config) // otherwise the old config will be used
    sim->config = *config;

  sim->simulation = new serversim_t{};
  sim->simulation->head = sim->config.start_frame;

  sim->last_update_time = sim->config.start_time;
  sim->time_acc = 0;

  return 0;
}

void simserver_stop(ss_simulation *sim) {
  if(!sim->simulation)
    return;

  delete sim->simulation;
  sim->simulation = nullptr;
}

uint32_t simserver_write(uint16_t id, void *buffer, uint32_t nbytes,
                         simpeer_t *peer, ss_simulation *sim) {
  assert(sizeof(UpdateMessage) <= nbytes);
  auto *msg = (UpdateMessage *)buffer;
  msg->m_type = MessageType::Update;
  if(sim->simulation) {
    msg->m_frame_id = sim->simulation->head;
    msg->m_start_time = sim->config.start_time;
    msg->m_frame_duration = sim->config.frame_duration;
    msg->m_start_frame = sim->config.start_frame;
    uint32_t peer_idx = find_peer(sim, peer);
    assert(peer_idx != -1);
    msg->m_cmdbuffer_size = sim->peer_data[peer_idx].input_buffer.Size();
    msg->m_movement_count = sim->simulation->m_entity_count;
    for(uint32_t i = 0; i < msg->m_movement_count; ++i) {
      msg->m_entities[i] = sim->simulation->m_entity_id[i];
      msg->m_movement[i] = sim->simulation->m_entity_movement[i];
    }
  } else {
    msg->m_frame_id = 0;
    msg->m_start_time = 0;
    msg->m_frame_duration = 0;
    msg->m_start_frame = 0;
    msg->m_cmdbuffer_size = 0;
    msg->m_movement_count = 0;
  }

  return sizeof(UpdateMessage);
}

uint32_t simserver_read(uint16_t id, const void *buffer, uint32_t nbytes,
                        simpeer_t *peer, ss_simulation *sim) {
  const auto *base = (const SimulationMessage *)buffer;

  uint32_t peer_idx = find_peer(sim, peer);
  if(peer_idx == -1)
    return -1;

  if(base->m_type == MessageType::Command) {
    if(!sim->simulation)
      return 0;

    const auto *msg = (const CommandMessage *)buffer;
    if(msg->m_frame_id < sim->simulation->head) {
      return 0; // ignore stale input.
    }

    simcmd_t cmd = {msg->m_buttons};
    sim->peer_data[peer_idx].input_buffer.PushBack(
        ss_simulation::PeerData::FrameInput{msg->m_frame_id, cmd});
    return 0;
  }
  return -2;
}

void simserver_ack(uint16_t id, simpeer_t *peer, ss_simulation *sim) {
  // acks not used for now
}

void simserver_connection(uint32_t state, simpeer_t *peer, ss_simulation *sim) {
  if(state == SIMSERVER_STATE_PEER_CONNECTED) {
    uint32_t empty_idx = find_peer(sim, nullptr);
    if(empty_idx != -1) {
      add_peer(sim, empty_idx, peer);
    }
  } else if(state == SIMSERVER_STATE_PEER_DISCONNECTED) {
    uint32_t peer_idx = find_peer(sim, peer);
    if(peer_idx != -1) {
      remove_peer(sim, peer_idx);
    }
  }
}

void simserver_update(ss_simulation *sim) {
  if(sim->simulation) {
    uint64_t now = get_time_us();
    uint64_t dt = now - sim->last_update_time;
    assert(dt >= 0);

    sim->time_acc += dt;
    while(sim->time_acc >= sim->config.frame_duration) {

      entityid_t entities[SIMSERVER_PEER_CAPACITY] = {};
      simcmd_t commands[SIMSERVER_PEER_CAPACITY] = {};

      uint32_t num_remote_entities =
          collect_cmds(sim, sim->simulation->head, entities, commands);

      step_server_simulation(sim->simulation, entities, commands,
                             num_remote_entities);

      sim->time_acc -= sim->config.frame_duration;

      remove_stale_cmds(sim);
    }

    sim->last_update_time = now;
  }
}

int simserver_info(ss_simulation *sim, ss_info *info) {
  info->running = sim->simulation != nullptr;
  if(sim->simulation) {
    info->head = sim->simulation->head;
    info->peer_count = peer_count(sim);

    for(int i = 0, j = 0; i < SIMSERVER_PEER_CAPACITY; ++i) {
      if(sim->peer_id[i] != nullptr) {
        info->peer_id[j] = sim->peer_id[i];
        info->remote_entity[j] = sim->peer_data[i].remote_entity;
        info->input_buffer_size[j] = sim->peer_data[i].input_buffer.Size();
        info->buffer_size_log[j] = sim->peer_data[i].buffer_size_log.Begin();
        info->buffer_size_log_size[j] =
            sim->peer_data[i].buffer_size_log.Size();
        j++;
      }
    }
  }
  return 0;
}

entityid_t simserver_entity_create(ss_simulation *sim, simpeer_t *owner) {
  if(!sim->simulation)
    return 0; // no simulation running

  if(!owner)
    return 0; // for now only entities owned by peers are supported

  uint32_t peer_idx = find_peer(sim, owner);
  assert(peer_idx != -1);

  auto &peer_data = sim->peer_data[peer_idx];

  if(peer_data.remote_entity != 0) {
    return 0; // peer already owns an entity. for now only one is supported
  }

  peer_data.remote_entity = add_remote_entity(sim->simulation);
  return peer_data.remote_entity;
}

int simserver_entity_destroy(ss_simulation *sim, entityid_t entity) {
  if(!sim->simulation)
    return -1;

  uint32_t peer_idx = find_entity_owner(sim->simulation, entity);
  if(peer_idx != -1) {
    remove_remote_entity(sim->simulation, entity);
    sim->peer_data[peer_idx].remote_entity = 0;
  } else {
    remove_entity(sim->simulation, entity);
  }

  return 0;
}

int simserver_entity_movement(ss_simulation *sim, entityid_t **ids,
                              entitymovement_t **data, uint32_t *count) {
  *count = 0;
  if(!sim->simulation)
    return -1;

  *count = sim->simulation->m_entity_count;

  if(*count > 0) {
    *ids = sim->simulation->m_entity_id;
    *data = sim->simulation->m_entity_movement;
  }
  return 0;
}