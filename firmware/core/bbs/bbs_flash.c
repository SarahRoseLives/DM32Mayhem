// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// BBS store on the data flash (bbs_flash.h): the native area at 0xF00000. A header in its first sector says the area
// is ours; blank means claim it; anything else is left alone.

#include <string.h>
#include "board.h"
#include "hal_flash.h"
#include "storage/storage.h"
#include "bbs/bbs_flash.h"

#define BBS_FLASH_BASE	0xF00000u	// the native area (boards/dm32/board.c flash_map.native_addr)
#define BBS_FLASH_SIZE	0x100000u	// 1 MB
#define BBS_FLASH_MAGIC	0x48534242u	// "BBSH" in memory order
#define BBS_FLASH_VERSION	1u

static uint8_t sector[STORAGE_SECTOR];
static uint8_t hdr_rec[BBS_REC_SIZE];

static bool in_range(uint32_t addr, uint32_t len)
{
	return addr >= BBS_FLASH_BASE && len <= BBS_FLASH_SIZE && addr - BBS_FLASH_BASE <= BBS_FLASH_SIZE - len;
}

static int bf_read(void *ctx, uint32_t addr, void *buf, uint32_t len)
{
	(void)ctx;
	if (board.flash == NULL || board.flash->read == NULL || buf == NULL || !in_range(addr, len)) {
		return -1;
	}
	return board.flash->read(addr, buf, len) ? 0 : -1;
}

static int bf_write(void *ctx, uint32_t addr, const void *buf, uint32_t len)
{
	(void)ctx;
	const hal_flash_t *f = board.flash;
	if (f == NULL || f->read == NULL || f->write == NULL || f->erase == NULL || buf == NULL) {
		return -1;
	}
	if (len == 0 || !in_range(addr, len)) {
		return -1;
	}
	storage_lock();
	int rc = 0;
	uint32_t done = 0;
	while (done < len) {
		uint32_t a = addr + done;
		uint32_t sec = a & ~(STORAGE_SECTOR - 1u);
		uint32_t off = a - sec;
		uint32_t chunk = STORAGE_SECTOR - off;
		if (chunk > len - done) {
			chunk = len - done;
		}
		if (!f->read(sec, sector, sizeof(sector))) {
			rc = -1;
			break;
		}
		memcpy(sector + off, (const uint8_t *)buf + done, chunk);
		if (!f->erase(sec) || !f->write(sec, sector, sizeof(sector))) {
			rc = -1;
			break;
		}
		done += chunk;
	}
	storage_unlock();
	return rc;
}

static bool blank_sector(void)
{
	return board.flash->read(BBS_FLASH_BASE, sector, sizeof(sector)) &&
	       sector[0] == 0xFFu && memcmp(sector, sector + 1, sizeof(sector) - 1u) == 0;
}

bool bbs_flash_backend(bbs_store_backend_t *be)
{
	uint8_t *hdr = hdr_rec;
	if (board.flash == NULL || board.flash->read == NULL || board.flash->write == NULL ||
	    board.flash->erase == NULL) {
		return false;
	}
	if (!board.flash->read(BBS_FLASH_BASE, hdr, sizeof(hdr))) {
		return false;
	}
	uint32_t magic = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) | ((uint32_t)hdr[2] << 16) |
			 ((uint32_t)hdr[3] << 24);
	if (magic != BBS_FLASH_MAGIC) {
		bool blank = hdr[0] == 0xFFu && memcmp(hdr, hdr + 1, sizeof(hdr) - 1u) == 0 && blank_sector();
		if (!blank) {
			return false;	// someone else's data: never erase it
		}
		memset(hdr, 0, BBS_REC_SIZE);
		hdr[0] = (uint8_t)BBS_FLASH_MAGIC;
		hdr[1] = (uint8_t)(BBS_FLASH_MAGIC >> 8);
		hdr[2] = (uint8_t)(BBS_FLASH_MAGIC >> 16);
		hdr[3] = (uint8_t)(BBS_FLASH_MAGIC >> 24);
		hdr[4] = (uint8_t)BBS_FLASH_VERSION;
		if (bf_write(NULL, BBS_FLASH_BASE, hdr, sizeof(hdr)) != 0) {
			return false;
		}
	}
	be->read = bf_read;
	be->write = bf_write;
	be->ctx = NULL;
	be->base = BBS_FLASH_BASE + BBS_REC_SIZE;	// after the header sector's first record
	be->slots = (uint16_t)((BBS_FLASH_SIZE - BBS_REC_SIZE) / BBS_REC_SIZE);
	return true;
}
