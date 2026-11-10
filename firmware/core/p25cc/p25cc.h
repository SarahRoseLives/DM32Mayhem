// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// P25 Phase 1 control channel frames for the P25 CC TX app. The frame layout (frame sync, BCH NID, TSBK, CRC, trellis
// code, data interleave and the outbound status symbols) follows SiteSim (MIT, Copyright (c) 2026 SarahRoseLives) and
// TIA-102. The result is a stream of dibits packed four to a byte, ready for the modem's 4FSK transmitter.
#ifndef P25CC_H
#define P25CC_H

#include <stdint.h>
#include "config.h"

#if CONFIG_P25CC_TX
typedef struct {
	uint16_t nac;		// network access code
	uint32_t wacn;		// wide area communications network
	uint16_t sysid;		// system ID
	uint8_t rfss;		// RF subsystem ID
	uint8_t site;		// site ID
	uint16_t tg;		// talkgroup for the grant frame
	uint16_t vch;		// voice channel number for the grant (0: no grant frame)
	uint32_t freq_hz;	// control channel frequency (the IDEN_UP base)
} p25cc_cfg_t;

// Build a rotating set of control channel frames (IDEN_UP, NET_STS, RFSS_STS and a group voice grant) into out, four
// dibits a byte, MSB first. Returns the byte count, or 0 when it does not fit cap_bytes. The stream is meant to be sent
// continuously and looped; frames are not padded to the modem's 30 ms blocks.
uint16_t p25cc_build(const p25cc_cfg_t *c, uint8_t *out, uint16_t cap_bytes);
#endif

#endif
