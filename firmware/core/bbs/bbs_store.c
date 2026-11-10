// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// BBS message store (bbs_store.h). One 512-byte record per message; a CRC in each slot lets a torn write be skipped.

#include <string.h>
#include "bbs/bbs_store.h"

#define O_MAGIC		0u
#define O_TYPE		4u
#define O_FLAGS		5u
#define O_BODYLEN	6u
#define O_MSGNO		8u
#define O_TIME		12u
#define O_FROM		16u
#define O_TO		26u
#define O_SUBJ		36u
#define O_BODY		76u
#define O_CRC		496u

// One shared record buffer: these functions run only in the radio task, and a 512-byte local at each level of the
// BBS call chain overflows that task's stack (R <n> after L crashed the radio).
static uint8_t io_rec[BBS_REC_SIZE];

static uint16_t get16(const uint8_t *p)
{
	return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static void put16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static uint32_t get32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static uint32_t bbs_crc32(const uint8_t *p, uint32_t n)
{
	uint32_t crc = 0xFFFFFFFFu;
	while (n--) {
		crc ^= *p++;
		for (int i = 0; i < 8; i++) {
			crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
		}
	}
	return crc ^ 0xFFFFFFFFu;
}

static void put_str(uint8_t *dst, size_t max, const char *src)
{
	size_t i = 0;
	if (src != NULL) {
		for (; i < max && src[i] != '\0'; i++) {
			dst[i] = (uint8_t)src[i];
		}
	}
	for (; i < max; i++) {
		dst[i] = 0;
	}
}

static void get_str(const uint8_t *src, size_t max, char *dst)
{
	size_t i;
	for (i = 0; i < max && src[i] != 0; i++) {
		dst[i] = (char)src[i];
	}
	dst[i] = '\0';
}

// A record loaded from a slot, checked for magic and CRC. Body length validated
static bool rec_decode(const uint8_t *r, bbs_msg_t *m, const uint8_t **body)
{
	if (get32(r) != BBS_REC_MAGIC || bbs_crc32(r, O_CRC) != get32(r + O_CRC)) {
		return false;
	}
	uint16_t len = get16(r + O_BODYLEN);
	if (len > BBS_BODY_MAX) {
		return false;
	}
	if (m != NULL) {
		memset(m, 0, sizeof(*m));
		m->type = r[O_TYPE];
		m->flags = r[O_FLAGS];
		m->msgno = get16(r + O_MSGNO);
		m->body_len = len;
		m->time = get32(r + O_TIME);
		get_str(r + O_FROM, BBS_FROM_MAX, m->from);
		get_str(r + O_TO, BBS_TO_MAX, m->to);
		get_str(r + O_SUBJ, BBS_SUBJECT_MAX, m->subject);
	}
	if (body != NULL) {
		*body = r + O_BODY;
	}
	return true;
}

static bool read_slot(const bbs_store_t *s, uint16_t slot, uint8_t *rec)
{
	return s->be.read(s->be.ctx, s->be.base + (uint32_t)slot * BBS_REC_SIZE, rec, BBS_REC_SIZE) == 0;
}

static bool write_slot(const bbs_store_t *s, uint16_t slot, const uint8_t *rec)
{
	return s->be.write(s->be.ctx, s->be.base + (uint32_t)slot * BBS_REC_SIZE, rec, BBS_REC_SIZE) == 0;
}

void bbs_store_init(bbs_store_t *s, const bbs_store_backend_t *be)
{
	memset(s, 0, sizeof(*s));
	s->be = *be;
	s->next_msgno = 1;
	for (uint16_t slot = 0; slot < s->be.slots && s->count < BBS_MAX_MSGS; slot++) {
		if (!read_slot(s, slot, io_rec) || !rec_decode(io_rec, NULL, NULL)) {
			continue;
		}
		if ((io_rec[O_FLAGS] & BBS_FLAG_KILLED) != 0) {
			continue;
		}
		s->idx[s->count++] = slot;
		uint16_t no = get16(io_rec + O_MSGNO);
		if (no >= s->next_msgno) {
			s->next_msgno = (uint16_t)(no + 1u);
			if (s->next_msgno == 0) {
				s->next_msgno = 1;
			}
		}
	}
}

uint16_t bbs_store_add(bbs_store_t *s, uint8_t type, const char *from, const char *to, const char *subject,
		       const char *body, uint32_t now)
{
	if (s->count >= BBS_MAX_MSGS || s->be.read == NULL || s->be.write == NULL) {
		return 0;
	}
	uint16_t slot = 0;
	bool found = false;
	for (; slot < s->be.slots; slot++) {
		uint8_t hdr[4];
		if (s->be.read(s->be.ctx, s->be.base + (uint32_t)slot * BBS_REC_SIZE, hdr, sizeof(hdr)) != 0) {
			continue;
		}
		if (get32(hdr) != BBS_REC_MAGIC) {	// blank or someone else's bytes: usable
			found = true;
			break;
		}
	}
	if (!found) {
		return 0;
	}

	memset(io_rec, 0, sizeof(io_rec));
	put32(io_rec + O_MAGIC, BBS_REC_MAGIC);
	io_rec[O_TYPE] = type;
	io_rec[O_FLAGS] = 0;
	put16(io_rec + O_MSGNO, s->next_msgno);
	put32(io_rec + O_TIME, now);
	put_str(io_rec + O_FROM, BBS_FROM_MAX, from);
	put_str(io_rec + O_TO, BBS_TO_MAX, to);
	put_str(io_rec + O_SUBJ, BBS_SUBJECT_MAX, subject);
	uint16_t len = 0;
	if (body != NULL) {
		while (len < BBS_BODY_MAX - 1u && body[len] != '\0') {
			io_rec[O_BODY + len] = (uint8_t)body[len];
			len++;
		}
	}
	put16(io_rec + O_BODYLEN, len);
	put32(io_rec + O_CRC, bbs_crc32(io_rec, O_CRC));
	if (!write_slot(s, slot, io_rec)) {
		return 0;
	}
	s->idx[s->count++] = slot;
	uint16_t no = s->next_msgno++;
	if (s->next_msgno == 0) {
		s->next_msgno = 1;
	}
	return no;
}

uint16_t bbs_store_count(const bbs_store_t *s)
{
	return s->count;
}

bool bbs_store_get(const bbs_store_t *s, uint16_t i, bbs_msg_t *out, char *body, size_t body_max)
{
	if (i >= s->count) {
		return false;
	}
	if (!read_slot(s, s->idx[i], io_rec)) {
		return false;
	}
	const uint8_t *src = NULL;
	if (!rec_decode(io_rec, out, &src)) {
		return false;
	}
	if (body != NULL && body_max != 0) {
		size_t n = out->body_len;
		if (n >= body_max) {
			n = body_max - 1u;
		}
		memcpy(body, src, n);
		body[n] = '\0';
	}
	return true;
}

bool bbs_store_get_no(const bbs_store_t *s, uint16_t msgno, bbs_msg_t *out, char *body, size_t body_max)
{
	for (uint16_t i = 0; i < s->count; i++) {
		bbs_msg_t m;
		if (!bbs_store_get(s, i, &m, NULL, 0)) {
			continue;
		}
		if (m.msgno == msgno) {
			return bbs_store_get(s, i, out, body, body_max);
		}
	}
	return false;
}

bool bbs_store_kill(bbs_store_t *s, uint16_t msgno)
{
	for (uint16_t i = 0; i < s->count; i++) {
		if (!read_slot(s, s->idx[i], io_rec) || !rec_decode(io_rec, NULL, NULL)) {
			continue;
		}
		if (get16(io_rec + O_MSGNO) != msgno) {
			continue;
		}
		io_rec[O_FLAGS] |= BBS_FLAG_KILLED;
		put32(io_rec + O_CRC, bbs_crc32(io_rec, O_CRC));
		if (!write_slot(s, s->idx[i], io_rec)) {
			return false;
		}
		for (uint16_t k = i + 1; k < s->count; k++) {
			s->idx[k - 1] = s->idx[k];
		}
		s->count--;
		return true;
	}
	return false;
}
