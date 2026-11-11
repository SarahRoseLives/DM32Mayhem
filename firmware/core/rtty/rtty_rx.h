// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// RTTY (Baudot/ITA2) receive for the RTTY RX app: a two-tone FSK demodulator for the FM discriminator audio, with a
// bit clock that locks to the tone changes and a UART for the start bit, five data bits and the stop bit. No board,
// RTOS or floating point. The tones are the transmitter's mark and mark + shift; 45.45 and 50 baud.
#ifndef RTTY_RX_H
#define RTTY_RX_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define RTTY_RX_WIN_MAX		256u	// correlator length limit: one bit at up to 11.5 kHz at 45 baud

typedef void (*rtty_rx_char_fn)(char c, void *ctx);

typedef struct {
	uint32_t rate;
	uint32_t win;			// one bit's samples
	uint32_t pos;
	uint32_t ph_mark, ph_space, inc_mark, inc_space;
	int32_t ring[4][RTTY_RX_WIN_MAX];
	int32_t sum[4];
	uint32_t peak_mark, peak_space;
	uint8_t decay_shift;
	int32_t pll, pll_step;
	bool last_mark;
	bool invert;			// mark and space swapped by the receiver's discriminator
	// The UART: idle (waiting for a start bit) or taking the five data bits
	uint8_t state;
	uint8_t nbits;
	uint8_t code;
	uint8_t shift;			// 0 LTRS, 1 FIGS
	// Counters for the app
	uint32_t samples, bits, chars, errors;
	rtty_rx_char_fn fn;
	void *ctx;
} rtty_rx_t;

// False when the rate or baud can't be handled (a bit longer than RTTY_RX_WIN_MAX samples). The decoder starts idle,
// expecting mark.
bool rtty_rx_init(rtty_rx_t *d, uint32_t rate, uint32_t baud, uint32_t mark_hz, uint32_t space_hz, bool invert,
		  rtty_rx_char_fn fn, void *ctx);

// Feed received FM discriminator audio. Each character is delivered to fn as it completes; LTRS/FIGS shifts are
// consumed, CR and LF are passed through.
void rtty_rx_feed(rtty_rx_t *d, const int16_t *s, size_t n);

#endif
