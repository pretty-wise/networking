#include "simserver/simserver.h"
#include "simcommon/protocol.h"
#include "simcommon/time.h"
#include <assert.h>

struct serversim_t {
  frameid_t head;
  uint32_t frame_count;

  // map<entityid_t, commandinput_t> prev_input;
};

void step(serversim_t &sim) {
  //
  sim.head += 1;
  sim.frame_count += 1;
}

struct ss_simulation {
  ss_config config;

  serversim_t *simulation;
  uint64_t last_update_time;
  uint64_t time_acc;

  /* todo(kstasik):
    struct PeerData {
      entityid_t owned;
      circular_buffer<pair<frameid_t, commandinput_t>> input_buffer;
    };
    map<simpeer_t, PeerData> m_peers;
  */
};

void simserver_make_default(ss_config *config) {
  config->frame_duration = 1000;
  config->start_frame = 0;
  config->start_time = sim_get_time_us();
}

ss_simulation *simserver_create(ss_config *config) {
  ss_simulation *sim = new ss_simulation{};
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
  assert(nbytes >= sizeof(UpdateMessage));
  auto *msg = (UpdateMessage *)buffer;
  msg->m_type = MessageType::Update;
  if(sim->simulation) {
    msg->m_frame_id = sim->simulation->head;
    msg->m_start_time = sim->config.start_time;
    msg->m_frame_duration = sim->config.frame_duration;
    msg->m_start_frame = sim->config.start_frame;
  } else {
    msg->m_frame_id = 0;
    msg->m_start_time = 0;
    msg->m_frame_duration = 0;
    msg->m_start_frame = 0;
  }

  return sizeof(UpdateMessage);
}

uint32_t simserver_read(uint16_t id, const void *buffer, uint32_t nbytes,
                        simpeer_t *peer, ss_simulation *sim) {
  const auto *base = (const SimulationMessage *)buffer;
  return 0;
}

void simserver_ack(uint16_t id, simpeer_t *peer, ss_simulation *sim) {
  // acks not used for now
}

void simserver_connection(uint32_t state, simpeer_t *peer, ss_simulation *sim) {
  // connections not used for now
}

void simserver_update(ss_simulation *sim) {
  if(sim->simulation) {
    uint64_t now = sim_get_time_us();
    uint64_t dt = now - sim->last_update_time;
    assert(dt >= 0);

    sim->time_acc += dt;
    while(sim->time_acc >= sim->config.frame_duration) {
      step(*sim->simulation);
      sim->time_acc -= sim->config.frame_duration;
    }

    sim->last_update_time = now;
  }
}

int simserver_info(ss_simulation *sim, ss_info *info) {
  info->running = sim->simulation != nullptr;
  if(sim->simulation) {
    info->head = sim->simulation->head;
  }
  return 0;
}