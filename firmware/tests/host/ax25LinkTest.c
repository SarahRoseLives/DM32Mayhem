// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Host test of the AX.25 connected-mode server (core/bbs/ax25_link.c): a client SABM, an I frame each way, an ack, a
// DISC, and a retransmission after T1.

#include <stdio.h>
#include <string.h>
#include "bbs/ax25_link.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

// --- the client side of the wire: addresses and frames ---

static void put_addr(uint8_t *out, const char *call, uint8_t ssid, bool last, bool cbit)
{
	char c[6] = { ' ', ' ', ' ', ' ', ' ', ' ' };
	for (size_t i = 0; i < 6 && call[i] != '\0'; i++) {
		c[i] = call[i];
	}
	for (size_t i = 0; i < 6; i++) {
		out[i] = (uint8_t)((uint8_t)c[i] << 1);
	}
	out[6] = (uint8_t)(0x60 | ((ssid & 0x0Fu) << 1) | (cbit ? 0x80u : 0u) | (last ? 1u : 0u));
}

static size_t mkframe(uint8_t *out, const char *dst, uint8_t dssid, const char *src, uint8_t sssid,
		      uint8_t control, int pid, const uint8_t *info, size_t n, bool command)
{
	put_addr(out, dst, dssid, false, command);
	put_addr(out + 7, src, sssid, true, !command);
	size_t k = 14;
	out[k++] = control;
	if (pid >= 0) {
		out[k++] = (uint8_t)pid;
	}
	if (n != 0) {
		memcpy(out + k, info, n);
	}
	/* the AX.25 FCS is not part of the host test's frames; the link takes frames without one, like the demod */
	return k + n;
}

static uint8_t last[2][AX25L_INFO_MAX + 20];
static size_t last_len[2];
static int tx_count;

static bool tx_cb(const uint8_t *frame, size_t n, void *ctx)
{
	(void)ctx;
	int slot = tx_count++ & 1;
	memcpy(last[slot], frame, n);
	last_len[slot] = n;
	return true;
}

// the last frame's control byte (no digipeaters: always at offset 14)
static uint8_t last_ctrl(void) { return last[(tx_count + 1) & 1][14]; }
static const uint8_t *last_info(size_t *n)
{
	int slot = (tx_count + 1) & 1;
	uint8_t ctrl = last[slot][14];
	if ((ctrl & 0x01u) == 0) {	// I frame: PID then info
		*n = last_len[slot] - 16;
		return last[slot] + 16;
	}
	*n = 0;
	return NULL;
}

// --- callbacks ---

static int connected, disconnected_remote, data_len;
static char data_buf[AX25L_INFO_MAX + 1];

static void on_connected(ax25_link_t *l, uint8_t idx) { (void)l; (void)idx; connected++; }
static void on_data(ax25_link_t *l, uint8_t idx, const uint8_t *p, size_t n)
{
	(void)l; (void)idx;
	memcpy(data_buf, p, n);
	data_buf[n] = '\0';
	data_len = (int)n;
}
static void on_disconnected(ax25_link_t *l, uint8_t idx, bool remote)
{
	(void)l; (void)idx;
	if (remote) {
		disconnected_remote++;
	}
}

int main(void)
{
	static const ax25_link_cb_t cb = { on_connected, on_data, on_disconnected };
	ax25_addr_t me;
	CHECK(ax25_addr_parse("N0CALL", 6, &me));
	ax25_link_t link;
	ax25_link_init(&link, &me, &cb, tx_cb, NULL);

	uint8_t f[AX25L_INFO_MAX + 20];
	size_t n;

	// The client connects
	n = mkframe(f, "N0CALL", 0, "N0CALL", 1, AX25L_SABM | AX25L_PF, -1, NULL, 0, true);
	CHECK(ax25_link_rx(&link, f, n, 1000));
	CHECK(connected == 1);
	CHECK((last_ctrl() & 0xEFu) == AX25L_UA);
	CHECK(ax25_link_connected(&link, 0));
	char peer[16];
	CHECK(ax25_link_peer(&link, 0, peer, sizeof(peer)));
	CHECK(strcmp(peer, "N0CALL-1") == 0);

	// The client sends a line in I frame 0
	n = mkframe(f, "N0CALL", 0, "N0CALL", 1, 0x00, AX25L_PID_NONE, (const uint8_t *)"HELLO", 5, true);
	CHECK(ax25_link_rx(&link, f, n, 2000));
	CHECK(connected == 1);
	CHECK(data_len == 5 && strcmp(data_buf, "HELLO") == 0);

	// The server sends a line, then the client acknowledges it
	int before = tx_count;
	CHECK(ax25_link_send(&link, 0, (const uint8_t *)"HI", 2, 3000));
	CHECK(tx_count == before + 1);
	CHECK((last_ctrl() & 0x01u) == 0);	// an I frame
	size_t ilen;
	const uint8_t *info = last_info(&ilen);
	CHECK(ilen == 2 && memcmp(info, "HI", 2) == 0);
	n = mkframe(f, "N0CALL", 0, "N0CALL", 1, 0x01 | (1u << 5), -1, NULL, 0, false);	// RR nr=1
	CHECK(ax25_link_rx(&link, f, n, 3100));

	// The client disconnects
	n = mkframe(f, "N0CALL", 0, "N0CALL", 1, AX25L_DISC | AX25L_PF, -1, NULL, 0, true);
	CHECK(ax25_link_rx(&link, f, n, 4000));
	CHECK((last_ctrl() & 0xEFu) == AX25L_UA);
	CHECK(disconnected_remote == 1);
	CHECK(!ax25_link_connected(&link, 0));

	// A second connect, an unacknowledged I frame, and T1 retransmission
	connected = 0;
	n = mkframe(f, "N0CALL", 0, "N0CALL", 1, AX25L_SABM | AX25L_PF, -1, NULL, 0, true);
	CHECK(ax25_link_rx(&link, f, n, 5000));
	CHECK(connected == 1);
	before = tx_count;
	CHECK(ax25_link_send(&link, 0, (const uint8_t *)"X", 1, 5000));
	ax25_link_tick(&link, 5000 + AX25L_T1_MS + 1);
	CHECK(tx_count > before + 1);	// the I frame went out again
	ax25_link_stats_t st;
	ax25_link_stats(&link, &st);
	CHECK(st.retries == 1);

	// A second caller connects (two links); a third is refused with DM
	n = mkframe(f, "N0CALL", 0, "N0CALL", 7, AX25L_SABM | AX25L_PF, -1, NULL, 0, true);
	CHECK(ax25_link_rx(&link, f, n, 6000));
	CHECK(connected == 2);
	CHECK((last_ctrl() & 0xEFu) == AX25L_UA);
	n = mkframe(f, "N0CALL", 0, "W1AW", 2, AX25L_SABM | AX25L_PF, -1, NULL, 0, true);
	CHECK(ax25_link_rx(&link, f, n, 6100));
	CHECK((last_ctrl() & 0xEFu) == AX25L_DM);

	printf("%s: %d failure(s)\n", fails == 0 ? "# PASSED" : "# FAILED", fails);
	return fails != 0;
}
