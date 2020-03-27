#include "simcommon/types.h"

bool is_pressed(siminput_t *i, uint32_t b) {
  return (i->previous.m_buttons & b) == 0 && (i->current.m_buttons & b) != 0;
}

bool is_released(siminput_t *i, uint32_t b) {
  return (i->previous.m_buttons & b) != 0 && (i->current.m_buttons & b) == 0;
}

bool is_hold(siminput_t *i, uint32_t b) {
  return (i->previous.m_buttons & b) != 0 && (i->current.m_buttons & b) != 0;
}