// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// ACARS (ARINC 618/620) receive: a 2400 bps MSK demodulator for the AM airband audio (an HR_C7000 8 kHz ring) and
// the character-oriented frame: SYN SYN SOH, the mode/address/ack/label/block id and text, ETX, CRC-16. No board,
// RTOS or floating point requirement beyond the demodulator. The format is public; the demodulator follows the
// classic VCO/matched-filter MSK receiver.
#ifndef ACARS_H
#define ACARS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define ACARS_TEXT_MAX		240
#define ACARS_ADDR_MAX		8

typedef struct {
	char mode;				// the first frame character after SOH
	char addr[ACARS_ADDR_MAX];		// aircraft registration, dots removed
	char ack;				// '!' for NAK
	char label[3];				// the two-character label
	char bid;				// block id ('0'..'9' downlink, else uplink)
	char text[ACARS_TEXT_MAX + 1];		// printable message text (message number and flight id kept)
	uint8_t err;				// parity errors seen in the frame
	int16_t level;				// relative signal level (matched filter), tenths of a dB-ish
} acars_msg_t;

typedef void (*acars_msg_fn)(const acars_msg_t *m, void *ctx);

// Internal demodulator limits: one bit is rate/1200 samples at 8 kHz = 7; a higher rate needs a longer filter
#define ACARS_FLEN_MAX		16
#define ACARS_OV		4
#define ACARS_H_MAX		(ACARS_FLEN_MAX * ACARS_OV + 1)

typedef struct {
	uint32_t rate;
	float inc0;				// 2*pi*1800/rate
	uint16_t flen, fleno;
	float h[ACARS_H_MAX];
	// demodulator
	float p, clk, df, fo;			// fo: the VCO's tone offset (an AFC for the receiver's audio centre)
	float hp_a, hp_x, hp_y;		// the input high pass (the receiver's DC and low-frequency)
	uint8_t msk_s;
	uint16_t idx;
	float inb_re[ACARS_FLEN_MAX], inb_im[ACARS_FLEN_MAX];
	// frame parser (MSB first, odd parity)
	uint8_t state, nbits, outbits;
	uint8_t txt[ACARS_TEXT_MAX + 4];
	uint16_t len;
	uint8_t crc[2], perr;
	// counters
	uint32_t samples, frames, errors, bit_count;
	float level_sum;
	acars_msg_fn fn;
	void *ctx;
} acars_rx_t;

// Start a receiver for audio at rate (8000 on the radio). fn may be NULL. False when the rate has no room in the filter.
bool acars_rx_init(acars_rx_t *d, uint32_t rate, acars_msg_fn fn, void *ctx);
// Shift the VCO/matched-filter centre by hz (the AM detector's audio centre is not always 1800); the bit clock is not moved
void acars_rx_set_fo(acars_rx_t *d, float hz);
// Feed received AM-demodulated audio (the audio that carries the MSK), 8 kHz s16.
void acars_rx_feed(acars_rx_t *d, const int16_t *s, size_t n);

#endif
