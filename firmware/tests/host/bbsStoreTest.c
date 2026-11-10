// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Host test of the BBS message store (core/bbs/bbs_store.c) on a RAM backend.

#include <stdio.h>
#include <string.h>
#include "bbs/bbs_store.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

#define RAM_SIZE 4096   // 8 records
static uint8_t ram[RAM_SIZE];

static int ram_read(void *ctx, uint32_t addr, void *buf, uint32_t len)
{
	(void)ctx;
	if (addr > RAM_SIZE || len > RAM_SIZE - addr) {
		return -1;
	}
	memcpy(buf, ram + addr, len);
	return 0;
}

static int ram_write(void *ctx, uint32_t addr, const void *buf, uint32_t len)
{
	(void)ctx;
	if (addr > RAM_SIZE || len > RAM_SIZE - addr) {
		return -1;
	}
	memcpy(ram + addr, buf, len);
	return 0;
}

int main(void)
{
	memset(ram, 0xFF, sizeof(ram));
	bbs_store_backend_t be = { ram_read, ram_write, NULL, 0, RAM_SIZE / BBS_REC_SIZE };
	bbs_store_t store;
	bbs_store_init(&store, &be);
	CHECK(bbs_store_count(&store) == 0);

	uint16_t no1 = bbs_store_add(&store, BBS_TYPE_MESSAGE, "AD8NT-1", "K8SDR-1", "First", "Hello\nWorld\n", 1234);
	CHECK(no1 == 1);
	CHECK(bbs_store_count(&store) == 1);

	bbs_msg_t m;
	char body[128];
	CHECK(bbs_store_get(&store, 0, &m, body, sizeof(body)));
	CHECK(m.msgno == 1 && m.type == BBS_TYPE_MESSAGE);
	CHECK(strcmp(m.from, "AD8NT-1") == 0);
	CHECK(strcmp(m.to, "K8SDR-1") == 0);
	CHECK(strcmp(m.subject, "First") == 0);
	CHECK(m.body_len == 12);
	CHECK(strcmp(body, "Hello\nWorld\n") == 0);
	CHECK(m.time == 1234);

	uint16_t no2 = bbs_store_add(&store, BBS_TYPE_MESSAGE, "W1AW-2", "K8SDR-1", "Second", "73", 5678);
	CHECK(no2 == 2);
	CHECK(bbs_store_count(&store) == 2);

	CHECK(bbs_store_get_no(&store, 2, &m, body, sizeof(body)));
	CHECK(strcmp(m.subject, "Second") == 0 && strcmp(body, "73") == 0);

	// Kill the first; the second becomes index 0
	CHECK(bbs_store_kill(&store, 1));
	CHECK(bbs_store_count(&store) == 1);
	CHECK(!bbs_store_get_no(&store, 1, &m, body, sizeof(body)));
	CHECK(bbs_store_get(&store, 0, &m, body, sizeof(body)));
	CHECK(m.msgno == 2);

	// A rescan sees the same live message after the reboot
	bbs_store_t again;
	bbs_store_init(&again, &be);
	CHECK(bbs_store_count(&again) == 1);
	CHECK(bbs_store_get(&again, 0, &m, body, sizeof(body)));
	CHECK(m.msgno == 2 && strcmp(m.subject, "Second") == 0);

	// A torn write (magic but bad CRC) is skipped, not read as a message
	size_t torn = 3u * BBS_REC_SIZE;
	ram[torn + 0] = 0x42;
	ram[torn + 1] = 0x53;
	ram[torn + 2] = 0x42;
	ram[torn + 3] = 0x31;
	bbs_store_init(&again, &be);
	CHECK(bbs_store_count(&again) == 1);

	printf("%s: %d failure(s)\n", fails == 0 ? "# PASSED" : "# FAILED", fails);
	return fails != 0;
}
