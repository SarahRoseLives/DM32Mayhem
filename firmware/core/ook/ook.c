// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// OOK TX bit streams (ook.h). The de Bruijn sequence uses the Fredricksen-Kessler-Maiorana (prefer-highest) recursion,
// which yields each Lyndon word of the order once; the sequence is the concatenation of all of them.
#include "config.h"
#if CONFIG_OOK_TX
#include <stdbool.h>
#include <string.h>
#include "ook/ook.h"

#define OOK_MAX_ORDER	10	// 2^10 symbols x 4 bits = 4096 bits, the burst buffer

static void put_bit(uint8_t *out, uint16_t i, bool v)
{
	if (v) {
		out[i >> 3] |= (uint8_t)(1u << (7u - (i & 7u)));
	}
}

static int hex_nibble(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

uint16_t ook_hex(const char *s, uint8_t *out, uint16_t cap_bits)
{
	memset(out, 0, (size_t)((cap_bits + 7u) / 8u));
	uint16_t bit = 0;
	for (; *s != '\0'; s++) {
		int n = hex_nibble(*s);
		if (n < 0)
			continue;
		if (bit + 4u > cap_bits) {
			return 0;
		}
		for (int b = 3; b >= 0; b--) {
			put_bit(out, bit++, (n >> b) & 1);
		}
	}
	return bit;
}

static bool db_step(uint8_t *a, uint8_t t, uint8_t p, uint8_t n, uint8_t *out, uint16_t cap, uint16_t *bit)
{
	if (t > n) {
		if (n % p == 0) {
			for (uint8_t j = 1; j <= p; j++) {
				uint8_t sym = a[j] ? 0x0Eu : 0x08u;	// 1110 / 1000
				for (int b = 3; b >= 0; b--) {
					if (*bit + 1u > cap) {
						return false;
					}
					put_bit(out, (*bit)++, (sym >> b) & 1);
				}
			}
		}
		return true;
	}
	a[t] = a[t - p];
	if (!db_step(a, (uint8_t)(t + 1u), p, n, out, cap, bit)) {
		return false;
	}
	for (uint8_t j = (uint8_t)(a[t - p] + 1u); j < 2u; j++) {
		a[t] = j;
		if (!db_step(a, (uint8_t)(t + 1u), t, n, out, cap, bit)) {
			return false;
		}
	}
	return true;
}

uint16_t ook_debruijn(uint8_t order, uint8_t *out, uint16_t cap_bits)
{
	if (order < 1 || order > OOK_MAX_ORDER) {
		return 0;
	}
	uint32_t need = ((uint32_t)1u << order) * 4u;
	if (need > cap_bits) {
		return 0;
	}
	memset(out, 0, (size_t)((cap_bits + 7u) / 8u));
	uint8_t a[OOK_MAX_ORDER + 2];
	memset(a, 0, sizeof a);
	uint16_t bit = 0;
	if (!db_step(a, 1, 1, order, out, cap_bits, &bit)) {
		return 0;
	}
	return bit;
}

#endif	// CONFIG_OOK_TX
