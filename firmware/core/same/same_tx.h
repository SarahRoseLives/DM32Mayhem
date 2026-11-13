// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// SAME (NOAA weather radio / EAS) headers for the TX app: the standard 520.83 baud AFSK burst, built from a few
// fields and handed to the radio's AFSK transmitter (core/aprs). The coding is the public NOAACPG SAME format.
#ifndef SAME_TX_H
#define SAME_TX_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

#if CONFIG_SAME_TX

#define SAME_ORG_COUNT		4
#define SAME_EVENT_COUNT	26
#define SAME_LEVEL_DEFAULT	80	// AFSK audio level: 50 is about 3 kHz deviation, 80 about 5 kHz

typedef struct {
	uint8_t org;		// index into same_org_codes
	uint8_t event;		// index into same_event_codes
	uint8_t fips_state;	// 00..99
	uint16_t fips_county;	// 000..999
	uint8_t dur_h, dur_m;	// valid time, hours and minutes
	char station[9];	// station ID, 8 characters (space padded when sent)
	uint32_t hz;		// transmit frequency
	bool narrow;		// 12.5 kHz (else 25 kHz)
	uint8_t level;		// AFSK audio level 0..100 (50 is about 3 kHz deviation)
} same_cfg_t;

extern const char *const same_org_codes[SAME_ORG_COUNT];
extern const char *const same_event_codes[SAME_EVENT_COUNT];

void same_cfg_default(same_cfg_t *c);
// Build the SAME header (preamble and header, three times as the standard asks) and queue it on the radio's AFSK
// transmitter. True: queued; false: out of a TX band, or a burst is already waiting.
bool same_tx_send(const same_cfg_t *c);

#endif	// CONFIG_SAME_TX
#endif
