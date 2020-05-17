#pragma once
#include <cstdint>

typedef uint32_t frameid_t;
typedef uint32_t entityid_t;
struct simpeer_t;

const uint32_t BUTTON_LEFT = 0x0001;
const uint32_t BUTTON_RIGHT = 0x0002;
const uint32_t BUTTON_UP = 0x0004;
const uint32_t BUTTON_DOWN = 0x0008;
const uint32_t BUTTON_A = 0x0010;
const uint32_t BUTTON_B = 0x0020;

struct simcmd_t {
  uint32_t m_buttons;
};

struct siminput_t {
  simcmd_t current;
  simcmd_t previous;
};

bool is_pressed(const siminput_t *i, uint32_t b);
bool is_released(const siminput_t *i, uint32_t b);
bool is_hold(const siminput_t *i, uint32_t b);

typedef uint32_t scriptid_t;
typedef uint32_t scriptguid_t;

#define SIMSERVER_SCRIPT_CAPACITY 4