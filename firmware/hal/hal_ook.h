// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// On-off keying the transmitter (the OOK TX app): the radio core has keyed the carrier in FM; the board mutes the
// microphone and switches the PA on and off at the bit rate. Optional: board.ook is NULL where there is none.
#ifndef HAL_OOK_H
#define HAL_OOK_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
	// Start sending nbits bits (MSB first in each byte) at bit_us, then repeat-1 more times with pause_us of silence
	// between. The radio must be keyed in FM; bits stays valid until busy() is false. False when it can't
	bool (*start)(const uint8_t *bits, uint16_t nbits, uint32_t bit_us, uint8_t repeat, uint32_t pause_us);
	bool (*busy)(void);
	// Stop (early or after the end): the PA off, the microphone back; the core unkeys after this
	void (*stop)(void);
} hal_ook_t;

#endif
