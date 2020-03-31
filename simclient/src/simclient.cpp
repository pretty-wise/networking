#include "simclient/simclient.h"
#include "simcommon/protocol.h"
#include "utils/circularbuffer.h"
#include "utils/time.h"
#include <assert.h>
#include <math.h>

struct clientsim_t {
  frameid_t m_head;

  entityid_t m_entity_id[SIMSERVER_ENTITY_CAPACITY];
  entitymovement_t m_entity_movement[SIMSERVER_ENTITY_CAPACITY];
  uint32_t m_entity_count;
};

void step(clientsim_t *sim, simcmd_t &input) { sim->m_head += 1; }

struct sc_simulation {
  sc_simulation() : m_offset_log(128), m_acceleration_log(128) {}

  clientsim_t *m_simulation = nullptr;

  simcmd_t m_last_input;
  void (*m_input_callback)(simcmd_t *input);

  uint64_t m_last_update_time = 0;
  uint64_t m_frame_duration = 0;
  uint64_t m_remote_sim_time_accumulator = 0;
  frameid_t m_remote_head = 0;
  int64_t m_predition_offset = 0;
  int64_t m_acceleration = 0;
  uint64_t m_local_sim_time_accumulator = 0;
  int64_t m_desired_predition_offset = 0;
  frameid_t m_acked_remote_frame = 0;

  CircularBuffer<float> m_offset_log;
  CircularBuffer<float> m_acceleration_log;
};

static void start_simulation(sc_simulation *sim, uint64_t start_time,
                             uint64_t frame_duration, frameid_t start_frame) {
  assert(frame_duration > 0);

  // todo(kstasik): m_remote_head calculations does not include
  // time synchronisation for now. it needs to be added to accurately deprict
  // remote_head on the client
  uint64_t now = get_time_us();
  uint64_t since_simulation_start = now - start_time;

  sim->m_last_update_time = now;
  sim->m_frame_duration = frame_duration;

  uint64_t integral = (uint64_t)(since_simulation_start / frame_duration);
  sim->m_remote_sim_time_accumulator =
      (uint64_t)fmodf((float)since_simulation_start, (float)frame_duration);
  sim->m_remote_head = start_frame + (frameid_t)integral;

  // todo(kstasik): initialize to something based on synchronized time
  sim->m_predition_offset = 0;
  sim->m_acceleration = 0;

  // for now, hardcoded to 100ms
  sim->m_desired_predition_offset = 100000;

  // todo(kstasik): is this the right value to initialize?
  sim->m_acked_remote_frame = 0;

  sim->m_simulation = new clientsim_t{};
  sim->m_simulation->m_head = sim->m_remote_head;
  sim->m_local_sim_time_accumulator = sim->m_remote_sim_time_accumulator;
}

static void stop_simulation(sc_simulation *sim) {
  delete sim->m_simulation;
  sim->m_simulation = nullptr;

  sim->m_acked_remote_frame = 0;
  sim->m_frame_duration = 0;
  sim->m_last_update_time = 0;
  sim->m_remote_head = 0;
  sim->m_remote_sim_time_accumulator = 0;

  sim->m_predition_offset = 0;
  sim->m_desired_predition_offset = 0;
  sim->m_acceleration = 0;
}

sc_simulation *simclient_create(sc_config *config) {
  if(!config || !config->input_callback)
    return nullptr;

  sc_simulation *sim = new sc_simulation{};
  sim->m_input_callback = config->input_callback;
  return sim;
}

void simclient_destroy(sc_simulation *sim) { delete sim; }

uint32_t simclient_write(sc_simulation *sim, uint16_t id, void *buffer,
                         uint32_t nbytes) {
  if(!sim)
    return 0;

  assert(sizeof(CommandMessage) <= nbytes);

  auto *msg = (CommandMessage *)buffer;
  msg->m_type = MessageType::Command;
  if(sim->m_simulation) {
    msg->m_frame_id = sim->m_simulation->m_head;
    msg->m_buttons = sim->m_last_input.m_buttons;
  }
  return sizeof(CommandMessage);
}

uint32_t simclient_read(sc_simulation *sim, uint16_t id, const void *buffer,
                        uint32_t nbytes) {
  if(!sim)
    return 0;

  const auto *base = (const SimulationMessage *)buffer;
  if(base->m_type == MessageType::Update) {
    const auto *msg = (const UpdateMessage *)buffer;

    if(msg->m_frame_duration > 0) {
      if(!sim->m_simulation) {
        start_simulation(sim, msg->m_start_time, msg->m_frame_duration,
                         msg->m_start_frame);
      }
      if(msg->m_frame_id > sim->m_acked_remote_frame) {
        sim->m_acked_remote_frame = msg->m_frame_id;

        // read entity movement
        sim->m_simulation->m_entity_count = msg->m_movement_count;
        for(uint32_t i = 0; i < msg->m_movement_count; ++i) {
          sim->m_simulation->m_entity_id[i] = msg->m_entities[i];
          sim->m_simulation->m_entity_movement[i] = msg->m_movement[i];
        }

        // adjustment of prediction offset based on server's feedback.
        // this is a very simple implementation that can be easily improved.
        // if server reports 0 cmds we extend prediction offset by delta.
        // if server reports > 1 cmds we shrink prediction offset by delta.
        const uint32_t delta = 1000;
        if(msg->m_cmdbuffer_size == 0) {
          sim->m_desired_predition_offset += delta;
        } else if(msg->m_cmdbuffer_size > 1) {
          sim->m_desired_predition_offset -= delta;
        }
      } // else ignore out of order and duplicate packets
    } else {
      stop_simulation(sim);
    }
    return 0;
  }

  return -1;
}

void simclient_ack(sc_simulation *sim, uint16_t id) {
  if(!sim)
    return;
}

void simclient_update(sc_simulation *sim) {
  if(!sim)
    return;

  if(sim->m_simulation) {
    uint64_t now = get_time_us();
    uint64_t dt = now - sim->m_last_update_time;

    // update server simulation frame
    {
      sim->m_remote_sim_time_accumulator += dt;

      uint64_t integral = (uint64_t)(sim->m_remote_sim_time_accumulator /
                                     sim->m_frame_duration);
      sim->m_remote_sim_time_accumulator =
          (uint64_t)fmodf((float)sim->m_remote_sim_time_accumulator,
                          (float)sim->m_frame_duration);
      sim->m_remote_head += (frameid_t)integral;

      // the code above will accumulate error
      // the other solution is:
      // sim->m_remote_head = m_startFrameId + (now -
      // m_simulationStartTimeSec) / sim->m_frame_duration;
    }

    // update offset and acceleration
    {
      int64_t delta = 1000; // todo(kstasik): what is the best delta?
      int64_t diff = sim->m_desired_predition_offset - sim->m_predition_offset;
      if(abs(diff) < delta) {
        sim->m_acceleration = 0;
      } else {
        sim->m_acceleration = -delta * (diff > 0 ? 1 : -1);
      }
    }

    // step local simulation
    {
      sim->m_input_callback(&sim->m_last_input);

      sim->m_local_sim_time_accumulator += dt;

      while(true) {
        uint64_t frame_duration = sim->m_frame_duration + sim->m_acceleration;
        assert((int64_t)sim->m_frame_duration + sim->m_acceleration > 0);

        if(sim->m_local_sim_time_accumulator < frame_duration)
          break;

        step(sim->m_simulation, sim->m_last_input);

        sim->m_offset_log.PushBack((float)sim->m_predition_offset);
        sim->m_acceleration_log.PushBack((float)sim->m_acceleration);

        sim->m_local_sim_time_accumulator -= frame_duration;
        sim->m_predition_offset -= sim->m_acceleration;
      }
    }

    sim->m_last_update_time = now;
  }
}

int simclient_info(sc_simulation *sim, sc_info *info) {
  if(!sim || !info)
    return -1;

  info->running = sim->m_simulation != nullptr;

  if(sim->m_simulation) {
    info->local_head = sim->m_simulation->m_head;
    info->remote_head = sim->m_remote_head;
    info->acked_frame = sim->m_acked_remote_frame;

    info->prediction_offset = sim->m_predition_offset;
    info->desired_offset = sim->m_desired_predition_offset;
    info->prediction_acceleration = sim->m_acceleration;

    assert(sim->m_offset_log.Size() == sim->m_acceleration_log.Size());
    info->acceleration_log = sim->m_acceleration_log.Begin();
    info->offset_log = sim->m_offset_log.Begin();
    info->log_size = sim->m_offset_log.Size();
  }
  return 0;
}

int simclient_entity_movement(sc_simulation *sim, entityid_t **ids,
                              entitymovement_t **data, uint32_t *count) {
  *count = 0;
  if(!sim->m_simulation)
    return -1;

  *count = sim->m_simulation->m_entity_count;
  if(*count > 0) {
    *ids = sim->m_simulation->m_entity_id;
    *data = sim->m_simulation->m_entity_movement;
  }
  return 0;
}
