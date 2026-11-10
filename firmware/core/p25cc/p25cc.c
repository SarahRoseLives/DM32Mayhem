// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// P25 control channel frames (p25cc.h). The frame layout and TSBK field packing follow SiteSim (MIT, Copyright (c) 2026
// SarahRoseLives) and TIA-102: a 48-bit frame sync, the BCH(64,16) coded NID, the 12-byte TSBK through its CRC-CCITT,
// rate-1/2 trellis code and data interleave, and the outbound status symbols every 35 dibits.
#include "config.h"
#if CONFIG_P25CC_TX
#include <stdbool.h>
#include <string.h>
#include "p25cc/p25cc.h"

#define DUID_TSBK		0x07u
#define FSW			0x5575F5FF77FFULL
#define SSC_VOICE_DATA_REG	0x70u	// voice + data + registration

static const uint64_t bch_matrix[16] = {
	0x8000cd930bdd3b2aULL, 0x4000ab5a8e33a6beULL, 0x2000983e4cc4e874ULL, 0x10004c1f2662743aULL,
	0x0800eb9c98ec0136ULL, 0x0400b85d47ab3bb0ULL, 0x02005c2ea3d59dd8ULL, 0x01002e1751eaceecULL,
	0x0080170ba8f56776ULL, 0x0040c616dfa78890ULL, 0x0020630b6fd3c448ULL, 0x00103185b7e9e224ULL,
	0x000818c2dbf4f112ULL, 0x0004c1f2662743a2ULL, 0x0002ad6a38ce9afbULL, 0x00019b2617ba7657ULL,
};

static const uint8_t trellis[4][4][2] = {
	{ { 0, 2 }, { 3, 0 }, { 0, 1 }, { 3, 3 } },
	{ { 3, 2 }, { 0, 0 }, { 3, 1 }, { 0, 3 } },
	{ { 2, 1 }, { 1, 3 }, { 2, 2 }, { 1, 0 } },
	{ { 1, 1 }, { 2, 3 }, { 1, 2 }, { 2, 0 } },
};

static uint64_t bch_encode(uint16_t data)
{
	uint64_t cw = 0;
	for (int i = 0; i < 16; i++) {
		if (data & (0x8000u >> i)) {
			cw ^= bch_matrix[i];
		}
	}
	return cw;
}

static uint16_t crc_ccitt(uint16_t high, uint64_t low)
{
	uint32_t crc = 0;
	for (int i = 15; i >= 0; i--) {
		crc <<= 1;
		if (((crc >> 16) ^ ((high >> i) & 1u)) & 1u) {
			crc ^= 0x1021;
		}
	}
	for (int i = 63; i >= 0; i--) {
		crc <<= 1;
		if (((crc >> 16) ^ (uint32_t)((low >> i) & 1u)) & 1u) {
			crc ^= 0x1021;
		}
	}
	return (uint16_t)((crc & 0xFFFFu) ^ 0xFFFFu);
}

static void build_tsbk(uint8_t out[12], bool last, uint8_t opcode, uint8_t mfid, uint64_t args)
{
	uint16_t high = (uint16_t)((last ? 1u << 15 : 0u) | ((uint16_t)opcode << 8) | mfid);
	uint16_t crc = crc_ccitt(high, args);
	out[0] = (uint8_t)(high >> 8);
	out[1] = (uint8_t)high;
	for (int i = 0; i < 8; i++) {
		out[2 + i] = (uint8_t)(args >> (56 - 8 * i));
	}
	out[10] = (uint8_t)(crc >> 8);
	out[11] = (uint8_t)crc;
}

// IDEN_UP for the control channel's own channel: identifier 1, 12.5 kHz bandwidth and spacing, no offset
static void tsbk_iden_up(uint8_t o[12], uint32_t freq_hz)
{
	uint64_t args = ((uint64_t)1u << 60) | ((uint64_t)100u << 51) | ((uint64_t)100u << 32) |
			(((uint64_t)freq_hz / 5u) & 0xFFFFFFFFu);
	build_tsbk(o, true, 0x3D, 0x00, args);
}

static void tsbk_net_status(uint8_t o[12], uint32_t wacn, uint16_t sysid)
{
	uint64_t ch = ((uint64_t)1u << 12);	// identifier 1, channel 0 (the control channel)
	uint64_t args = ((uint64_t)(wacn & 0xFFFFFu) << 36) | ((uint64_t)(sysid & 0xFFFu) << 24) |
			(ch << 8) | SSC_VOICE_DATA_REG;
	build_tsbk(o, true, 0x3B, 0x00, args);
}

static void tsbk_rfss_status(uint8_t o[12], uint16_t sysid, uint8_t rfss, uint8_t site)
{
	uint64_t ch = ((uint64_t)1u << 12);
	uint64_t active = (1ULL << 52) | (1ULL << 53);	// network active and up to date
	uint64_t args = ((uint64_t)(sysid & 0xFFFu) << 40) | active | ((uint64_t)rfss << 32) |
			((uint64_t)site << 24) | (ch << 8) | SSC_VOICE_DATA_REG;
	build_tsbk(o, true, 0x3A, 0x00, args);
}

static void tsbk_grp_grant(uint8_t o[12], uint16_t vch, uint16_t tg)
{
	uint64_t ch = ((uint64_t)1u << 12) | (uint64_t)(vch & 0xFFFu);
	uint64_t args = (ch << 48) | ((uint64_t)0x02u << 40) | ((uint64_t)tg << 24);
	build_tsbk(o, true, 0x00, 0x00, args);
}

static void trellis_encode(const uint8_t in[48], uint8_t out[98])
{
	uint8_t state = 0;
	for (int i = 0; i < 49; i++) {
		uint8_t d = i < 48 ? in[i] : 0;
		out[i * 2] = trellis[state][d][0];
		out[i * 2 + 1] = trellis[state][d][1];
		state = d;
	}
}

static void data_interleave(const uint8_t in[98], uint8_t out[98])
{
	int o = 0;
	for (int j = 0; j < 97; j += 8) {
		out[o++] = in[j];
		out[o++] = in[j + 1];
	}
	for (int i = 2; i < 7; i += 2) {
		for (int j = 0; j < 89; j += 8) {
			out[o++] = in[i + j];
			out[o++] = in[i + j + 1];
		}
	}
}

static void put_dibit(uint8_t *d, int *n, uint8_t v)
{
	d[(*n)++] = (uint8_t)(v & 3u);
}

static int build_frame(uint16_t nac, const uint8_t tsbk[12], uint8_t *out)
{
	uint8_t raw[160];
	int n = 0;
	for (int i = 0; i < 24; i++) {
		put_dibit(raw, &n, (uint8_t)((FSW >> (46 - 2 * i)) & 3u));
	}
	uint64_t nid = bch_encode((uint16_t)((nac << 4) | DUID_TSBK));
	for (int i = 0; i < 32; i++) {
		put_dibit(raw, &n, (uint8_t)((nid >> (62 - 2 * i)) & 3u));
	}
	uint8_t in[48];
	for (int i = 0; i < 12; i++) {
		in[i * 4] = (uint8_t)((tsbk[i] >> 6) & 3u);
		in[i * 4 + 1] = (uint8_t)((tsbk[i] >> 4) & 3u);
		in[i * 4 + 2] = (uint8_t)((tsbk[i] >> 2) & 3u);
		in[i * 4 + 3] = (uint8_t)(tsbk[i] & 3u);
	}
	uint8_t enc[98], ilv[98];
	trellis_encode(in, enc);
	data_interleave(enc, ilv);
	for (int i = 0; i < 98; i++) {
		put_dibit(raw, &n, ilv[i]);
	}
	// One status symbol every 35 dibits: the first marks the inbound slot start, the rest are between slots
	int num = (n + 34) / 35, rem = num, oi = 0, i = 1, ss = 0;
	for (int k = 0; k < n; k++) {
		out[oi++] = raw[k];
		if (i % 35 == 0 && rem > 0) {
			out[oi++] = ss++ == 0 ? 0x03u : 0x02u;
			rem--;
		}
		i++;
	}
	while (rem > 0) {
		out[oi++] = 0;
		if (i % 35 == 0) {
			out[oi++] = ss++ == 0 ? 0x03u : 0x02u;
			rem--;
		}
		i++;
	}
	return oi;
}

uint16_t p25cc_build(const p25cc_cfg_t *c, uint8_t *out, uint16_t cap_bytes)
{
	uint8_t d[1024];
	int n = 0;
	uint8_t tsbk[12];

	tsbk_iden_up(tsbk, c->freq_hz);
	n += build_frame(c->nac, tsbk, d + n);
	tsbk_net_status(tsbk, c->wacn, c->sysid);
	n += build_frame(c->nac, tsbk, d + n);
	tsbk_rfss_status(tsbk, c->sysid, c->rfss, c->site);
	n += build_frame(c->nac, tsbk, d + n);
	if (c->vch != 0) {
		tsbk_grp_grant(tsbk, c->vch, c->tg);
		n += build_frame(c->nac, tsbk, d + n);
	}
	while (n % 4 != 0) {
		d[n++] = 0;
	}
	uint16_t bytes = (uint16_t)(n / 4);
	if (bytes > cap_bytes) {
		return 0;
	}
	for (uint16_t k = 0; k < bytes; k++) {
		out[k] = (uint8_t)((d[k * 4] << 6) | (d[k * 4 + 1] << 4) | (d[k * 4 + 2] << 2) | d[k * 4 + 3]);
	}
	return bytes;
}

#endif	// CONFIG_P25CC_TX
