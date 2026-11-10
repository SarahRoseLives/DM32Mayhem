// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// SAME header transmitter (same_tx.h). The format is the public NOAA SAME/EAS coding: "ZCZC-ORG-EEE-PSSCCC+TTTT-
// JJJHHMM-LLLLLLLL-", each byte sent LSB first, three times back to back behind a 16-byte 0xAB preamble, as AFSK at
// 520.83 baud (mark 2083 Hz, space 1562.5 Hz). The radio's existing APRS AFSK modulator sends the bits; the RF path
// is the same one APRS uses (key, tones, unkey, tune back).
#include "config.h"
#if CONFIG_SAME_TX
#include <string.h>
#include "same/same_tx.h"
#include "aprs/aprs.h"
#include "board.h"

const char *const same_org_codes[SAME_ORG_COUNT] = { "WXR", "EAS", "CIV", "PEP" };

// The event codes of the SAME standard (NOAA SAME event code list)
const char *const same_event_codes[SAME_EVENT_COUNT] = {
	"RWT", "RMT", "NPT", "NST", "NMT", "EAN", "EAT", "NIC", "ADR", "AVA", "AVW", "BZW", "CFW",
	"CFS", "DSW", "EQW", "EVI", "FFW", "FFS", "FFH", "FRW", "HLS", "HUW", "HUH", "SVR", "TOR",
};

#define SAME_REPEATS	3
#define SAME_PREAMBLE	16	// bytes of 0xAB before each header
#define SAME_BAUD	521u	// 520.83, rounded
#define SAME_MARK_HZ	2083u
#define SAME_SPACE_HZ	1563u
#define SAME_LEVEL_DEFAULT	80	// about 5 kHz deviation at 25 kHz (50 is ~3 kHz)

static char *put_str(char *p, const char *s)
{
	while (*s != '\0') {
		*p++ = *s++;
	}
	return p;
}

static char *put2(char *p, unsigned v)
{
	*p++ = (char)('0' + (v / 10u) % 10u);
	*p++ = (char)('0' + v % 10u);
	return p;
}

static char *put3(char *p, unsigned v)
{
	*p++ = (char)('0' + (v / 100u) % 10u);
	*p++ = (char)('0' + (v / 10u) % 10u);
	*p++ = (char)('0' + v % 10u);
	return p;
}

void same_cfg_default(same_cfg_t *c)
{
	memset(c, 0, sizeof(*c));
	c->org = 0;			// WXR
	c->event = 0;			// RWT
	c->fips_state = 39;
	c->fips_county = 7;
	c->dur_h = 0;
	c->dur_m = 30;
	c->hz = 162550000u;		// NOAA WX-3
	c->narrow = false;
	c->level = SAME_LEVEL_DEFAULT;
}

bool same_tx_send(const same_cfg_t *c)
{
	if (!board_in_band(c->hz, true)) {
		return false;
	}
	char msg[64];
	char *p = msg;
	p = put_str(p, "ZCZC-");
	p = put_str(p, same_org_codes[c->org < SAME_ORG_COUNT ? c->org : 0]);
	*p++ = '-';
	p = put_str(p, same_event_codes[c->event < SAME_EVENT_COUNT ? c->event : 0]);
	*p++ = '-';
	p = put2(p, c->fips_state % 100u);	// PSSCCC: part 0, state, county
	*p++ = '0';
	p = put3(p, c->fips_county % 1000u);
	*p++ = '+';
	p = put2(p, c->dur_h % 100u);
	p = put2(p, c->dur_m % 100u);
	p = put_str(p, "-0010000-");		// issue time (the radio has no usable clock here)
	static const char station[] = "SAMETX";	// 8 characters, space padded
	for (int i = 0; i < 8; i++) {
		*p++ = i < (int)sizeof(station) - 1 ? station[i] : ' ';
	}
	*p++ = '-';
	*p = '\0';
	size_t mlen = (size_t)(p - msg);

	// One bit buffer: preamble + header, three times back to back (afsk_mod reads each byte LSB first, as SAME sends)
	uint8_t bits[SAME_REPEATS * (SAME_PREAMBLE + sizeof msg)];
	uint16_t n = 0;
	for (int r = 0; r < SAME_REPEATS; r++) {
		memset(bits + n, 0xAB, SAME_PREAMBLE);
		n += SAME_PREAMBLE;
		memcpy(bits + n, msg, mlen);
		n = (uint16_t)(n + mlen);
	}
	return aprs_queue_afsk_burst(c->hz, c->narrow, c->level, bits, (uint16_t)((size_t)n * 8u), SAME_BAUD,
				     SAME_MARK_HZ, SAME_SPACE_HZ);
}

#endif	// CONFIG_SAME_TX
