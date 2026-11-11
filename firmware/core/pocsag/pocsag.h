// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The DM32Mayhem Authors
// POCSAG (Post Office Code Standardisation Advisory Group) paging: the encoder's bit stream and a receiver that
// turns 8 kHz discriminator audio into pages. The air format is the public POCSAG standard: a preamble, the sync
// codeword, then batches of sixteen 32-bit codewords, BCH(31,21) with even parity.
#ifndef POCSAG_H
#define POCSAG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define POCSAG_MAX_TEXT		80
#define POCSAG_BAUD_512		512u
#define POCSAG_BAUD_1200	1200u
#define POCSAG_BAUD_2400	2400u

// One page to send. capcode is 21 bits (the address is capcode >> 3, the frame capcode & 7); func 0 numeric,
// 3 alphanumeric for most pagers. repeat is 1..4 pages. invert flips the data polarity.
typedef struct {
	uint32_t capcode;
	uint8_t func;
	bool alpha;
	uint16_t baud;
	uint8_t repeat;
	bool invert;
	const char *text;
} pocsag_tx_cfg_t;

// Pack the page's bits, MSB first, into out (cap_bits). False when the page doesn't fit or the capcode is out of range.
bool pocsag_build(const pocsag_tx_cfg_t *cfg, uint8_t *out, uint16_t cap_bits, uint16_t *nbits);

typedef struct {
	uint32_t capcode;
	uint8_t func;
	bool alpha;
	uint16_t baud;
	char text[POCSAG_MAX_TEXT + 1];
} pocsag_msg_t;

typedef void (*pocsag_msg_cb)(const pocsag_msg_t *msg, void *ctx);

// One baud's bit-clock, sync and page assembly. Exposed so a caller can keep the receiver static (the radio has no
// room for a heap allocation per app).
typedef struct {
	uint16_t baud;
	uint32_t inc;		// samples per bit, Q16
	uint32_t phase;		// since the last transition (or free-running), Q16
	int32_t dc;		// the discriminator's slow rest point
	int32_t peak;		// the slicer's adaptive threshold (peak follower)
	int8_t last_sign;
	bool have_last;
	bool sampled;
	uint32_t shift;		// the sync search
	bool synced;
	bool invert;		// the sync matched the inverted polarity: the data bits are inverted
	bool expect_sync;
	uint32_t sync_shift;
	uint32_t cw;
	uint8_t cw_bits;
	uint8_t cw_index;
	bool have_msg;
	uint32_t capcode;
	uint8_t func;
	bool alpha;
	uint32_t bitacc;
	uint8_t bitn;
	uint8_t text_len;
	char text[POCSAG_MAX_TEXT + 1];
	uint32_t syncs, words, bad;	// diagnostics for the app's status line
} pocsag_dec_t;

// A receiver, fed 8 kHz signed samples. baud 0 tries 512, 1200 and 2400 at once; a value uses that one.
typedef struct {
	uint16_t rate;
	uint8_t n;
	pocsag_msg_cb cb;
	void *ctx;
	uint32_t samples;
	pocsag_dec_t d[3];
} pocsag_rx_t;

void pocsag_rx_init(pocsag_rx_t *rx, uint16_t baud, pocsag_msg_cb cb, void *ctx);
void pocsag_rx_feed(pocsag_rx_t *rx, const int16_t *samples, size_t n);

#endif	// POCSAG_H
