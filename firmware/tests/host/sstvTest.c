// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// SSTV loopback: generate a Martin M1 VIS header and two lines on synthetic discriminator audio, decode them again
// (core/sstv/sstv_rx.c), and check the mode, the line count and the colors of known pixels.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "sstv/sstv_rx.h"

#define RATE		8000
#define AMP		8000
#define MAX_SAMPLES	400000

static int16_t sig[MAX_SAMPLES];
static size_t sig_n;
static double phase;

static void tone(double hz, int samples)
{
	for (int i = 0; i < samples && sig_n < MAX_SAMPLES; i++) {
		phase += 2.0 * M_PI * hz / RATE;
		if (phase >= 2.0 * M_PI) {
			phase -= 2.0 * M_PI;
		}
		sig[sig_n++] = (int16_t)lrint(AMP * sin(phase));
	}
}

static void us(double hz, uint32_t t_us)
{
	tone(hz, (int)((uint64_t)RATE * t_us / 1000000u));
}

static void vis(uint8_t code)
{
	us(1900, 300000);
	us(1200, 10000);
	us(1900, 300000);
	uint8_t par = 0;
	for (int i = 0; i < 7; i++) {
		uint8_t b = (code >> i) & 1u;
		par ^= b;
		us(b ? 1300 : 1100, 30000);
	}
	us(par ? 1300 : 1100, 30000);
	us(1200, 30000);
}

// A scan of scan_us with the pixel value from val: the radio samples 320 pixels over that time
static uint8_t val_ramp(uint16_t x) { return (uint8_t)(x * 255u / 319u); }
static uint8_t val_128(uint16_t x) { (void)x; return 128; }
static uint8_t val_160(uint16_t x) { (void)x; return 160; }
static void scan_line(uint32_t scan_us, uint8_t (*val)(uint16_t))
{
	uint32_t total = (uint32_t)((uint64_t)RATE * scan_us / 1000000u);
	for (uint32_t i = 0; i < total; i++) {
		uint16_t x = (uint16_t)((uint64_t)i * 320u / total);
		tone(1500.0 + val(x) * 800.0 / 255.0, 1);
	}
}

static int lines_seen, mode_seen;
static uint8_t got[256][320][3];	// line, x, r/g/b

static void on_pixel(void *ctx, uint16_t x, uint16_t y, uint16_t color)
{
	(void)ctx;
	if (x < 320 && y < 256) {
		got[y][x][0] = (uint8_t)(((color >> 11) & 0x1F) << 3);
		got[y][x][1] = (uint8_t)(((color >> 5) & 0x3F) << 2);
		got[y][x][2] = (uint8_t)((color & 0x1F) << 3);
	}
}

static void on_line(void *ctx, uint16_t line, uint16_t lines)
{
	(void)ctx; (void)lines;
	lines_seen = line + 1;
}

static void on_mode(void *ctx, uint8_t v, uint8_t mode, uint16_t w, uint16_t h)
{
	(void)ctx; (void)v; (void)w; (void)h;
	if (mode != SSTV_MODE_NONE) {
		mode_seen = mode;
	}
}

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
	int fails = 0;
	sstv_rx_t rx;
	vis(44);
	// Two Martin M1 lines: sync, porch, Y, sep, Cr, sep, Cb, sep
	for (int l = 0; l < 2; l++) {
		us(1200, 4862);
		us(1500, 572);
		scan_line(146432u, l == 0 ? val_ramp : val_128);
		us(1500, 572);
		scan_line(146432u, l == 0 ? val_128 : val_160);
		us(1500, 572);
		scan_line(146432u, val_128);
		us(1500, 572);
	}

	if (!sstv_rx_begin(&rx, RATE, on_pixel, on_line, on_mode, NULL)) {
		printf("FAIL begin\n");
		return 1;
	}
	// Feed in 160-sample blocks like the radio's ring
	for (size_t i = 0; i < sig_n; i += 160) {
		uint32_t n = (uint32_t)(sig_n - i < 160 ? sig_n - i : 160);
		sstv_rx_feed(&rx, &sig[i], n);
	}

	CHECK(mode_seen == SSTV_MODE_MARTIN_M1);
	CHECK(lines_seen >= 2);
	// Line 0, x=0 is black (Y=0, Cr=128, Cb=128); x=319 near white
	CHECK(got[0][8][0] <= 48 && got[0][8][1] <= 48 && got[0][8][2] <= 48);
	CHECK(got[0][150][0] > 80 && got[0][150][0] < 200);
	CHECK(got[0][319][0] > 200 && got[0][319][1] > 200 && got[0][319][2] > 200);
	// Line 1: Y=128, Cr=160, Cb=128 -> a red-ish gray
	CHECK(got[1][100][0] > got[1][100][2]);

	printf("%s: mode %d lines %d (samples %u frames %u)\n", fails ? "FAIL" : "PASS", mode_seen, lines_seen,
	       rx.samples, rx.frames);
	return fails ? 1 : 0;
}
