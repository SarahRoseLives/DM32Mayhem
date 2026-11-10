// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// P25 Phase 1 C4FM transmit through the modem's physical-layer 4FSK transmitter (the P25 CC TX app). The radio core
// has keyed the carrier in FM; the board mutes the microphone, switches the modem to its PHY continuous mode and feeds
// it the symbol stream in 30 ms blocks. Optional: board.p25_tx is NULL where there is none.
#ifndef HAL_P25_TX_H
#define HAL_P25_TX_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
	// Start streaming packed dibits (four to a byte, MSB first), looping the buffer, until stop(). The radio must be
	// keyed in FM. False when it can't
	bool (*start)(const uint8_t *bytes, uint16_t len);
	bool (*busy)(void);
	void (*stop)(void);
} hal_p25_tx_t;

#endif
