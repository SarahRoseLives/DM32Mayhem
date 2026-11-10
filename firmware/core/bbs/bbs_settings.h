// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The BBS configuration in the settings store (bbs_settings.h): append-only settings_t fields, zero meaning the defaults.
#ifndef RFOUNDRY_BBS_SETTINGS_H
#define RFOUNDRY_BBS_SETTINGS_H

#include "bbs/bbs_config.h"

#define BBS_SET_STORED	0x01u	// the bbs_ fields were written
#define BBS_SET_BOOT	0x02u	// start the BBS at power-on

void bbs_settings_load(bbs_cfg_t *c);
void bbs_settings_save(const bbs_cfg_t *c, uint32_t now_ms);

#endif
