#pragma once
#include <cstdint>

typedef uint32_t frameid_t;
struct simpeer_t;

struct siminput_t {
  siminput_t();

  uint32_t m_buttons;
};