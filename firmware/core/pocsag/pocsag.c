// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The DM32Mayhem Authors
// POCSAG encoder and receiver (pocsag.h). No board or RTOS dependency: the host tests make a page and decode it
// from synthetic discriminator audio, and the radio's app feeds the board's received audio.
#include <string.h>
#include "pocsag/pocsag.h"
#include "pocsag/pocsag_bch_table.h"

#define PREAMBLE_BITS	576u
#define POCSAG_SYNC	0x7CD215D8u
#define POCSAG_IDLE	0x7A89C197u
#define POCSAG_RATE	8000u

// --- BCH(31,21) + even parity ---

static uint8_t parity32(uint32_t v)
{
	v ^= v >> 16;
	v ^= v >> 8;
	v ^= v >> 4;
	v &= 0x0Fu;
	return (uint8_t)((0x6996u >> v) & 1u);
}

static uint32_t bch_remainder(uint32_t cw31)
{
	for (int i = 30; i >= 10; i--) {
		if (cw31 & (1u << i)) {
			cw31 ^= 0x769u << (i - 10);
		}
	}
	return cw31 & 0x3FFu;
}

// A 21-bit data word (bit 20 is the address/message flag) as the 32-bit codeword
static uint32_t codeword(uint32_t data21)
{
	uint32_t cw31 = (data21 << 10) | bch_remainder(data21 << 10);
	return (cw31 << 1) | parity32(cw31);
}

// Correct and check one received 32-bit codeword. False: not a codeword.
static bool codeword_check(uint32_t *cw)
{
	uint32_t c = *cw;
	uint32_t parity = c & 1u;
	uint32_t cw31 = (c >> 1) & 0x7FFFFFFFu;
	uint32_t s = bch_remainder(cw31);

	if (s != 0) {
		uint32_t err = pocsag_bch_error[s];
		if (err == 0xFFFFFFFFu) {
			return false;
		}
		cw31 ^= err;
		c = (cw31 << 1) | parity;
	}
	if (parity32(cw31) != parity) {
		if (s != 0) {
			return false;	// corrected to a codeword whose parity still fails: not ours
		}
		c ^= 1u;	// the parity bit itself
	}
	*cw = c;
	return true;
}

static int hamming32(uint32_t a, uint32_t b)
{
	uint32_t v = a ^ b;
	int n = 0;
	while (v) {
		v &= v - 1u;
		n++;
	}
	return n;
}

// --- encoder ---

static void put_bit(uint8_t *out, uint32_t *bit, uint32_t cap, bool v)
{
	if (*bit < cap && v) {
		out[*bit >> 3] |= (uint8_t)(1u << (7u - (*bit & 7u)));
	}
	(*bit)++;
}

static void put_word(uint8_t *out, uint32_t *bit, uint32_t cap, uint32_t w, uint8_t n)
{
	for (int i = n - 1; i >= 0; i--) {
		put_bit(out, bit, cap, (w >> i) & 1u);
	}
}

// POCSAG message characters are sent LSB first inside their 7 (alpha) or 4 (numeric) bits. The codeword
// packs those fields MSB first, so the field's bits are reversed on the way in and out.
static uint8_t rev7(uint8_t v)
{
	return (uint8_t)(((v & 0x01u) << 6) | ((v & 0x02u) << 4) | ((v & 0x04u) << 2) | (v & 0x08u) |
			 ((v & 0x10u) >> 2) | ((v & 0x20u) >> 4) | ((v & 0x40u) >> 6));
}

static uint8_t rev4(uint8_t v)
{
	return (uint8_t)(((v & 0x01u) << 3) | ((v & 0x02u) << 1) | ((v & 0x04u) >> 1) | ((v & 0x08u) >> 3));
}

static int numeric_digit(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c == 'U' || c == 'u') return 0xB;
	if (c == ' ') return 0xC;
	if (c == '-' || c == '.') return 0xD;
	if (c == ']') return 0xE;
	if (c == '[') return 0xF;
	return -1;
}

bool pocsag_build(const pocsag_tx_cfg_t *cfg, uint8_t *out, uint16_t cap_bits, uint16_t *nbits)
{
	if (cfg == NULL || out == NULL || nbits == NULL || cfg->text == NULL || cfg->baud < 512u || cfg->baud > 2400u ||
	    cfg->capcode > 0x1FFFFFu) {
		return false;
	}
	memset(out, 0, (size_t)((cap_bits + 7u) / 8u));

	uint32_t words[48];
	uint8_t nw = 0;
	uint32_t acc = 0;
	uint8_t nb = 0;

	if (cfg->alpha) {
		// OpenPOCSAG framing: 7-bit characters LSB first, zero-padded to a 20-bit codeword. No ETX/0x04 padding.
		static uint8_t bits[20u * sizeof words];
		uint16_t nbit = 0;
		for (const char *p = cfg->text; *p != '\0' && nbit + 7u <= sizeof bits; p++) {
			uint8_t c = (uint8_t)*p & 0x7Fu;
			for (int k = 0; k < 7; k++) {
				bits[nbit++] = (c >> k) & 1u;
			}
		}
		while (nbit % 20u != 0 && nbit < sizeof bits) {
			bits[nbit++] = 0;
		}
		for (uint16_t i = 0; i < nbit; i += 20u) {
			uint32_t field = 0;
			for (int k = 0; k < 20; k++) {
				field = (field << 1) | bits[i + k];
			}
			words[nw++] = field;
		}
	} else {
		for (const char *p = cfg->text; *p != '\0' && nw < sizeof words - 1; p++) {
			int d = numeric_digit(*p);
			if (d < 0) d = 0xC;	// space
			acc = (acc << 4) | (uint32_t)rev4((uint8_t)d);
			nb = (uint8_t)(nb + 4u);
			if (nb >= 20u) {
				nb = (uint8_t)(nb - 20u);
				words[nw++] = (acc >> nb) & 0xFFFFFu;
				acc = nb ? (acc & ((1u << nb) - 1u)) : 0;
			}
		}
		if (nb != 0) {
			while (nb < 20u) {
				acc = (acc << 4) | 0xCu;	// space padding
				nb = (uint8_t)(nb + 4u);
			}
			words[nw++] = acc & 0xFFFFFu;
		}
	}

	uint32_t frame = cfg->capcode & 7u;
	uint32_t address = cfg->capcode >> 3;
	uint32_t slots = 2u * frame + 1u + nw;
	uint32_t batches = (slots + 15u) / 16u;
	uint32_t reps = cfg->repeat < 1u ? 1u : cfg->repeat > 4u ? 4u : cfg->repeat;
	uint64_t total = (uint64_t)reps * (PREAMBLE_BITS + (uint64_t)batches * (32u + 16u * 32u));
	if (total > cap_bits) {
		return false;
	}

	uint32_t bit = 0;
	for (uint32_t r = 0; r < reps; r++) {
		for (uint32_t i = 0; i < PREAMBLE_BITS; i++) {
			put_bit(out, &bit, cap_bits, (i & 1u) == 0u);
		}
		uint32_t wi = 0;
		for (uint32_t b = 0; b < batches; b++) {
			put_word(out, &bit, cap_bits, POCSAG_SYNC, 32);
			for (uint32_t s = 0; s < 16u; s++) {
				uint32_t global = b * 16u + s;
				uint32_t cw;
				if (b == 0 && s == 2u * frame) {
					uint32_t data21 = (address << 2) | (cfg->func & 3u);
					cw = codeword(data21);
				} else if (wi < nw && global > 2u * frame) {
					uint32_t data21 = (1u << 20) | (words[wi++] & 0xFFFFFu);
					cw = codeword(data21);
				} else {
					cw = POCSAG_IDLE;
				}
				put_word(out, &bit, cap_bits, cw, 32);
			}
		}
	}
	if (cfg->invert) {
		for (uint32_t i = 0; i < bit && i < cap_bits; i++) {
			out[i >> 3] ^= (uint8_t)(1u << (7u - (i & 7u)));
		}
	}
	*nbits = (uint16_t)bit;
	return true;
}

// --- receiver ---

static void flush_msg(pocsag_rx_t *rx, pocsag_dec_t *d)
{
	if (!d->have_msg) {
		return;
	}
	d->have_msg = false;
	d->text[d->text_len] = '\0';
	if (d->text_len != 0 && rx->cb != NULL) {
		pocsag_msg_t m;
		m.capcode = d->capcode;
		m.func = d->func;
		m.alpha = d->alpha;
		m.baud = d->baud;
		memcpy(m.text, d->text, (size_t)d->text_len + 1u);
		rx->cb(&m, rx->ctx);
	}
	d->text_len = 0;
	d->bitn = 0;
	d->bitacc = 0;
}

static void append_bits(pocsag_rx_t *rx, pocsag_dec_t *d, uint32_t data20)
{
	d->bitacc = (d->bitacc << 20) | (data20 & 0xFFFFFu);
	d->bitn = (uint8_t)(d->bitn + 20u);
	uint8_t width = d->alpha ? 7u : 4u;
	while (d->bitn >= width) {
		d->bitn = (uint8_t)(d->bitn - width);
		uint32_t ch = (d->bitacc >> d->bitn) & ((1u << width) - 1u);
		if (d->bitn) {
			d->bitacc &= (1u << d->bitn) - 1u;
		} else {
			d->bitacc = 0;
		}
		if (d->alpha) {
			ch = rev7((uint8_t)ch);
			if (ch == 0x03u) {	// ETX ends the page
				flush_msg(rx, d);
				return;
			}
			if (ch == 0x04u || ch == 0x00u) {
				continue;	// pad
			}
			ch = (ch >= 0x20u && ch < 0x7Fu) ? ch : ' ';
		} else {
			static const char num[16] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ' ', 'U', ' ',
						      '-', ']', '[' };
			ch = (uint8_t)num[rev4((uint8_t)ch) & 15u];
		}
		if (d->text_len < POCSAG_MAX_TEXT) {
			d->text[d->text_len++] = (char)ch;
		}
	}
}

static void handle_cw(pocsag_rx_t *rx, pocsag_dec_t *d, uint32_t cw)
{
	if (!codeword_check(&cw)) {
		d->bad++;
		return;
	}
	d->words++;
	if (cw == POCSAG_IDLE) {
		flush_msg(rx, d);
		return;
	}
	uint32_t flag = cw >> 31;
	uint32_t data20 = (cw >> 11) & 0xFFFFFu;
	if (flag == 0) {
		flush_msg(rx, d);	// a new address ends the page before it
		uint32_t address = data20 >> 2;
		d->func = (uint8_t)(data20 & 3u);
		d->capcode = (address << 3) | (d->cw_index >> 1);
		d->alpha = d->func != 0;	// 0 numeric, 3 alphanumeric (1 and 2 are tone modes)
		d->have_msg = true;
		d->text_len = 0;
		d->bitn = 0;
		d->bitacc = 0;
	} else if (d->have_msg) {
		append_bits(rx, d, data20);
	}
}

static void dec_bit(pocsag_rx_t *rx, pocsag_dec_t *d, uint8_t bit)
{
	if (!d->synced) {
		d->shift = (d->shift << 1) | bit;
		if (hamming32(d->shift, POCSAG_SYNC) <= 2 || hamming32(d->shift, ~POCSAG_SYNC) <= 2) {
			d->synced = true;
			d->syncs++;
			d->invert = hamming32(d->shift, POCSAG_SYNC) > 2;
			d->expect_sync = false;
			d->cw = 0;
			d->cw_bits = 0;
			d->cw_index = 0;
		}
		return;
	}
	if (d->invert) {
		bit ^= 1u;
	}
	if (d->expect_sync) {
		d->sync_shift = (d->sync_shift << 1) | bit;
		d->cw_bits++;
		if (d->cw_bits == 32u) {
			d->cw_bits = 0;
			if (hamming32(d->sync_shift, POCSAG_SYNC) <= 2) {
				d->invert = false;
				d->expect_sync = false;
				d->cw_index = 0;
			} else if (hamming32(d->sync_shift, ~POCSAG_SYNC) <= 2) {
				d->invert = true;
				d->expect_sync = false;
				d->cw_index = 0;
			} else {
				// Not the expected sync: look again from these bits
				d->synced = false;
				d->shift = d->sync_shift;
				flush_msg(rx, d);
			}
		}
		return;
	}
	d->cw = (d->cw << 1) | bit;
	d->cw_bits++;
	if (d->cw_bits == 32u) {
		d->cw_bits = 0;
		uint32_t cw = d->cw;
		handle_cw(rx, d, cw);
		if (++d->cw_index == 16u) {
			d->cw_index = 0;
			d->expect_sync = true;
			d->sync_shift = 0;
			d->cw_bits = 0;
		}
	}
}

static void feed_one(pocsag_rx_t *rx, pocsag_dec_t *d, int16_t sample)
{
	// A slow DC average: the FM discriminator's rest point. The data itself moves much faster.
	d->dc += ((int32_t)sample - d->dc) >> 8;
	int32_t v = (int32_t)sample - d->dc;
	// The discriminator audio is high-passed by the FM receive path, so a run of one bit droops toward zero and a
	// plain zero-crossing slicer sees spurious transitions. Use an adaptive threshold (half the recent peak) with
	// hysteresis: a transition only counts when the signal leaves the band, which the real edges do.
	int32_t av = v < 0 ? -v : v;
	d->peak += (av - d->peak) >> 6;
	int32_t h = d->peak >> 1;
	int8_t sign = d->last_sign;
	if (v > h) {
		sign = 1;
	} else if (v < -h) {
		sign = -1;
	}

	if (d->have_last && sign != d->last_sign) {
		// A transition is a bit boundary: start the new bit here, and sample it in the middle.
		d->phase = 0;
		d->sampled = false;
	}
	d->last_sign = sign;
	d->have_last = true;

	d->phase += d->inc;
	if (!d->sampled && d->phase >= (1u << 15)) {
		d->sampled = true;
		dec_bit(rx, d, sign > 0);
	}
	if (d->phase >= (1u << 16)) {
		d->phase -= 1u << 16;
		d->sampled = false;
	}
}

void pocsag_rx_init(pocsag_rx_t *rx, uint16_t baud, pocsag_msg_cb cb, void *ctx)
{
	memset(rx, 0, sizeof(*rx));
	rx->rate = POCSAG_RATE;
	rx->cb = cb;
	rx->ctx = ctx;
	if (baud == 0) {
		rx->n = 3;
		rx->d[0].baud = POCSAG_BAUD_512;
		rx->d[1].baud = POCSAG_BAUD_1200;
		rx->d[2].baud = POCSAG_BAUD_2400;
	} else {
		rx->n = 1;
		rx->d[0].baud = baud;
	}
	for (uint8_t i = 0; i < rx->n; i++) {
		rx->d[i].inc = ((uint32_t)rx->d[i].baud << 16) / rx->rate;
	}
}

void pocsag_rx_feed(pocsag_rx_t *rx, const int16_t *samples, size_t n)
{
	rx->samples += (uint32_t)n;
	for (size_t i = 0; i < n; i++) {
		for (uint8_t k = 0; k < rx->n; k++) {
			feed_one(rx, &rx->d[k], samples[i]);
		}
	}
}
