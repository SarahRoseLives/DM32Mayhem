// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// RTTY loopback on synthetic discriminator audio: encode ITA2 with the transmitter's encoder, modulate it as
// continuous-phase FSK at 8 kHz, optionally high passed like the DM32's FM discriminator, and decode it again
// (core/rtty/rtty_rx.c). 45 and 50 baud, the transmitter's tone presets, and an inverted discriminator.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "rtty/rtty.h"
#include "rtty/rtty_rx.h"

#define RATE		8000
#define AMP		8000
#define MAX_SAMPLES	400000

static int16_t sig[MAX_SAMPLES];
static size_t sig_n;
static char out[256];
static size_t out_n;

static void on_char(char c, void *ctx)
{
	(void)ctx;
	if (out_n < sizeof out - 1) {
		out[out_n++] = c;
		out[out_n] = '\0';
	}
}

static void tone(double *ph, double hz, int n)
{
	for (int i = 0; i < n && sig_n < MAX_SAMPLES; i++) {
		*ph += 2.0 * M_PI * hz / RATE;
		if (*ph >= 2.0 * M_PI) {
			*ph -= 2.0 * M_PI;
		}
		sig[sig_n++] = (int16_t)lrint(AMP * sin(*ph));
	}
}

// Continuous-phase FSK: 20 bits of idle mark, the encoded stream, 20 bits of mark
static void modulate(const uint8_t *bits, uint16_t nbits, uint32_t baud, uint32_t mark, uint32_t space)
{
	double ph = 0.0;
	uint64_t step_q = ((uint64_t)RATE << 16) / baud;	// samples per bit, Q16
	uint64_t end_q = 0;
	size_t made = 0;
	for (uint16_t i = 0; i < nbits + 40u; i++) {
		bool b = true;
		if (i >= 20u && i < nbits + 20u) {
			uint16_t k = (uint16_t)(i - 20u);
			b = ((bits[k >> 3] >> (k & 7u)) & 1u) != 0;
		}
		end_q += step_q;
		while (made < (size_t)(end_q >> 16)) {
			tone(&ph, b ? mark : space, 1);
			made++;
		}
	}
}

static void high_pass(void)
{
	double a = 1.0 / (1.0 + 2.0 * M_PI * 300.0 / RATE);
	double x1 = 0.0, y1 = 0.0;
	for (size_t i = 0; i < sig_n; i++) {
		double x = sig[i];
		double y = a * (y1 + x - x1);
		x1 = x;
		y1 = y;
		sig[i] = (int16_t)lrint(y);
	}
}

static int run(const char *text, uint32_t baud, uint32_t mark, uint32_t space, bool invert, bool hp)
{
	uint8_t bits[2048];
	uint16_t nbits = rtty_encode(text, bits, (uint16_t)(8u * sizeof bits));
	if (nbits == 0) {
		printf("  encode failed\n");
		return 1;
	}
	sig_n = 0;
	// An inverted discriminator: the tones on the wire are swapped, and so is the decoder's mark/space
	modulate(bits, nbits, baud, invert ? space : mark, invert ? mark : space);
	if (hp) {
		high_pass();
	}
	out_n = 0;
	out[0] = '\0';
	rtty_rx_t rx;
	bool ok = rtty_rx_init(&rx, RATE, baud, mark, space, invert, on_char, NULL);
	if (!ok) {
		printf("  init failed\n");
		return 1;
	}
	for (size_t i = 0; i < sig_n; i += 160) {
		size_t n = sig_n - i < 160 ? sig_n - i : 160;
		rtty_rx_feed(&rx, &sig[i], n);
	}
	bool good = strcmp(out, text) == 0;
	printf("%s  %u baud %u/%u%s%s -> \"%s\" (chars %u, errors %u)\n",
	       good ? "ok  " : "FAIL", (unsigned)baud, (unsigned)mark, (unsigned)space,
	       invert ? " inv" : "", hp ? " hp" : "", out, (unsigned)rx.chars, (unsigned)rx.errors);
	if (!good) {
		printf("       expected \"%s\"\n", text);
	}
	return good ? 0 : 1;
}

int main(void)
{
	int fails = 0;
	const char *msg = "HELLO WORLD 123";
	fails += run(msg, 45, 2125, 2295, false, false);
	fails += run(msg, 45, 2125, 2295, false, true);
	fails += run(msg, 50, 2125, 2295, false, false);
	fails += run(msg, 45, 1275, 1445, false, true);
	fails += run(msg, 45, 1700, 1870, false, true);
	fails += run(msg, 45, 2125, 2295, true, true);
	if (fails != 0) {
		printf("rttyRxTest: %d failure(s)\n", fails);
		return 1;
	}
	printf("rttyRxTest: all passed\n");
	return 0;
}
