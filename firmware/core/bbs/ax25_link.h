// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// AX.25 2.2 connected-mode server (docs/spec/bbs.md): the link state machine a packet BBS runs, SABM / UA / DISC
// and I and S frames with modulo-8 sequence numbers, one retransmit timer (T1) and N2 tries. Written from AX.25 2.2
// (TAPR, 1998) 4.3, 6.1-6.4. The caller owns the frames and the RF; this file has no RTOS, no board, no flash, so
// the host tests run it as is.
#ifndef RFOUNDRY_AX25_LINK_H
#define RFOUNDRY_AX25_LINK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "aprs/ax25.h"

#define AX25L_MAX_LINKS	2	// simultaneous connections a BBS build may run
#define AX25L_INFO_MAX	256	// information field of one I frame
#define AX25L_WINDOW	4	// k: unacknowledged I frames allowed
#define AX25L_N2		10	// retries before the link is dropped
#define AX25L_T1_MS		4000	// retransmit timer

// Control field values (the P/F bit is added where the frame needs it)
#define AX25L_SABM	0x2F
#define AX25L_DISC	0x43
#define AX25L_DM		0x0F
#define AX25L_UA		0x63
#define AX25L_FRMR	0x87
#define AX25L_UI		0x03
#define AX25L_RR		0x01
#define AX25L_RNR		0x05
#define AX25L_REJ		0x09
#define AX25L_PF		0x10
#define AX25L_PID_NONE	0xF0

typedef struct ax25_link ax25_link_t;

// Link events. idx is the link table index the caller uses for sends and status
typedef struct {
	void (*connected)(ax25_link_t *l, uint8_t idx);
	void (*data)(ax25_link_t *l, uint8_t idx, const uint8_t *p, size_t n);
	void (*disconnected)(ax25_link_t *l, uint8_t idx, bool remote);
} ax25_link_cb_t;

// Counters for the UI and the debug link
typedef struct {
	uint16_t connects, disconnects, rx_frames, tx_frames, retries, drops;
} ax25_link_stats_t;

// One unacknowledged I frame, kept for a retransmission
typedef struct {
	uint8_t ns;
	uint16_t len;
	uint8_t data[AX25L_INFO_MAX];
} ax25_link_unacked_t;

// One served link
typedef struct {
	uint8_t state;			// 0 free, 1 connected, 2 releasing
	uint8_t vs, va, vr;		// send state, ack state, receive state
	uint8_t retries;
	uint32_t t1_ms;			// when to retransmit, valid while t1_armed
	bool t1_armed;
	ax25_addr_t peer;
	uint8_t nslots;			// I frames in unacked
	ax25_link_unacked_t unacked[AX25L_WINDOW];
	uint8_t disc[2u * AX25_ADDR_BYTES + 1u];	// the DISC frame to repeat while releasing
	size_t disc_len;
} ax25_link_slot_t;

struct ax25_link {
	ax25_addr_t me;
	const ax25_link_cb_t *cb;
	bool (*tx)(const uint8_t *frame, size_t n, void *ctx);
	void *tx_ctx;
	ax25_link_slot_t link[AX25L_MAX_LINKS];
	ax25_link_stats_t v;
};

// tx hands one assembled AX.25 frame (no FCS) to the radio; true when it was queued
void ax25_link_init(ax25_link_t *l, const ax25_addr_t *me, const ax25_link_cb_t *cb,
		    bool (*tx)(const uint8_t *frame, size_t n, void *ctx), void *tx_ctx);

// Feed one received AX.25 frame (no FCS). True when it was a connected-mode frame for us (consumed); false leaves it
// to APRS (UI frames, other destinations)
bool ax25_link_rx(ax25_link_t *l, const uint8_t *frame, size_t n, uint32_t now_ms);

// Timers: retransmissions and give-ups. Call from the radio poll
void ax25_link_tick(ax25_link_t *l, uint32_t now_ms);

// Queue data on a connected link. False when the link is not connected or its window is full
bool ax25_link_send(ax25_link_t *l, uint8_t idx, const uint8_t *p, size_t n, uint32_t now_ms);

// Start a disconnect (the BBS's Q command)
void ax25_link_disconnect(ax25_link_t *l, uint8_t idx, uint32_t now_ms);

// State for the BBS and the UI
bool ax25_link_connected(const ax25_link_t *l, uint8_t idx);
// The peer's callsign ("N0CALL-7"), false when the link is free
bool ax25_link_peer(const ax25_link_t *l, uint8_t idx, char *out, size_t max);
void ax25_link_stats(const ax25_link_t *l, ax25_link_stats_t *s);

#endif
