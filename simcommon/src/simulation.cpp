#include "simcommon/simulation.h"
#include "simcommon/types.h"

void step_movement(entitymovement_t &output, const entitymovement_t &state,
                   const siminput_t &input) {
  // apply input
  output = state;

  float delta = 10.f;
  if(is_pressed(&input, BUTTON_LEFT) || is_hold(&input, BUTTON_LEFT))
    output.m_pos[0] -= delta;

  if(is_pressed(&input, BUTTON_RIGHT) || is_hold(&input, BUTTON_RIGHT))
    output.m_pos[0] += delta;

  if(is_pressed(&input, BUTTON_DOWN) || is_hold(&input, BUTTON_DOWN))
    output.m_pos[1] -= delta;

  if(is_pressed(&input, BUTTON_UP) || is_hold(&input, BUTTON_UP))
    output.m_pos[1] += delta;
}