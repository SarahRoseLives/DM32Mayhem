// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The packet BBS application (bbs.h): a banner and a command prompt over an AX.25 connected link, messages kept in
// the store. The radio supplies the frames, the store supplies the memory; this file has no RTOS, no board.
#ifndef RFOUNDRY_BBS_H
#define RFOUNDRY_BBS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "bbs/bbs_store.h"
#include "bbs/bbs_config.h"
#include "aprs/ax25.h"

// One-time setup. tx hands an assembled frame to the radio (ax25_link.h). The defaults apply until bbs_set_config
void bbs_init(bbs_store_t *store, bool (*tx)(const uint8_t *frame, size_t n, void *ctx), void *tx_ctx);
// Take a configuration (validated and normalized here). Re-initializes the links
void bbs_set_config(const bbs_cfg_t *cfg);
void bbs_get_config(bbs_cfg_t *out);
// Answering on or off. Off drops any connection; on resumes on the configured channel
void bbs_set_enabled(bool on);
bool bbs_enabled(void);

// Lock around store access shared by the radio task and the UI task (bbs_radio.c points it at storage_lock). NULL:
// no lock, as in the host tests
void bbs_set_store_lock(void (*lock)(void), void (*unlock)(void));

// One received AX.25 frame (no FCS) from the demodulator. True when it was a connected-mode frame the BBS consumed;
// UI frames and other destinations are left to APRS
bool bbs_rx_frame(const uint8_t *frame, size_t n, uint32_t now_ms);
// Timers and the outbound queue. Call from the radio poll
void bbs_tick(uint32_t now_ms);

uint16_t bbs_count(void);
bool bbs_peer(uint8_t idx, char *out, size_t max);

typedef struct {
	bool enabled;
	uint8_t links;			// connected stations
	char peer[AX25_CALL_LEN + 4];	// the first connected station ("N0CALL-7"), "" when none
	uint16_t messages;
	uint32_t rx_frames, tx_frames, connects;
} bbs_status_t;
void bbs_get_status(bbs_status_t *out);

// --- For the radio's own menu (any task; safe with the store lock) ---
uint16_t bbs_ui_count(void);
// The i-th live message (oldest first). body NULL: header only. False for an out-of-range index or an unreadable record
bool bbs_ui_msg(uint16_t i, bbs_msg_t *out, char *body, size_t body_max);
bool bbs_ui_msg_no(uint16_t msgno, bbs_msg_t *out, char *body, size_t body_max);
// A kill and a clear all run in the radio task (flash work): poll for the result
void bbs_ui_kill(uint16_t msgno);
void bbs_ui_clear(void);
// 0 idle, 1 a request is pending, 2 one finished since the last call (ok says how it went)
uint8_t bbs_ui_poll(bool *ok);

#endif
