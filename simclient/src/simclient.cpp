#include "simclient/simclient.h"
#include "simcommon/protocol.h"
#include "utils/circularbuffer.h"
#include "utils/time.h"
#include <assert.h>
#include <cstring>
#include <math.h>

struct entitydata_t {
  frameid_t m_current;
  frameid_t m_predicted;
  frameid_t m_last_error;
  entitymovement_t m_movement;
};

struct clientsim_t {
  // the id of the last locally predicted frame.
  frameid_t m_local_head;

  entityid_t m_local_entity;

  entityid_t m_entity_id[SIMSERVER_ENTITY_CAPACITY];
  entitydata_t m_entity_data[SIMSERVER_ENTITY_CAPACITY];
  uint32_t m_entity_count;
};

static uint32_t find_entity(clientsim_t *sim, entityid_t id) {
  if(id == 0)
    return -1;
  for(uint32_t i = 0; i < SIMSERVER_ENTITY_CAPACITY; ++i) {
    if(sim->m_entity_id[i] == id)
      return i;
  }
  return -1;
}

static uint32_t add_entity(clientsim_t *sim, entityid_t id) {
  assert(id != 0);

  for(uint32_t i = 0; i < SIMSERVER_ENTITY_CAPACITY; ++i) {
    if(sim->m_entity_id[i] == 0) {
      sim->m_entity_id[i] = id;
      sim->m_entity_data[i] = {};
      ++sim->m_entity_count;
      return i;
    }
  }
  return -1;
}

void step_client_simulation(clientsim_t *sim, siminput_t &input) {
  frameid_t frame = (sim->m_local_head += 1);

  uint32_t entity_idx = find_entity(sim, sim->m_local_entity);
  if(entity_idx != -1) {
    auto &entity_data = sim->m_entity_data[entity_idx];
    assert(entity_data.m_predicted == frame - 1);
    entitymovement_t prev_movement = entity_data.m_movement;
    step_movement(entity_data.m_movement, prev_movement, input);
    entity_data.m_predicted = frame;
  }
}

struct sc_simulation {
  sc_simulation()
      : m_offset_log(128), m_acceleration_log(128), m_cmd_log(128) {}

  clientsim_t *m_simulation = nullptr;

  CircularBuffer<simcmd_t> m_cmd_log; // last entry is of sim->m_local_head
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
  sim->m_simulation->m_local_head = sim->m_remote_head;
  // fill the buffer with empty commands as if nothing was pressed to simplify
  // the algorithm
  for(uint32_t i = 0; i < 128; ++i)
    sim->m_cmd_log.PushBack(simcmd_t{});
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
    msg->m_frame_id = sim->m_simulation->m_local_head;
    assert(sim->m_cmd_log.Size() > 0);
    simcmd_t last_cmd = *(sim->m_cmd_log.End() - 1);
    msg->m_buttons = last_cmd.m_buttons;
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

        sim->m_simulation->m_local_entity = msg->m_local_entity;

        // read entities
        for(uint32_t i = 0; i < msg->m_entity_count; ++i) {
          auto *entity_update =
              (EntityUpdate *)((uint8_t *)buffer + sizeof(UpdateMessage) +
                               i * sizeof(EntityUpdate));

          uint32_t eidx = find_entity(sim->m_simulation, entity_update->m_id);
          if(eidx == -1) {
            eidx = add_entity(sim->m_simulation, entity_update->m_id);
          }

          assert(eidx != -1); // entity limit reached. this could happen if we
                              // have stale entities that we were not notified
                              // about destruction of just yet. this case needs
                              // to be supported.

          if(eidx == -1)
            return 0;

          auto &entity_data = sim->m_simulation->m_entity_data[eidx];

          if(sim->m_simulation->m_entity_id[eidx] ==
             sim->m_simulation->m_local_entity) {
            //
            // for local entity for now we apply the local state and resimulate
            // to the predicted frame
            //
            entitymovement_t predicted = entity_data.m_movement;

            entity_data.m_movement = entity_update->m_movement;
            entity_data.m_current = msg->m_frame_id;
            entity_data.m_predicted = sim->m_simulation->m_local_head;

            uint32_t frames_to_simulate =
                sim->m_simulation->m_local_head - msg->m_frame_id;
            assert(sim->m_cmd_log.Size() > frames_to_simulate);

            for(simcmd_t *it = sim->m_cmd_log.End() - frames_to_simulate;
                it < sim->m_cmd_log.End(); ++it) {
              entitymovement_t prev_movement = entity_data.m_movement;
              siminput_t input = {*it, *(it - 1)};
              step_movement(entity_data.m_movement, prev_movement, input);
            }

            if(0 != memcmp(&predicted, &entity_data.m_movement,
                           sizeof(entitymovement_t))) {
              entity_data.m_last_error = msg->m_frame_id;
            }
          } else {
            //
            // for remote entities simply apply the received values
            //
            entity_data.m_current = msg->m_frame_id;
            entity_data.m_predicted = msg->m_frame_id;
            entity_data.m_movement = entity_update->m_movement;
          }
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
      simcmd_t cmd;
      sim->m_input_callback(&cmd);

      sim->m_local_sim_time_accumulator += dt;

      while(true) {
        uint64_t frame_duration = sim->m_frame_duration + sim->m_acceleration;
        assert((int64_t)sim->m_frame_duration + sim->m_acceleration > 0);

        if(sim->m_local_sim_time_accumulator < frame_duration)
          break;

        assert(sim->m_cmd_log.Size() > 0);

        siminput_t input = {cmd, *(sim->m_cmd_log.End() - 1)};
        step_client_simulation(sim->m_simulation, input);
        sim->m_cmd_log.PushBack(cmd);

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
    info->local_head = sim->m_simulation->m_local_head;
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
                              entityinfo_t **data, uint32_t *count) {
  *count = 0;
  if(!sim->m_simulation)
    return -1;

  // this is a temporary solution to only return movement data.
  // the api needs to be improved and probably returned more details than just
  // movement
  static entityinfo_t transfer_buffer[SIMSERVER_ENTITY_CAPACITY];

  *count = sim->m_simulation->m_entity_count;
  if(*count > 0) {
    *ids = sim->m_simulation->m_entity_id;
    for(uint32_t i = 0; i < sim->m_simulation->m_entity_count; ++i) {
      auto &entity_data = sim->m_simulation->m_entity_data[i];
      transfer_buffer[i].movement = entity_data.m_movement;
      transfer_buffer[i].confirmed = entity_data.m_current;
      transfer_buffer[i].predicted = entity_data.m_predicted;
      transfer_buffer[i].last_error = entity_data.m_last_error;
    }
    *data = transfer_buffer;
  }
  return 0;
}
