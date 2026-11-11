// SPDX-License-Identifier: MIT
// POCSAG: the encoder's page against the receiver, from synthetic 8 kHz discriminator audio.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pocsag/pocsag.h"

static pocsag_rx_t rx;
static pocsag_msg_t last;
static int got;

static void on_msg(const pocsag_msg_t *m, void *ctx)
{
	(void)ctx;
	last = *m;
	got++;
}

static size_t synth(const uint8_t *bits, uint16_t nbits, uint16_t baud, int16_t *s, size_t cap)
{
	size_t pos = 0;
	for (uint32_t i = 0; i < nbits; i++) {
		uint32_t end = (uint32_t)(((uint64_t)(i + 1u) * 8000u) / baud);
		int16_t v = ((bits[i >> 3] >> (7u - (i & 7u))) & 1u) ? 12000 : -12000;
		while (pos < end && pos < cap) {
			s[pos++] = v;
		}
	}
	return pos;
}

static int roundtrip(uint32_t capcode, uint8_t func, bool alpha, const char *text, uint16_t baud, bool invert,
		     int flip_twice)
{
	uint8_t bits[512];
	uint16_t nbits = 0;
	pocsag_tx_cfg_t cfg = { .capcode = capcode, .func = func, .alpha = alpha, .baud = baud, .repeat = 1,
			       .invert = invert, .text = text };
	if (!pocsag_build(&cfg, bits, (uint16_t)sizeof bits * 8u, &nbits)) {
		printf("FAIL build cap %u text %s\n", (unsigned)capcode, text);
		return 1;
	}
	if (flip_twice) {
		// The first codeword after the sync (the address) at bit 576 + 32; a message codeword follows. Flip two bits
		// of the message codeword and let the BCH put them back.
		uint32_t at = 576 + 32 + 32 + 5;
		if (at + 20 < nbits) {
			bits[(at + 0) >> 3] ^= (uint8_t)(1u << (7u - ((at + 0) & 7u)));
			bits[(at + 19) >> 3] ^= (uint8_t)(1u << (7u - ((at + 19) & 7u)));
		}
	}
	static int16_t audio[8000 * 30];
	size_t n = synth(bits, nbits, baud, audio, sizeof audio / sizeof audio[0]);
	if (n == 0 || n >= sizeof audio / sizeof audio[0]) {
		printf("FAIL synth\n");
		return 1;
	}
	got = 0;
	memset(&last, 0, sizeof last);
	pocsag_rx_init(&rx, 0, on_msg, NULL);
	for (size_t i = 0; i < n; i += 80) {
		size_t k = n - i < 80 ? n - i : 80;
		pocsag_rx_feed(&rx, audio + i, k);
	}
	int bad = 0;
	if (got == 0) {
		printf("FAIL no message (cap %u, %u baud, text '%s')\n", (unsigned)capcode, (unsigned)baud, text);
		bad = 1;
	} else if (last.capcode != capcode || last.func != func || last.alpha != alpha || strcmp(last.text, text) != 0) {
		printf("FAIL got cap %u func %u alpha %d baud %u text '%s' (wanted cap %u func %u alpha %d text '%s')\n",
		       (unsigned)last.capcode, (unsigned)last.func, last.alpha, (unsigned)last.baud, last.text,
		       (unsigned)capcode, (unsigned)func, alpha, text);
		bad = 1;
	} else if (!invert && last.baud != baud) {
		printf("FAIL baud %u (wanted %u)\n", (unsigned)last.baud, (unsigned)baud);
		bad = 1;
	}
	return bad;
}

int main(void)
{
	int bad = 0;
	bad += roundtrip(123456u, 3, true, "HELLO DAPNET 123", 1200, false, 0);
	bad += roundtrip(123456u, 3, true, "HELLO 512", 512, false, 0);
	bad += roundtrip(123456u, 3, true, "HELLO 2400", 2400, false, 0);
	bad += roundtrip(0x12340u, 3, true, "INVERTED", 1200, true, 0);
	bad += roundtrip(0x12340u, 3, true, "BCH FIX", 1200, false, 1);
	bad += roundtrip(123456u, 0, false, "12345", 1200, false, 0);
	if (bad == 0) {
		printf("pocsagTest: all passed\n");
	}
	return bad ? 1 : 0;
}
