// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// SSTV receive (docs/spec/sstv.md): the VIS header, the line sync and the pixels from 8 kHz FM discriminator audio.
// The tones are 1200/1500/1900/2300 Hz and a pixel is 1500..2300 Hz, so the 8 kHz receive audio the radio already
// samples carries it. Pure state; no RTOS and no board access, so the host tests run it.
#ifndef SSTV_RX_H
#define SSTV_RX_H

#include <stdint.h>
#include <stdbool.h>

#define SSTV_LINE_MAX		320
#define SSTV_LINES_MAX		256
#define SSTV_MODE_NAME		8

// The modes the decoder follows (VIS code in brackets). Martin M1 [44], M2 [40]; Scottie S1 [60], S2 [56];
// Robot 36 [8], 72 [12]. The image is 320 wide except Robot (320 too); the height is what the mode sends.
typedef enum {
	SSTV_MODE_NONE = 0,
	SSTV_MODE_MARTIN_M1,
	SSTV_MODE_MARTIN_M2,
	SSTV_MODE_SCOTTIE_S1,
	SSTV_MODE_SCOTTIE_S2,
	SSTV_MODE_ROBOT_36,
	SSTV_MODE_ROBOT_72,
} sstv_mode_t;

// A pixel as it is decoded. color is the RGB565 the screens draw
typedef void (*sstv_pixel_fn)(void *ctx, uint16_t x, uint16_t y, uint16_t color);
// A line completed (0 before the first): lets the screen show progress and paint. lines is the mode's line count
typedef void (*sstv_line_fn)(void *ctx, uint16_t line, uint16_t lines);
// The header named a mode (mode may be SSTV_MODE_NONE for an unknown VIS), or a frame ended
typedef void (*sstv_mode_fn)(void *ctx, uint8_t vis, uint8_t mode, uint16_t w, uint16_t h);

typedef struct {
	uint32_t rate;			// audio rate, 8000 on the radio
	sstv_pixel_fn pixel;
	sstv_line_fn line_cb;
	sstv_mode_fn on_mode;
	void *ctx;
	// Tone: the zero crossings of the DC-blocked audio, interpolated between samples so 2300 Hz at 8 kHz (a
	// crossing every 1.7 samples) still resolves to a few Hz
	float dc;			// the running DC
	float bp_x1, bp_x2, bp_y1, bp_y2;	// band-pass state (rejects the hiss that adds crossings)
	float prev;			// the last band-passed sample
	bool prev_ok;
	int8_t zc_state;		// hysteresis: the last settled sign
	float last_cross;		// the last crossing's sample position (fractional)
	uint32_t total;		// samples fed since the start: the crossing times, never reset by the state machine
	bool cross_ok;
	float hz_hist[3];		// the last three raw tone estimates (a median rejects a noisy one)
	float hz;			// the median tone in Hz
	// The state machine
	uint8_t state;
	uint8_t mode;
	uint16_t lines;			// the mode's line count
	uint16_t line;			// the line being received
	uint32_t sample;		// samples since the state started
	uint32_t t0;			// the state's running sample boundary
	// VIS
	uint8_t vis_bits, vis_count, vis_parity;
	uint16_t bit_ones;	// samples of this bit above 1200 Hz (integrate and dump)
	bool armed;			// the second leader is confirmed: the next tone drop starts the bits
	// The line: which of Y/Cr/Cb (seg), the pixel in it
	uint8_t seg;
	uint16_t px;
	uint8_t y[SSTV_LINE_MAX];
	int8_t cr[SSTV_LINE_MAX], cb[SSTV_LINE_MAX];
	uint8_t have;			// which of Y, Cr, Cb this line has: bit 0 Y, 1 Cr, 2 Cb
	uint32_t samples;
	uint32_t frames;
} sstv_rx_t;

// rate, callbacks (each may be NULL), returns false when the rate can't be handled
bool sstv_rx_begin(sstv_rx_t *rx, uint32_t rate, sstv_pixel_fn pixel, sstv_line_fn line, sstv_mode_fn on_mode,
		   void *ctx);
// Back to waiting for a VIS header
void sstv_rx_reset(sstv_rx_t *rx);
// Feed received FM discriminator audio, signed 16-bit at rx->rate
void sstv_rx_feed(sstv_rx_t *rx, const int16_t *samples, uint32_t n);

#endif
