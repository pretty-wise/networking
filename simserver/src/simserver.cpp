#include "simserver/simserver.h"
#include "simcommon/protocol.h"
#include "simcommon/script.h"
#include "simcommon/simulation.h"
#include "utils/circularbuffer.h"
#include "utils/serialize.h"
#include "utils/time.h"
#include <algorithm>
#include <assert.h>

//
// script networking
//

struct scriptdelta_t {
  scriptid_t m_id;
  scriptguid_t m_guid;
  uint8_t m_lifetime_flags : 2;
  // array of varialbes that changed (id, affected range for arrays)
  // array of indices of changed states
  // array of indiced of executed actions
};

static bool create_script_delta(scriptdelta_t *delta,
                                const entityscript_t &prev,
                                const entityscript_t &curr) {
  bool has_changes = false;
  delta->m_lifetime_flags = 0;
  if(prev.m_guid == 0 && curr.m_guid != 0) {
    delta->m_lifetime_flags = 0x1; // create
    delta->m_id = curr.m_id;
    delta->m_guid = curr.m_guid;
    has_changes = true;
  } else if(prev.m_guid != 0 && curr.m_guid == 0) {
    delta->m_lifetime_flags = 0x2; // destroy
    delta->m_id = prev.m_id;
    delta->m_guid = prev.m_guid;
    has_changes = true;
  } else if(prev.m_guid != 0 && curr.m_guid != 0) {
    delta->m_id = prev.m_id;
    delta->m_guid = prev.m_guid;

    // check for script changes here (and during creation and destruction)
    has_changes = false; // todo(kstasik): for now creation and destruction are
                         // considered as changes
  } else {
    // no script running
  }
  return has_changes;
}

struct entitydelta_t {
  scriptdelta_t m_scripts[SIMSERVER_SCRIPT_CAPACITY];
  // array of owner variables that changed
};

static bool create_entity_delta(entitydelta_t *delta,
                                const entityscriptmgr_t &prev,
                                const entityscriptmgr_t &curr,
                                frameid_t frame) {
  bool has_changes = false;
  for(uint32_t i = 0; i < SIMSERVER_SCRIPT_CAPACITY; ++i) {
    has_changes |= create_script_delta(&delta->m_scripts[i], prev.m_script[i],
                                       curr.m_script[i]);
  }
  return has_changes;
}

struct scriptpacket_t {
  static const uint32_t MAX_PAYLOAD = 128;
  bool m_local;
  frameid_t m_start; // if 0, this is a full update
  frameid_t m_end;
  // based on union of entitydelta_t (start, end)
  uint8_t m_payload[MAX_PAYLOAD];
  uint32_t m_nbytes;
};

static void create_full_packet(scriptpacket_t *packet, frameid_t frame,
                               const entityscriptmgr_t &entity, bool is_local) {
  // todo(kstasik): unify packet writing between 2 functions

  packet->m_local = is_local;
  packet->m_start = 0;
  packet->m_end = frame;

  BitWriter writer(packet->m_payload, scriptpacket_t::MAX_PAYLOAD);
  writer.WriteBits(is_local ? 1 : 0, 1);
  writer.WriteBits(packet->m_start, 32);
  writer.WriteBits(packet->m_end, 32);

  uint32_t changed_script_count =
      SIMSERVER_SCRIPT_CAPACITY; // todo(kstasik): send only running scripts
  writer.WriteBits(changed_script_count,
                   GetRequiredBits<SIMSERVER_SCRIPT_CAPACITY>::Value);

  for(uint32_t i = 0; i < SIMSERVER_SCRIPT_CAPACITY; ++i) {
    const entityscript_t &script = entity.m_script[i];
    if(script.m_id != 0) {
      writer.WriteBits(script_create_flag, 2); // create bit
      writer.WriteBits(script.m_id, 32);
      writer.WriteBits(script.m_guid, 32);
    }
  }

  packet->m_nbytes = writer.Flush();
}

static uint32_t num_scripts_changed(const entitydelta_t &changes) {
  return std::count_if(std::begin(changes.m_scripts),
                       std::end(changes.m_scripts),
                       [](const scriptdelta_t &s) { return s.m_id != 0; });
}

static void create_update_packet(scriptpacket_t *packet, frameid_t start,
                                 frameid_t end, const entityscriptmgr_t &entity,
                                 const entitydelta_t &changes, bool is_local) {
  packet->m_local = is_local;
  packet->m_start = start;
  packet->m_end = end;

  BitWriter writer(packet->m_payload, scriptpacket_t::MAX_PAYLOAD);
  writer.WriteBits(is_local ? 1 : 0, 1);
  writer.WriteBits(packet->m_start, 32);
  writer.WriteBits(packet->m_end, 32);

  uint32_t changed_script_count = num_scripts_changed(changes);
  writer.WriteBits(changed_script_count,
                   GetRequiredBits<SIMSERVER_SCRIPT_CAPACITY>::Value);

  for(uint32_t i = 0; i < SIMSERVER_SCRIPT_CAPACITY; ++i) {
    const scriptdelta_t &script_changes = changes.m_scripts[i];
    if(script_changes.m_id != 0) {
      writer.WriteBits(script_changes.m_lifetime_flags, 2);
      if((script_changes.m_lifetime_flags & script_create_flag) != 0) {
        writer.WriteBits(script_changes.m_id, 32);
        writer.WriteBits(script_changes.m_guid, 32);
        printf("server script created: %x %x (index:%d)\n", script_changes.m_id,
               script_changes.m_guid, i);
      }

      if((script_changes.m_lifetime_flags & script_destroy_flag) != 0) {
        writer.WriteBits(script_changes.m_id, 32);
        writer.WriteBits(script_changes.m_guid, 32);
        printf("server script destroyed: %x %x (index:%d)\n",
               script_changes.m_id, script_changes.m_guid, i);
      }
    }
  }

  packet->m_nbytes = writer.Flush();
}

struct scriptghost_t {
  static const uint32_t SIMSERVER_OUTSTANDING_CAPACITY = 32;

  simpeer_t *m_peer;
  frameid_t m_last_acked;
  frameid_t m_last_sent;

  // todo(kstasik): share script packets between ghosts to avoid serializing the
  // same data multiple times as usually the same data needs to be sent to all
  // ghosts
  // todo(kstasik): support packet drop (nacked packets)
  scriptpacket_t m_outstanding[SIMSERVER_OUTSTANDING_CAPACITY];
  uint32_t m_packets[SIMSERVER_OUTSTANDING_CAPACITY];
};

static scriptpacket_t *acquire_packet(scriptghost_t *ghost,
                                      uint16_t sequence_id) {
  for(uint32_t i = 0; i < scriptghost_t::SIMSERVER_OUTSTANDING_CAPACITY; ++i) {
    if(ghost->m_packets[i] == -1) {
      ghost->m_packets[i] = sequence_id;
      return &ghost->m_outstanding[i];
    }
  }
  return nullptr;
}

void ack_packet(scriptghost_t *ghost, uint16_t sequence_id) {
  for(uint32_t i = 0; i < scriptghost_t::SIMSERVER_OUTSTANDING_CAPACITY; ++i) {
    if(ghost->m_packets[i] == sequence_id) {
      ghost->m_packets[i] = -1;
      ghost->m_outstanding[i] = {};
    }
  }
}

struct entitydeltalog_t {
  frameid_t m_frame;
  entitydelta_t m_delta;
};

static bool create_entity_delta(entitydeltalog_t *delta,
                                const entityscriptmgr_t &prev,
                                const entityscriptmgr_t &curr,
                                frameid_t frame) {
  delta->m_frame = frame;
  return create_entity_delta(&delta->m_delta, prev, curr, frame);
}

struct syncmgr_t {
  syncmgr_t() : m_deltas(128) {}
  scriptghost_t m_endpoint[SIMSERVER_PEER_CAPACITY];
  // todo(kstasik): remove when all clients ack old deltas
  CircularBuffer<entitydeltalog_t> m_deltas;

  void Reset() { m_deltas.Clear(); }
};

static uint32_t find_ghost(syncmgr_t *mgr, simpeer_t *peer) {
  if(!peer)
    return -1;
  for(uint32_t i = 0; i < SIMSERVER_PEER_CAPACITY; ++i) {
    if(mgr->m_endpoint[i].m_peer == peer)
      return i;
  }
  return -1;
}

static uint32_t add_ghost(syncmgr_t *mgr, simpeer_t *peer) {
  assert(peer != nullptr);
  for(uint32_t i = 0; i < SIMSERVER_PEER_CAPACITY; ++i) {
    auto &endpoint = mgr->m_endpoint[i];
    if(endpoint.m_peer == nullptr) {
      endpoint.m_peer = peer;
      endpoint.m_last_acked = 0;
      for(uint32_t i = 0; i < scriptghost_t::SIMSERVER_OUTSTANDING_CAPACITY;
          ++i)
        endpoint.m_packets[i] = -1;
      return i;
    }
  }
  return -1;
}

static void remove_ghost(syncmgr_t *mgr, simpeer_t *peer) {
  assert(peer != nullptr);
  for(uint32_t i = 0; i < SIMSERVER_PEER_CAPACITY; ++i) {
    auto &endpoint = mgr->m_endpoint[i];
    if(endpoint.m_peer == peer) {
      endpoint.m_peer = nullptr;
    }
  }
}

static void append_delta(entitydelta_t *output, const entitydelta_t &delta) {
  for(uint32_t i = 0; i < SIMSERVER_SCRIPT_CAPACITY; ++i) {
    assert(output->m_scripts[i].m_guid == 0 ||
           output->m_scripts[i].m_guid == delta.m_scripts[i].m_guid);
    output->m_scripts[i].m_id = delta.m_scripts[i].m_id;
    output->m_scripts[i].m_guid = delta.m_scripts[i].m_guid;
    output->m_scripts[i].m_lifetime_flags |=
        delta.m_scripts[i].m_lifetime_flags;
  }
}

// returns id of the first update frame
static void create_delta(entitydelta_t *output, const syncmgr_t &sync,
                         frameid_t after, frameid_t to) {
  for(uint32_t i = 0; i < sync.m_deltas.Size(); ++i) {
    const entitydeltalog_t *delta = sync.m_deltas.Begin() + i;
    if(delta->m_frame > after && delta->m_frame <= to) {
      append_delta(output, delta->m_delta);
    }
  }
}

//
// simulation
//

struct serversim_t {
  frameid_t m_head;

  entityid_t m_entity_id[SIMSERVER_ENTITY_CAPACITY];
  entitymovement_t m_entity_movement[SIMSERVER_ENTITY_CAPACITY];
  entityscriptmgr_t m_entity_script[SIMSERVER_ENTITY_CAPACITY];
  syncmgr_t m_sync[SIMSERVER_ENTITY_CAPACITY];
  uint32_t m_entity_count;

  entityid_t m_remote[SIMSERVER_PEER_CAPACITY];
  simcmd_t m_prev_cmd[SIMSERVER_PEER_CAPACITY];
};

struct commandframe_t {
  frameid_t frame;
  simcmd_t cmd;
};

struct peerdata_t {
  peerdata_t() : command_buffer(128), buffer_size_log(128) {}

  void Reset() {
    command_buffer.Clear();
    buffer_size_log.Clear();
  }
  entityid_t remote_entity = 0;
  uint32_t entity_write_index = 0;
  CircularBuffer<commandframe_t> command_buffer;
  CircularBuffer<float> buffer_size_log;
};

struct ss_simulation {
  ss_config config;

  serversim_t *simulation;
  uint64_t start_time;
  uint64_t last_update_time;
  uint64_t time_acc;

  simpeer_t *peer_id[SIMSERVER_PEER_CAPACITY];
  peerdata_t peer_data[SIMSERVER_PEER_CAPACITY];
};

static uint32_t peer_count(ss_simulation *sim) {
  uint32_t count = 0;
  for(int i = 0; i < SIMSERVER_PEER_CAPACITY; ++i) {
    if(sim->peer_id[i] != nullptr)
      ++count;
  }
  return count;
}

static bool find_frame_cmd(peerdata_t &info, frameid_t frame,
                           simcmd_t *result) {
  for(auto *it = info.command_buffer.Begin(); it != info.command_buffer.End();
      ++it) {
    if(it->frame == frame) {
      *result = it->cmd;
      return true;
    }
  }

  // of the input is not found duplicate the last available
  // input
  if(info.command_buffer.Size() > 0) {
    *result = (info.command_buffer.End() - 1)->cmd;
  }
  return false;
}

static uint32_t collect_cmds(ss_simulation *sim, frameid_t frame,
                             entityid_t entities[SIMSERVER_PEER_CAPACITY],
                             simcmd_t cmds[SIMSERVER_PEER_CAPACITY]) {
  uint32_t num_entities = 0;
  for(uint32_t i = 0; i < SIMSERVER_PEER_CAPACITY; ++i) {
    peerdata_t &info = sim->peer_data[i];
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
    peer.buffer_size_log.PushBack((float)peer.command_buffer.Size());

    while(peer.command_buffer.Size() > 0) {
      auto *input = peer.command_buffer.Begin();
      if(input->frame <= sim->simulation->m_head) {
        peer.command_buffer.PopFront(1);
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
  assert(sim->m_head != 0);

  static entityid_t entity_generator = 0;

  // generate entity for a joining peer
  entityid_t id = 0;
  while(id == 0) {
    id = ++entity_generator;
  }

  uint32_t i = sim->m_entity_count;

  sim->m_entity_id[i] = id;
  sim->m_entity_movement[i] = {};
  sim->m_entity_script[i] = {};

  sim->m_sync[i].Reset();
  // add entity creation delta
  entitydeltalog_t delta = {sim->m_head};
  sim->m_sync[i].m_deltas.PushBack(delta);

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
  sim->m_entity_script[entity_idx] = {};
  --sim->m_entity_count;

  // todo(kstasik): this leaves gaps when entities are removed.
  // it needs to be addressed
}

static entityid_t add_remote_entity(ss_simulation *sim) {
  entityid_t id = add_entity(sim->simulation);
  for(uint32_t i = 0; i < SIMSERVER_PEER_CAPACITY; ++i) {
    if(sim->simulation->m_remote[i] != 0) {
      sim->simulation->m_remote[i] = id;
      sim->simulation->m_prev_cmd[i] = {};
      break;
    }
  }

  // add all ghosts to the newly added entity
  uint32_t entity_idx = find_entity(sim->simulation, id);
  if(entity_idx != -1) {
    for(uint32_t i = 0; i < SIMSERVER_PEER_CAPACITY; ++i) {
      if(sim->peer_id[i] != nullptr) {
        add_ghost(&sim->simulation->m_sync[entity_idx], sim->peer_id[i]);
      }
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
  if(sim->simulation) {
    // add ghosts to all existing entities
    for(uint32_t i = 0; i < SIMSERVER_ENTITY_CAPACITY; ++i) {
      if(sim->simulation->m_entity_id[i] != 0) {
        add_ghost(&sim->simulation->m_sync[i], peer);
      }
    }
  }

  sim->peer_id[index] = peer;
  sim->peer_data[index].Reset();

  entityid_t new_entity = 0;
  if(sim->simulation)
    new_entity = add_remote_entity(sim);
  sim->peer_data[index].remote_entity = new_entity;
}

static void remove_peer(ss_simulation *sim, uint32_t index) {
  simpeer_t *peer = sim->peer_id[index];

  entityid_t entity_id = sim->peer_data[index].remote_entity;
  remove_remote_entity(sim->simulation, entity_id);
  sim->peer_data[index].Reset();

  sim->peer_id[index] = nullptr;

  // remove ghosts from all entities
  for(uint32_t i = 0; i < SIMSERVER_ENTITY_CAPACITY; ++i) {
    if(sim->simulation->m_entity_id[i] != 0) {
      remove_ghost(&sim->simulation->m_sync[i], peer);
    }
  }
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

  frameid_t frame = (sim->m_head += 1);

  uint32_t remote_idx = 0;
  while(entities[remote_idx] != 0) {
    uint32_t idx = find_entity(sim, entities[remote_idx]);
    if(idx != -1) {
      entitymovement_t previous = sim->m_entity_movement[idx];
      step_movement(sim->m_entity_movement[idx], previous, inputs[remote_idx]);

      entityscriptmgr_t prev_script = sim->m_entity_script[idx];
      step_script(sim->m_entity_script[idx], prev_script, inputs[remote_idx]);

      entitydeltalog_t delta;
      if(create_entity_delta(&delta, prev_script, sim->m_entity_script[idx],
                             frame)) {
        sim->m_sync[idx].m_deltas.PushBack(delta);
      }
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
  config->frame_duration = 1000 * 1000;
  config->start_frame = 1;
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
  sim->simulation->m_head = sim->config.start_frame;

  sim->start_time = get_time_us();
  sim->last_update_time = sim->start_time;
  sim->time_acc = 0;

  for(uint32_t i = 0; i < SIMSERVER_PEER_CAPACITY; ++i) {
    if(sim->peer_id[i] != nullptr) {
      entityid_t new_entity = add_remote_entity(sim);
      sim->peer_data[i].remote_entity = new_entity;
    }
  }

  return 0;
}

void simserver_stop(ss_simulation *sim) {
  if(!sim->simulation)
    return;

  delete sim->simulation;
  sim->simulation = nullptr;
}

uint32_t simserver_write(uint16_t id, void *buffer, uint32_t nbytes,
                         simpeer_t *dest_peer, ss_simulation *sim) {

  assert(sizeof(UpdateMessage) <= nbytes);
  auto *msg = (UpdateMessage *)buffer;
  msg->m_type = MessageType::Update;

  if(!sim->simulation) {
    msg->m_frame_id = 0;
    msg->m_start_time = 0;
    msg->m_frame_duration = 0;
    msg->m_start_frame = 0;
    msg->m_cmdbuffer_size = 0;
    msg->m_entity_movement_count = 0;
    msg->m_script_update_count = 0;
    return sizeof(UpdateMessage);
  }

  uint32_t msg_size = sizeof(UpdateMessage);

  msg->m_frame_id = sim->simulation->m_head;
  msg->m_start_time = sim->start_time;
  msg->m_frame_duration = sim->config.frame_duration;
  msg->m_start_frame = sim->config.start_frame;
  uint32_t peer_idx = find_peer(sim, dest_peer);
  assert(peer_idx != -1);
  peerdata_t &peer_data = sim->peer_data[peer_idx];

  msg->m_cmdbuffer_size = peer_data.command_buffer.Size();
  msg->m_local_entity = peer_data.remote_entity;

  // write movement update
  const uint32_t movement_update_count = sim->simulation->m_entity_count;
  msg->m_entity_movement_count = movement_update_count;
  for(uint32_t eidx = 0; eidx < msg->m_entity_movement_count; ++eidx) {
    auto *movement_update =
        (MovementUpdate *)((uint8_t *)buffer + sizeof(UpdateMessage) +
                           eidx * sizeof(MovementUpdate));
    movement_update->m_id = sim->simulation->m_entity_id[eidx];
    movement_update->m_movement = sim->simulation->m_entity_movement[eidx];
  }
  msg_size += movement_update_count + sizeof(MovementUpdate);

  // write script update
  const uint32_t entity_packet_capacity =
      sim->simulation->m_entity_count > UpdateMessage::MAX_ENTITY_COUNT
          ? UpdateMessage::MAX_ENTITY_COUNT
          : sim->simulation->m_entity_count;

  uint32_t start_index = peer_data.entity_write_index;
  uint32_t eidx = peer_data.entity_write_index;
  uint32_t num_entities_written = 0;

  auto *script_update =
      (ScriptUpdate *)((uint8_t *)buffer + sizeof(UpdateMessage) +
                       movement_update_count * sizeof(MovementUpdate));
  uint8_t *payload_buffer = (uint8_t *)script_update + sizeof(ScriptUpdate);
  bool sent = false;
  do {
    entityid_t entity_id = sim->simulation->m_entity_id[eidx];
    if(entity_id != 0) {

      auto &script_mgr = sim->simulation->m_entity_script[eidx];
      auto &script_sync = sim->simulation->m_sync[eidx];

      uint32_t ghost_idx = find_ghost(&script_sync, dest_peer);
      assert(ghost_idx != -1);
      auto &ghost = script_sync.m_endpoint[ghost_idx];

      assert(script_sync.m_deltas.Size() > 0); // is it ok to have empty list of
                                               // deltas? shouldn't we create a
                                               // delta when the entity is first
                                               // created?

      frameid_t latest_change = (script_sync.m_deltas.End() - 1)->m_frame;
      frameid_t last_change_sent = ghost.m_last_sent;
      assert(last_change_sent <= latest_change);

      if(latest_change == last_change_sent) {
        // no changes, skipping.
      } else {
        // create and pack a full update or a partial delta
        // todo(kstasik): check if a packet like this already exists.
        scriptpacket_t *packet = acquire_packet(&ghost, id);
        assert(packet); // todo(kstasik): this could happen, we need to
                        // disconnect peer? the outstanding list needs to be big
                        // enough though

        bool is_full_update = last_change_sent == 0;
        if(is_full_update) {
          create_full_packet(packet, latest_change, script_mgr,
                             peer_data.remote_entity == entity_id);
        } else {
          entitydelta_t changes = {};
          create_delta(&changes, script_sync, last_change_sent, latest_change);

          create_update_packet(packet, last_change_sent, latest_change,
                               script_mgr, changes,
                               peer_data.remote_entity == entity_id);
        }
        script_update->m_id = entity_id;
        script_update->m_create = is_full_update;
        script_update->m_destroy = true;
        script_update->m_payload_size = packet->m_nbytes;

        // make sure we don't go over the MTU. this needs to be limited by the
        // number of scriptpacket_t we send per MTU
        assert(payload_buffer + packet->m_nbytes < (uint8_t *)buffer + nbytes);

        memcpy(payload_buffer, packet->m_payload, packet->m_nbytes);

        // this should be done differently. we need to check last acked and
        // the outgoing packets
        ghost.m_last_sent = msg->m_frame_id;

        // move buffer pointers
        msg_size += sizeof(ScriptUpdate) + packet->m_nbytes;
        script_update = (ScriptUpdate *)(payload_buffer + packet->m_nbytes);
        payload_buffer = (uint8_t *)script_update + sizeof(ScriptUpdate);

        num_entities_written++;
      }
    }
    ++eidx;
    if(eidx >= SIMSERVER_ENTITY_CAPACITY)
      eidx = 0;
    peer_data.entity_write_index = eidx;
  } while(num_entities_written < entity_packet_capacity && eidx != start_index);

  msg->m_script_update_count = num_entities_written;
  return msg_size;
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
    if(msg->m_frame_id <= sim->simulation->m_head) {
      return 0; // ignore stale input.
    }

    peerdata_t &peer_data = sim->peer_data[peer_idx];

    bool already_received =
        peer_data.command_buffer.Size() > 0 &&
        msg->m_frame_id <= (*(peer_data.command_buffer.End() - 1)).frame;
    if(already_received) {
      // here, we don't fill in input gaps and only accept the most recent
      // input a better way would be to fill in those gaps and the peer
      // should send multiple frames of input in one packet to reduce the
      // number of lost ocmmand frames.
      return 0;
    }
    simcmd_t cmd = {msg->m_buttons};
    peer_data.command_buffer.PushBack(commandframe_t{msg->m_frame_id, cmd});
    return 0;
  }
  return -2;
}

void simserver_ack(uint16_t id, simpeer_t *peer, ss_simulation *sim) {
  if(!sim->simulation)
    return;

  for(uint32_t eidx = 0; eidx < SIMSERVER_ENTITY_CAPACITY; ++eidx) {
    if(sim->simulation->m_entity_id[eidx] == 0)
      continue;
    auto &sync = sim->simulation->m_sync[eidx];

    uint32_t ghost_idx = find_ghost(&sync, peer);
    assert(ghost_idx != -1);
    auto &ghost = sync.m_endpoint[ghost_idx];
    ack_packet(&ghost, id);
  }
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
          collect_cmds(sim, sim->simulation->m_head + 1, entities, commands);

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
    info->head = sim->simulation->m_head;
    info->peer_count = peer_count(sim);

    for(int i = 0, j = 0; i < SIMSERVER_PEER_CAPACITY; ++i) {
      if(sim->peer_id[i] != nullptr) {
        info->peer_id[j] = sim->peer_id[i];
        info->remote_entity[j] = sim->peer_data[i].remote_entity;
        info->input_buffer_size[j] = sim->peer_data[i].command_buffer.Size();
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

  peerdata_t &peer_data = sim->peer_data[peer_idx];

  if(peer_data.remote_entity != 0) {
    return 0; // peer already owns an entity. for now only one is supported
  }

  peer_data.remote_entity = add_remote_entity(sim);
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

int simserver_entity_script(ss_simulation *sim, ss_script_info *info,
                            entityid_t entity) {
  if(!sim->simulation)
    return -1;

  uint32_t entity_idx = find_entity(sim->simulation, entity);
  if(entity_idx == -1)
    return -2;

  auto &script_mgr = sim->simulation->m_entity_script[entity_idx];
  info->count = 0;
  for(uint32_t i = 0; i < SIMSERVER_SCRIPT_CAPACITY; ++i) {
    auto &script = script_mgr.m_script[i];
    if(script.m_guid != 0) {
      info->guid[info->count] = script.m_guid;
      info->id[info->count] = script.m_id;
      ++info->count;
    }
  }
  return 0;
}