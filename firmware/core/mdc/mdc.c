// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// MDC-1200 transmit encoding (mdc.h). The codeword is CRC-16 over its four data bytes, then a rate-1/2 K=7 convolutional
// code and a 112-bit interleave; the whole burst is NRZI before the AFSK tones. The constants and the bit order are the
// MDC-1200 signaling format.
#include "config.h"
#if CONFIG_MDC_TX
#include <string.h>
#include "mdc/mdc.h"

#define PREAMBLE_BLOCKS	3
#define PREAMBLE_BYTES	7
#define SYNC_BYTES	5
#define CW_BYTES	14
#define STREAM_MAX	(PREAMBLE_BLOCKS * PREAMBLE_BYTES + SYNC_BYTES + 2 * CW_BYTES)

static const uint8_t sync_word[SYNC_BYTES] = { 0x07, 0x09, 0x2A, 0x44, 0x6F };

static uint8_t rev8(uint8_t b)
{
	b = (uint8_t)(((b & 0xF0u) >> 4) | ((b & 0x0Fu) << 4));
	b = (uint8_t)(((b & 0xCCu) >> 2) | ((b & 0x33u) << 2));
	b = (uint8_t)(((b & 0xAAu) >> 1) | ((b & 0x55u) << 1));
	return b;
}

static uint16_t rev16(uint16_t v)
{
	uint16_t r = 0;
	for (int i = 0; i < 16; i++) {
		r = (uint16_t)((r << 1) | (v & 1u));
		v >>= 1;
	}
	return r;
}

// CRC-16 (CCITT polynomial) over the four data bytes, each byte taken bit-reversed, the result reversed and inverted
static uint16_t mdc_crc(const uint8_t *data, int len)
{
	uint16_t crc = 0;
	for (int i = 0; i < len; i++) {
		uint16_t c = rev8(data[i]);
		for (uint16_t j = 0x80u; j != 0; j >>= 1) {
			uint16_t bit = crc & 0x8000u;
			crc = (uint16_t)(crc << 1);
			if (c & j) {
				bit ^= 0x8000u;
			}
			if (bit) {
				crc ^= 0x1021u;
			}
		}
	}
	crc = rev16(crc);
	crc ^= 0xFFFFu;
	return crc;
}

// CRC + convolutional encode + interleave, in place, for the 14-byte codeword (4 data, 2 CRC, 1 zero, 7 coded)
static void pack_codeword(uint8_t cw[CW_BYTES])
{
	uint16_t crc = mdc_crc(cw, 4);
	cw[4] = (uint8_t)(crc & 0xFFu);
	cw[5] = (uint8_t)(crc >> 8);
	cw[6] = 0;
	uint8_t csr[7] = { 0 };
	for (int i = 0; i < 7; i++) {
		cw[i + 7] = 0;
		for (int j = 0; j <= 7; j++) {
			for (int k = 6; k > 0; k--) {
				csr[k] = csr[k - 1];
			}
			csr[0] = (uint8_t)((cw[i] >> j) & 1u);
			int b = csr[0] + csr[2] + csr[5] + csr[6];
			cw[i + 7] = (uint8_t)(cw[i + 7] | (uint8_t)((b & 1) << j));
		}
	}
	uint8_t lbits[112];
	memset(lbits, 0, sizeof lbits);
	int l = 0, m = 0;
	for (int i = 0; i < 14; i++) {
		for (int j = 0; j <= 7; j++) {
			lbits[l] = (uint8_t)((cw[i] >> j) & 1u);
			l += 16;
			if (l > 111) {
				m++;
				l = m;
			}
		}
	}
	l = 0;
	for (int i = 0; i < 14; i++) {
		cw[i] = 0;
		for (int j = 7; j >= 0; j--) {
			if (lbits[l]) {
				cw[i] = (uint8_t)(cw[i] | (1u << j));
			}
			l++;
		}
	}
}

static int build_single(uint8_t op, uint8_t arg, uint8_t hi, uint8_t lo, uint8_t *s)
{
	int idx = 0;
	for (int i = 0; i < PREAMBLE_BLOCKS; i++) {
		for (int j = 0; j < PREAMBLE_BYTES; j++) {
			s[idx++] = 0xAA;
		}
	}
	for (int j = 0; j < SYNC_BYTES; j++) {
		s[idx++] = sync_word[j];
	}
	uint8_t cw[CW_BYTES];
	memset(cw, 0, sizeof cw);
	cw[0] = op;
	cw[1] = arg;
	cw[2] = hi;
	cw[3] = lo;
	pack_codeword(cw);
	for (int j = 0; j < CW_BYTES; j++) {
		s[idx++] = cw[j];
	}
	return idx;
}

static int build_double(uint8_t op1, uint8_t arg1, uint8_t hi1, uint8_t lo1, uint8_t op2, uint8_t arg2, uint8_t hi2,
			uint8_t lo2, uint8_t *s)
{
	int idx = build_single(op1, arg1, hi1, lo1, s);	// preamble, sync and the first codeword
	uint8_t cw[CW_BYTES];
	memset(cw, 0, sizeof cw);
	cw[0] = op2;
	cw[1] = arg2;
	cw[2] = hi2;
	cw[3] = lo2;
	pack_codeword(cw);
	for (int j = 0; j < CW_BYTES; j++) {
		s[idx++] = cw[j];
	}
	return idx;
}

bool mdc_op_is_double(uint8_t op)
{
	return op == 0x80 || op == 0x81 || op == 0x82;
}

uint16_t mdc_build(const mdc_cfg_t *c, uint8_t *out, uint16_t cap_bits)
{
	uint8_t s[STREAM_MAX];
	int n;
	if (mdc_op_is_double(c->op)) {
		// The data opcode first (the request), then the caller's op with the source ID
		n = build_double(0x35, 0x89, c->id_hi, c->id_lo, c->op, 0x0D, c->src_hi, c->src_lo, s);
	} else {
		n = build_single(c->op, c->arg, c->id_hi, c->id_lo, s);
	}
	if ((uint32_t)n * 8u > cap_bits) {
		return 0;
	}
	// NRZI, then reversed per byte: afsk_mod reads a byte's bits LSB first, the on-air order is MSB first
	uint8_t prev = 0;
	for (int i = 0; i < n; i++) {
		uint8_t raw = s[i], nrzi = 0;
		for (int bit = 7; bit >= 0; bit--) {
			uint8_t rb = (uint8_t)((raw >> bit) & 1u);
			uint8_t nb = (uint8_t)(rb ^ prev);
			prev = rb;
			nrzi = (uint8_t)((nrzi << 1) | nb);
		}
		out[i] = rev8(nrzi);
	}
	return (uint16_t)(n * 8);
}

#endif	// CONFIG_MDC_TX
