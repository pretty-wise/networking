#include "simcommon/types.h"

bool is_pressed(const siminput_t *i, uint32_t b) {
  return (i->previous.m_buttons & b) == 0 && (i->current.m_buttons & b) != 0;
}

bool is_released(const siminput_t *i, uint32_t b) {
  return (i->previous.m_buttons & b) != 0 && (i->current.m_buttons & b) == 0;
}

bool is_hold(const siminput_t *i, uint32_t b) {
  return (i->previous.m_buttons & b) != 0 && (i->current.m_buttons & b) != 0;
}