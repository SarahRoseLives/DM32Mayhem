// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// RTTY (Baudot/ITA2) receive (rtty_rx.h). The demodulator is the APRS AFSK demodulator's shape: a quadrature
// correlator per tone over one bit, each tone's magnitude normalized by its own decaying peak (undoing the receiver's
// tilt), the two differenced with a little hysteresis, and a digital PLL that locks the bit clock to the tone changes.
// Instead of HDLC, the sampled bits go to a UART: a start bit, five data bits LSB first, a stop bit. The tones are
// well above the FM discriminator's high pass, so this is one of the protocols the DM32's audio path handles well.
#include <string.h>
#include "rtty/rtty_rx.h"

// sin(2 pi i / 256) x 32767
static const int16_t sine[256] = {
	0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739, 9512, 10278, 11039, 11793,
	12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530, 18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
	23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
	30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
	32767, 32757, 32728, 32678, 32609, 32521, 32412, 32285, 32137, 31971, 31785, 31580, 31356, 31113, 30852, 30571,
	30273, 29956, 29621, 29268, 28898, 28510, 28105, 27683, 27245, 26790, 26319, 25832, 25329, 24811, 24279, 23731,
	23170, 22594, 22005, 21403, 20787, 20159, 19519, 18868, 18204, 17530, 16846, 16151, 15446, 14732, 14010, 13279,
	12539, 11793, 11039, 10278, 9512, 8739, 7962, 7179, 6393, 5602, 4808, 4011, 3212, 2410, 1608, 804,
	0, -804, -1608, -2410, -3212, -4011, -4808, -5602, -6393, -7179, -7962, -8739, -9512, -10278, -11039, -11793,
	-12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530, -18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
	-23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790, -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
	-30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971, -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
	-32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285, -32137, -31971, -31785, -31580, -31356, -31113, -30852, -30571,
	-30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683, -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731,
	-23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868, -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
	-12539, -11793, -11039, -10278, -9512, -8739, -7962, -7179, -6393, -5602, -4808, -4011, -3212, -2410, -1608, -804,
};

#define MIX_SHIFT	8	// sample x sine (Q15) >> 8: one bit's sum stays within int32
#define HYST_Q8		13	// hysteresis on the normalized difference, in 1/256 (13 = 0.05)
#define PLL_SEARCH	3	// pull per tone change before the first character
#define PLL_LOCKED	10	// ... after it
// A one-time nudge when the start bit is found, in eighths of a bit: the correlator's decision trails the edge, and
// the first character (LTRS, all ones) has no data change to pull the clock back
#ifndef RTTY_RX_START_BIAS
#define RTTY_RX_START_BIAS 0
#endif

static uint32_t phase_inc(uint32_t hz, uint32_t rate)
{
	return (uint32_t)(((uint64_t)hz << 32) / rate);
}

// |(i, q)| within about 3 %: max(M, 7/8 M + 1/2 m) of the larger and smaller part
static uint32_t mag(int32_t i, int32_t q)
{
	uint32_t a = (uint32_t)(i < 0 ? -i : i), b = (uint32_t)(q < 0 ? -q : q);
	uint32_t hi = a > b ? a : b, lo = a > b ? b : a;
	uint32_t m = hi - (hi >> 3) + (lo >> 1);
	return m > hi ? m : hi;
}

static uint32_t peak(uint32_t p, uint32_t m, uint8_t shift)
{
	p -= p >> shift;
	return m > p ? m : p;
}

// ITA2: letters and figures share the five-bit codes; LTRS (0x1F) and FIGS (0x1B) switch between them
static const char ita2_ltrs[32] = {
	0, 'E', '\n', 'A', ' ', 'S', 'I', 'U', '\r', 'D', 'R', 'J', 'N', 'F', 'C', 'K',
	'T', 'Z', 'L', 'W', 'H', 'Y', 'P', 'Q', 'O', 'B', 'G', 0, 'M', 'X', 'V', 0
};
static const char ita2_figs[32] = {
	0, '3', '\n', '-', ' ', '\'', '8', '7', '\r', '$', '4', ' ', ',', '!', ':', '(',
	'5', '"', ')', '2', '#', '6', '0', '1', '9', '?', '&', 0, '.', '/', ';', 0
};

static void emit(rtty_rx_t *d, uint8_t code)
{
	code &= 31u;
	if (code == 0x1Fu) {
		d->shift = 0;		// LTRS
		return;
	}
	if (code == 0x1Bu) {
		d->shift = 1;		// FIGS
		return;
	}
	char c = (d->shift != 0 ? ita2_figs : ita2_ltrs)[code];
	if (c != '\0' && d->fn != NULL) {
		d->fn(c, d->ctx);
	}
}

static void bit_sample(rtty_rx_t *d, bool mark)
{
	d->bits++;
	if (d->state == 0) {
		if (!mark) {		// idle is mark; a space starts a character
			d->state = 1;
			d->nbits = 0;
			d->code = 0;
			d->pll += (int32_t)((int64_t)d->pll_step * (int64_t)d->win * RTTY_RX_START_BIAS / 8);
		}
		return;
	}
	if (d->nbits < 5) {		// data bits, LSB first
		if (mark) {
			d->code |= (uint8_t)(1u << d->nbits);
		}
		d->nbits++;
		return;
	}
	d->state = 0;			// the stop bit; a bad one drops the character
	if (mark) {
		// The first character after the clock finds the signal is the least reliable (the correlator is still
		// settling) and is usually the transmitter's LTRS; drop it unless it is a shift the next characters need
		if (d->chars != 0 || d->code == 0x1Fu || d->code == 0x1Bu) {
			emit(d, d->code);
		}
		d->chars++;
	} else {
		d->errors++;
	}
}

static void sample(rtty_rx_t *d, int16_t x)
{
	int32_t cm = sine[((d->ph_mark >> 24) + 64u) & 255u], sm = sine[d->ph_mark >> 24];
	int32_t cs = sine[((d->ph_space >> 24) + 64u) & 255u], ss = sine[d->ph_space >> 24];
	d->ph_mark += d->inc_mark;
	d->ph_space += d->inc_space;
	d->samples++;
	int32_t v[4];
	v[0] = (x * cm) >> MIX_SHIFT;
	v[1] = (x * sm) >> MIX_SHIFT;
	v[2] = (x * cs) >> MIX_SHIFT;
	v[3] = (x * ss) >> MIX_SHIFT;
	for (int k = 0; k < 4; k++) {	// exact integer sums: no drift to correct
		d->sum[k] += v[k] - d->ring[k][d->pos];
		d->ring[k][d->pos] = v[k];
	}
	uint32_t mm = mag(d->sum[0], d->sum[1]), ms = mag(d->sum[2], d->sum[3]);
	d->peak_mark = peak(d->peak_mark, mm, d->decay_shift);
	d->peak_space = peak(d->peak_space, ms, d->decay_shift);
	// mm / peak_mark - ms / peak_space against the hysteresis, cross multiplied (both peaks > 0 once audio arrives)
	int64_t pm = d->peak_mark, ps = d->peak_space;
	int64_t diff = (int64_t)mm * ps - (int64_t)ms * pm;
	int64_t hyst = (int64_t)(((uint64_t)pm * (uint64_t)ps) >> 8) * HYST_Q8;
	bool mark = d->last_mark ? diff > -hyst : diff > hyst;
	if (d->invert) {
		mark = !mark;		// a discriminator that swaps mark and space is undone here
	}

	// The PLL counter wraps +2^31 -> -2^31 once a bit, which is where the bit is sampled; a tone change pulls it
	// toward 0 (the bit edge)
	int32_t prev = d->pll;
	d->pll = (int32_t)((uint32_t)d->pll + (uint32_t)d->pll_step);
	if (prev > 0 && d->pll < 0) {
		bit_sample(d, mark);
	}
	if (mark != d->last_mark) {
		d->pll -= d->pll / (d->chars == 0 ? PLL_SEARCH : PLL_LOCKED);
		d->last_mark = mark;
	}
	if (++d->pos >= d->win) {
		d->pos = 0;
	}
}

bool rtty_rx_init(rtty_rx_t *d, uint32_t rate, uint32_t baud, uint32_t mark_hz, uint32_t space_hz, bool invert,
		  rtty_rx_char_fn fn, void *ctx)
{
	memset(d, 0, sizeof(*d));
	if (rate == 0 || baud == 0) {
		return false;
	}
	uint32_t win = (rate + baud / 2u) / baud;
	if (win == 0 || win > RTTY_RX_WIN_MAX) {
		return false;
	}
	d->rate = rate;
	d->win = win;
	d->inc_mark = phase_inc(mark_hz, rate);
	d->inc_space = phase_inc(space_hz, rate);
	d->pll_step = (int32_t)phase_inc(baud, rate);
	// The peaks fall by 1/2^shift a sample: to 1/e in about 60 bits (the nearest power of two to 60 x win)
	uint32_t t = 60u * win;
	d->decay_shift = 0;
	while ((1u << (d->decay_shift + 1u)) <= t + t / 2u) {
		d->decay_shift++;
	}
	d->invert = invert;
	d->fn = fn;
	d->ctx = ctx;
	d->last_mark = true;		// the line idles at mark
	return true;
}

void rtty_rx_feed(rtty_rx_t *d, const int16_t *s, size_t n)
{
	while (n--) {
		sample(d, *s++);
	}
}
