// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// ACARS receive (acars.h). The demodulator is the classic VCO / matched-filter MSK receiver: a 1800 Hz VCO mixes the
// audio to baseband, a bit clock at 3*pi/2 per bit samples a half-cosine matched filter at 600 Hz, the alternating
// in-phase/quadrature decision gives the bit, and a PLL pulls the bit clock from the decisions. The bits are MSB
// first; the frame is SYN SYN SOH ... ETX/ETB CRC-16 with odd parity on every character.
//
// Tested against the sigidwiki ACARS IQ sample at 8 kHz (five frames), and against the same audio at 12.5/24 kHz.
#include <string.h>
#include <math.h>
#include "acars/acars.h"

#define ACARS_SYN	0x16
#define ACARS_SYN_INV	0xE9u
#define ACARS_SOH	0x01
#define ACARS_STX	0x02
#define ACARS_ETX	0x03
#define ACARS_ETB	0x17
#define ACARS_DLE	0x7F

#define PLL_G		38e-4f
#define PLL_C		0.52f

enum { ST_WSYN, ST_SYN2, ST_SOH, ST_TXT, ST_CRC1, ST_CRC2 };

static const int16_t sin256[256] = {
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

static uint8_t popcount8(uint8_t v)
{
	uint8_t n = 0;
	for (; v; v &= (uint8_t)(v - 1u)) {
		n++;
	}
	return n;
}

static uint16_t crc_byte(uint16_t crc, uint8_t c)
{
	crc ^= c;
	for (int i = 0; i < 8; i++) {
		crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0x8408u) : (uint16_t)(crc >> 1);
	}
	return crc;
}

static char pr(uint8_t c)
{
	c &= 0x7f;
	return (c >= 32 && c < 127) ? (char)c : '.';
}

static void emit(acars_rx_t *d)
{
	acars_msg_t m;
	memset(&m, 0, sizeof m);
	if (d->len < 13) {
		return;
	}
	m.mode = pr(d->txt[0]);
	int j = 0;
	for (int i = 1; i < 8 && j < ACARS_ADDR_MAX - 1; i++) {
		char c = pr(d->txt[i]);
		if (c != '.' && c != ' ') {
			m.addr[j++] = c;
		}
	}
	m.addr[j] = '\0';
	uint8_t a = (uint8_t)(d->txt[8] & 0x7f);
	m.ack = (a == 0x15) ? '!' : pr(a);
	m.label[0] = pr(d->txt[9]);
	m.label[1] = (d->txt[10] & 0x7f) == 0x7f ? 'd' : pr(d->txt[10]);
	m.label[2] = '\0';
	m.bid = pr(d->txt[11]);
	m.err = d->perr;

	// The block: byte 12 is the text-start marker; the text runs to the ETX/ETB at len-1
	int start = 13;
	int end = (int)d->len - 1;	// the ETX/ETB
	if (start < end && (d->txt[12] & 0x7f) != ACARS_ETX) {
		bool down = m.bid >= '0' && m.bid <= '9';
		char *o = m.text;
		size_t cap = sizeof m.text;
		size_t n = 0;
		if (down && end - start >= 10) {
			// downlink: four-character message number, then six-character flight id
			for (int i = start + 4; i < start + 10 && n + 1 < cap; i++) {
				o[n++] = pr(d->txt[i]);
			}
			if (n + 1 < cap) {
				o[n++] = ' ';
			}
			for (int i = start; i < start + 4 && n + 1 < cap; i++) {
				o[n++] = pr(d->txt[i]);
			}
			if (n + 1 < cap) {
				o[n++] = ' ';
			}
			start += 10;
		}
		for (int i = start; i < end && n + 1 < cap; i++) {
			o[n++] = pr(d->txt[i]);
		}
		o[n] = '\0';
	}
	m.level = (int16_t)(10.0f * log10f(d->level_sum / (d->bit_count != 0 ? (float)d->bit_count : 1.0f) + 1e-9f));
	d->frames++;
	if (d->fn != NULL) {
		d->fn(&m, d->ctx);
	}
}

static void decode_byte(acars_rx_t *d, uint8_t r)
{
	switch (d->state) {
	case ST_WSYN:
		if (r == ACARS_SYN) {
			d->state = ST_SYN2;
			d->nbits = 8;
			return;
		}
		if (r == ACARS_SYN_INV) {
			d->msk_s ^= 2;	// the receiver's polarity is inverted
			d->state = ST_SYN2;
			d->nbits = 8;
			return;
		}
		d->nbits = 1;
		return;
	case ST_SYN2:
		if (r == ACARS_SYN) {
			d->state = ST_SOH;
			d->nbits = 8;
			return;
		}
		if (r == ACARS_SYN_INV) {
			d->msk_s ^= 2;
			d->nbits = 8;
			return;
		}
		d->state = ST_WSYN;
		d->nbits = 1;
		return;
	case ST_SOH:
		if (r == ACARS_SOH) {
			d->state = ST_TXT;
			d->len = 0;
			d->perr = 0;
			d->level_sum = 0;
			d->bit_count = 0;
			d->nbits = 8;
			return;
		}
		d->state = ST_WSYN;
		d->nbits = 1;
		return;
	case ST_TXT:
		if (d->len < sizeof d->txt) {
			d->txt[d->len++] = r;
		}
		if ((popcount8(r) & 1u) == 0) {
			d->perr++;
		}
		if ((r & 0x7f) == ACARS_ETX || (r & 0x7f) == ACARS_ETB) {
			d->state = ST_CRC1;
			d->nbits = 8;
			return;
		}
		if (d->len >= sizeof d->txt) {
			d->state = ST_WSYN;
			d->nbits = 1;
			return;
		}
		d->nbits = 8;
		return;
	case ST_CRC1:
		d->crc[0] = r;
		if ((popcount8(r) & 1u) == 0) {
			d->perr++;
		}
		d->state = ST_CRC2;
		d->nbits = 8;
		return;
	case ST_CRC2: {
		d->crc[1] = r;
		if ((popcount8(r) & 1u) == 0) {
			d->perr++;
		}
		uint16_t crc = 0;
		for (uint16_t i = 0; i < d->len; i++) {
			crc = crc_byte(crc, d->txt[i]);
		}
		crc = crc_byte(crc, d->crc[0]);
		crc = crc_byte(crc, d->crc[1]);
		if (crc == 0) {
			emit(d);
		} else {
			d->errors++;
		}
		d->state = ST_WSYN;
		d->nbits = 1;
		return;
	}
	default:
		d->state = ST_WSYN;
		d->nbits = 1;
		return;
	}
}

static void putbit(acars_rx_t *d, float v)
{
	d->outbits >>= 1;
	if (v > 0) {
		d->outbits |= 0x80;
	}
	if (--d->nbits == 0) {
		decode_byte(d, d->outbits);
	}
}

bool acars_rx_init(acars_rx_t *d, uint32_t rate, acars_msg_fn fn, void *ctx)
{
	memset(d, 0, sizeof *d);
	if (rate < 7200u) {
		return false;
	}
	uint32_t flen = rate / 1200u + 1u;
	if (flen > ACARS_FLEN_MAX) {
		return false;
	}
	d->rate = rate;
	d->flen = (uint16_t)flen;
	d->fleno = (uint16_t)(flen * ACARS_OV + 1u);
	d->inc0 = 6.28318530718f * 1800.0f / (float)rate;
	for (uint32_t i = 0; i < d->fleno; i++) {
		float c = cosf(6.28318530718f * 600.0f / (float)rate / ACARS_OV * ((float)i - (float)(d->fleno - 1) / 2.0f));
		d->h[i] = c > 0.0f ? c : 0.0f;
	}
	d->state = ST_WSYN;
	d->nbits = 8;
	// A 500 Hz first-order high pass, as the sample audio had: the FM/AM tap carries a DC and low-frequency the
	// MSK demodulator does not want
	d->hp_a = (float)rate / ((float)rate + 6.28318530718f * 500.0f);
	d->fn = fn;
	d->ctx = ctx;
	return true;
}

void acars_rx_set_fo(acars_rx_t *d, float hz)
{
	d->fo = (d->rate != 0) ? 6.28318530718f * hz / (float)d->rate : 0.0f;
}

void acars_rx_feed(acars_rx_t *d, const int16_t *s, size_t n)
{
	for (; n != 0; n--, s++) {
		float xin = (float)*s;
		float x = d->hp_a * (d->hp_y + xin - d->hp_x);
		d->hp_x = xin;
		d->hp_y = x;
		float inc = d->inc0 + d->df;
		d->p += inc;
		if (d->p >= 6.28318530718f) {
			d->p -= 6.28318530718f;
		}
		float ph = d->p * (256.0f / 6.28318530718f);
		int idx0 = (int)ph;
		float frac = ph - (float)idx0;
		idx0 &= 255;
		int ic = (idx0 + 64) & 255;
		float si = sin256[idx0] + (sin256[(idx0 + 1) & 255] - sin256[idx0]) * frac;
		float co = sin256[ic] + (sin256[(ic + 1) & 255] - sin256[ic]) * frac;
		float c = co * (1.0f / 32768.0f), sn = si * (1.0f / 32768.0f);
		d->inb_re[d->idx] = x * c;
		d->inb_im[d->idx] = -x * sn;
		d->idx = (uint16_t)((d->idx + 1u) % d->flen);
		d->clk += inc;
		if (d->clk >= 4.71238898038f - inc * 0.5f) {	// 3*pi/2
			d->clk -= 4.71238898038f;
			float o = ACARS_OV * (d->clk / inc + 0.5f);
			int oi = (int)o;
			if (oi > ACARS_OV) {
				oi = ACARS_OV;
			}
			float vr = 0.0f, vi = 0.0f;
			for (uint16_t j = 0; j < d->flen; j++) {
				uint16_t k = (uint16_t)((j + d->idx) % d->flen);
				float h = d->h[oi + (int)j * ACARS_OV];
				vr += h * d->inb_re[k];
				vi += h * d->inb_im[k];
			}
			float lvl = sqrtf(vr * vr + vi * vi);
			float inv = 1.0f / (lvl + 1e-9f);
			vr *= inv;
			vi *= inv;
			float vo, dphi;
			if (d->msk_s & 1u) {
				vo = vi;
				dphi = (vo >= 0.0f) ? -vr : vr;
			} else {
				vo = vr;
				dphi = (vo >= 0.0f) ? vi : -vi;
			}
			d->level_sum += lvl * lvl * 0.25f;
			d->bit_count++;
			putbit(d, (d->msk_s & 2u) ? -vo : vo);
			d->msk_s++;
			d->df = PLL_C * d->df + (1.0f - PLL_C) * PLL_G * dphi;
		}
		d->samples++;
	}
}
