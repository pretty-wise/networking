#pragma once
#include <cstdint>

class siminput_t;

const uint32_t SIMSERVER_ENTITY_CAPACITY = 64;

struct entitymovement_t {
  float m_pos[3];
};

void step_movement(entitymovement_t &output, const entitymovement_t &state,
                   const siminput_t &input);