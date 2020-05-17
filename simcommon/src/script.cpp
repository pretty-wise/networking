#include "simcommon/script.h"
#include <algorithm>

uint32_t script_count(const entityscriptmgr_t *mgr) {
  return std::count_if(std::begin(mgr->m_script), std::end(mgr->m_script),
                       [](const entityscript_t &s) { return s.m_id != 0; });
}

scriptguid_t add_script(entityscriptmgr_t *mgr, scriptid_t id) {
  if(script_count(mgr) >= SIMSERVER_SCRIPT_CAPACITY)
    return 0;
  static scriptguid_t guid = 0;
  while(guid == 0) {
    guid++;
  }

  for(uint32_t i = 0; i < SIMSERVER_SCRIPT_CAPACITY; ++i) {
    if(mgr->m_script[i].m_id == 0) {
      mgr->m_script[i].m_id = id;
      mgr->m_script[i].m_guid = guid;
      break;
    }
  }

  return guid;
}

bool remove_script(entityscriptmgr_t *mgr, scriptguid_t guid) {
  uint32_t idx = find_script(mgr, guid);
  if(idx == -1)
    return false;
  mgr->m_script[idx].m_id = 0;
  mgr->m_script[idx].m_guid = 0;
  return true;
}

uint32_t find_script(entityscriptmgr_t *mgr, scriptguid_t guid) {
  for(uint32_t i = 0; i < SIMSERVER_SCRIPT_CAPACITY; ++i) {
    if(mgr->m_script[i].m_guid == guid)
      return i;
  }
  return -1;
}
uint32_t insert_script(entityscriptmgr_t *mgr, scriptid_t id,
                       scriptguid_t guid) {
  uint32_t free_idx = find_script(mgr, 0);
  if(free_idx == -1)
    return -1;
  mgr->m_script[free_idx].m_id = id;
  mgr->m_script[free_idx].m_guid = guid;
  return free_idx;
}

void step_script(entityscriptmgr_t &output, const entityscriptmgr_t &script,
                 const siminput_t &input) {
  const scriptid_t id = 0xdeaddead;
  static scriptguid_t guid = 0;
  if(is_pressed(&input, BUTTON_A)) {
    if(script_count(&script) == 0) {
      guid = add_script(&output, id);
    } else {
      remove_script(&output, guid);
    }
  }
}