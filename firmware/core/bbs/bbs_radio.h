// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Radio glue for the packet BBS (bbs_radio.h): claims the flash store, points the APRS modem at the BBS channel, and
// starts the application with a transmit path into the APRS RF queue.
#ifndef RFOUNDRY_BBS_RADIO_H
#define RFOUNDRY_BBS_RADIO_H

#include <stdint.h>
#include "bbs/bbs_config.h"

// Call once at radio start-up, after aprs_init()
void bbs_start(uint32_t now_ms);

// Point the APRS modem at the config's channel and take it into the BBS engine (does not tune the VFO or enable it)
void bbs_radio_apply(const bbs_cfg_t *cfg, uint32_t now_ms);
void bbs_radio_set_enabled(bool on, uint32_t now_ms);
// Save to the settings store, then apply
void bbs_radio_save(const bbs_cfg_t *cfg, uint32_t now_ms);

#endif
