// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The BBS message store (bbs_store.h): a log of fixed 512-byte records in a byte-addressable backend. The firmware
// backend is the radio's data flash (bbs_flash.c); the host tests use a RAM array. No RTOS, no board.
#ifndef RFOUNDRY_BBS_STORE_H
#define RFOUNDRY_BBS_STORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define BBS_REC_SIZE		512u
#define BBS_REC_MAGIC		0x31425342u	// "BSB1" in memory order
#define BBS_SUBJECT_MAX	40
#define BBS_FROM_MAX		10
#define BBS_TO_MAX		10
#define BBS_BODY_MAX		420
#define BBS_MAX_MSGS		256

#define BBS_TYPE_MESSAGE	1
#define BBS_TYPE_BULLETIN	2

#define BBS_FLAG_READ	0x01u
#define BBS_FLAG_KILLED	0x02u

typedef struct {
	int (*read)(void *ctx, uint32_t addr, void *buf, uint32_t len);	// 0 ok
	int (*write)(void *ctx, uint32_t addr, const void *buf, uint32_t len);
	void *ctx;
	uint32_t base;
	uint16_t slots;		// slots = region size / BBS_REC_SIZE
} bbs_store_backend_t;

typedef struct {
	uint8_t type;
	uint8_t flags;
	uint16_t msgno;
	uint16_t body_len;
	uint32_t time;
	char from[BBS_FROM_MAX + 1];
	char to[BBS_TO_MAX + 1];
	char subject[BBS_SUBJECT_MAX + 1];
} bbs_msg_t;

typedef struct {
	bbs_store_backend_t be;
	uint16_t idx[BBS_MAX_MSGS];	// slot of each live message, oldest first
	uint16_t count;
	uint16_t next_msgno;
} bbs_store_t;

// Scan the backend and build the index. Safe on blank or foreign flash: bad records are skipped
void bbs_store_init(bbs_store_t *s, const bbs_store_backend_t *be);

// Add a message. from/to/subject/body are copied (truncated). Returns the message number, or 0 when the store is
// full or the write failed
uint16_t bbs_store_add(bbs_store_t *s, uint8_t type, const char *from, const char *to, const char *subject,
		       const char *body, uint32_t now);

uint16_t bbs_store_count(const bbs_store_t *s);

// The i-th live message (0..count-1, oldest first). body is NUL terminated and truncated to body_max
bool bbs_store_get(const bbs_store_t *s, uint16_t i, bbs_msg_t *out, char *body, size_t body_max);
bool bbs_store_get_no(const bbs_store_t *s, uint16_t msgno, bbs_msg_t *out, char *body, size_t body_max);

// Mark a message killed and drop it from the index. True when it was found and rewritten
bool bbs_store_kill(bbs_store_t *s, uint16_t msgno);

#endif
