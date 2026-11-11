// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The DM32Mayhem Authors
// POCSAG transmit: a baseband 2FSK waveform through the FM modulator. The radio core has keyed the carrier in FM; the
// board mutes the microphone, sends the bits at baud and restores the microphone at stop. Optional: NULL when the
// board has no such path.
#ifndef HAL_POCSAG_H
#define HAL_POCSAG_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
	// bits: MSB first, the encoder's pocsag_build() stream. baud: 512, 1200 or 2400. invert: flip the data polarity.
	// level 0..100 scales the deviation. False when the radio isn't transmitting FM or is busy.
	bool (*start)(const uint8_t *bits, uint16_t nbits, uint16_t baud, bool invert, uint8_t level);
	bool (*busy)(void);
	void (*stop)(void);
} hal_pocsag_tx_t;

#endif	// HAL_POCSAG_H
