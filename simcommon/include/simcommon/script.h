#pragma once
#include "simcommon/types.h"

//
// script
//

const uint8_t script_create_flag = 0x1;
const uint8_t script_destroy_flag = 0x2;

struct entityscript_t {
  scriptid_t m_id; // reference to static data.
  scriptguid_t m_guid;
};

struct entityscriptmgr_t {
  entityscript_t m_script[SIMSERVER_SCRIPT_CAPACITY];
};

uint32_t script_count(const entityscriptmgr_t *mgr);
scriptguid_t add_script(entityscriptmgr_t *mgr, scriptid_t id);
bool remove_script(entityscriptmgr_t *mgr, scriptguid_t guid);
uint32_t find_script(entityscriptmgr_t *mgr, scriptguid_t guid);
uint32_t insert_script(entityscriptmgr_t *mgr, scriptid_t id,
                       scriptguid_t guid);

void step_script(entityscriptmgr_t &output, const entityscriptmgr_t &script,
                 const siminput_t &input);