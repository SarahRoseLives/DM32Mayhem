// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// RTTY ITA2 encoding (rtty.h). Each character is one start bit (0), five data bits LSB first and two stop bits (1).
// Letters and figures share the five-bit codes; LTRS (0x1F) and FIGS (0x1B) switch between them.
#include "config.h"
#if CONFIG_RTTY_TX
#include <stdbool.h>
#include <string.h>
#include "rtty/rtty.h"

#define BIT_LTRS	0x1Fu
#define BIT_FIGS	0x1Bu
#define BIT_SPACE	0x04u
#define BIT_CR		0x08u
#define BIT_LF		0x02u

enum { SHIFT_LTRS, SHIFT_FIGS };

// Letters A-Z, then the figure codes for 0-9 and common punctuation
static int rtty_code(char c, uint8_t *shift)
{
	*shift = SHIFT_LTRS;
	if (c >= 'a' && c <= 'z') {
		c = (char)(c - 'a' + 'A');
	}
	switch (c) {
	case 'A': return 0x03; case 'B': return 0x19; case 'C': return 0x0E; case 'D': return 0x09;
	case 'E': return 0x01; case 'F': return 0x0D; case 'G': return 0x1A; case 'H': return 0x14;
	case 'I': return 0x06; case 'J': return 0x0B; case 'K': return 0x0F; case 'L': return 0x12;
	case 'M': return 0x1C; case 'N': return 0x0C; case 'O': return 0x18; case 'P': return 0x16;
	case 'Q': return 0x17; case 'R': return 0x0A; case 'S': return 0x05; case 'T': return 0x10;
	case 'U': return 0x07; case 'V': return 0x1E; case 'W': return 0x13; case 'X': return 0x1D;
	case 'Y': return 0x15; case 'Z': return 0x11;
	case ' ': return BIT_SPACE;
	case '\r': return BIT_CR;
	case '\n': return BIT_LF;
	default:
		break;
	}
	*shift = SHIFT_FIGS;
	switch (c) {
	case '0': return 0x16; case '1': return 0x17; case '2': return 0x13; case '3': return 0x01;
	case '4': return 0x0A; case '5': return 0x10; case '6': return 0x15; case '7': return 0x07;
	case '8': return 0x06; case '9': return 0x18;
	case '-': return 0x03; case '.': return 0x1C; case ',': return 0x0C; case '?': return 0x19;
	case '/': return 0x1D; case ':': return 0x0E; case ';': return 0x1E; case '(': return 0x0F;
	case ')': return 0x12; case '$': return 0x09; case '\'': return 0x05; case '!': return 0x0D;
	case '&': return 0x1A; case '#': return 0x14; case '+': return 0x11; case '=': return 0x07;
	default: return BIT_SPACE;	// space is common to both shifts
	}
}

static bool put_bit(uint8_t *out, uint16_t cap_bits, uint16_t *n, bool v)
{
	if (*n >= cap_bits) {
		return false;
	}
	if (v) {
		out[*n >> 3] |= (uint8_t)(1u << (*n & 7u));
	}
	(*n)++;
	return true;
}

static bool put_char(uint8_t *out, uint16_t cap_bits, uint16_t *n, uint8_t code)
{
	if (!put_bit(out, cap_bits, n, false)) {	// start
		return false;
	}
	for (int i = 0; i < 5; i++) {			// data, LSB first
		if (!put_bit(out, cap_bits, n, (code >> i) & 1u)) {
			return false;
		}
	}
	return put_bit(out, cap_bits, n, true) && put_bit(out, cap_bits, n, true);	// two stop bits
}

uint16_t rtty_encode(const char *text, uint8_t *out, uint16_t cap_bits)
{
	memset(out, 0, (size_t)((cap_bits + 7u) / 8u));
	uint16_t n = 0;
	int shift = SHIFT_LTRS;
	if (!put_char(out, cap_bits, &n, BIT_LTRS)) {
		return 0;
	}
	for (const char *p = text; *p != '\0'; p++) {
		uint8_t code_shift;
		int code = rtty_code(*p, &code_shift);
		if (code_shift == SHIFT_FIGS && shift != SHIFT_FIGS) {
			if (!put_char(out, cap_bits, &n, BIT_FIGS)) {
				return 0;
			}
			shift = SHIFT_FIGS;
		} else if (code_shift == SHIFT_LTRS && shift != SHIFT_LTRS) {
			if (!put_char(out, cap_bits, &n, BIT_LTRS)) {
				return 0;
			}
			shift = SHIFT_LTRS;
		}
		if (!put_char(out, cap_bits, &n, (uint8_t)code)) {
			return 0;
		}
	}
	return n;
}

#endif	// CONFIG_RTTY_TX
