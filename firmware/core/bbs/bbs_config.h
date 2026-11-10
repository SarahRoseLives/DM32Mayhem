// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// BBS configuration (bbs_config.h): the station identity, the operating channel and the options the radio's own
// panel sets. No settings store, no RTOS here, so the host tests can build it; bbs_settings.c loads and saves it.
#ifndef RFOUNDRY_BBS_CONFIG_H
#define RFOUNDRY_BBS_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define BBS_CALL_MAX		6	// AX.25 callsign characters without the SSID
#define BBS_NAME_MAX		16	// the BBS name in the banner
#define BBS_SYSOP_MAX		6
#define BBS_CALL_DEFAULT	"N0CALL"
#define BBS_SSID_DEFAULT	0
#define BBS_NAME_DEFAULT	"DM32BBS"
#define BBS_SYSOP_DEFAULT	"N0CALL"
#define BBS_FREQ_DEFAULT	145100000u
#define BBS_TXDELAY_DEFAULT	30	// KISS TXDELAY, 10 ms units: 300 ms

// Bandwidth codes (the radio's radio_bw_t lives in radio.h; this keeps the config host-buildable)
enum { BBS_BW_12K5 = 0, BBS_BW_25K = 1 };

typedef struct {
	char call[BBS_CALL_MAX + 1];	// base callsign, upper case, NUL terminated
	char name[BBS_NAME_MAX + 1];
	char sysop[BBS_SYSOP_MAX + 1];
	uint32_t freq_hz;
	uint8_t ssid;			// 0..15
	uint8_t bw;			// BBS_BW_*
	uint8_t txdelay;		// 10 ms units, 0 = the radio's default
	uint8_t cwid;			// CW ID interval code: 0 off, 1 5 min, 2 10, 3 15, 4 30
	bool on_at_boot;
} bbs_cfg_t;

void bbs_cfg_defaults(bbs_cfg_t *c);
// Upper-case the callsigns and terminate the strings. Safe on any value
void bbs_cfg_normalize(bbs_cfg_t *c);
// 1..6 letters/digits in the call, SSID 0..15, a non-zero frequency, the strings terminated
bool bbs_cfg_valid(const bbs_cfg_t *c);
// "N0CALL-1" ("N0CALL" for SSID 0). The length written, 0 when max is too small
size_t bbs_cfg_format_call(const bbs_cfg_t *c, char *out, size_t max);

#endif
