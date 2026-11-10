// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Host test of the BBS session (core/bbs/bbs.c): connect, banner, L, R, N, Q over the AX.25 link with a RAM store.

#include <stdio.h>
#include <string.h>
#include "bbs/bbs.h"
#include "bbs/ax25_link.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

#define RAM_SIZE 4096
static uint8_t ram[RAM_SIZE];
static int ram_read(void *ctx, uint32_t addr, void *buf, uint32_t len)
{
	(void)ctx;
	if (addr > RAM_SIZE || len > RAM_SIZE - addr) { return -1; }
	memcpy(buf, ram + addr, len);
	return 0;
}
static int ram_write(void *ctx, uint32_t addr, const void *buf, uint32_t len)
{
	(void)ctx;
	if (addr > RAM_SIZE || len > RAM_SIZE - addr) { return -1; }
	memcpy(ram + addr, buf, len);
	return 0;
}

// --- the client side of the wire ---
static void put_addr(uint8_t *out, const char *call, uint8_t ssid, bool last, bool cbit)
{
	char c[6] = { ' ', ' ', ' ', ' ', ' ', ' ' };
	for (size_t i = 0; i < 6 && call[i] != '\0'; i++) { c[i] = call[i]; }
	for (size_t i = 0; i < 6; i++) { out[i] = (uint8_t)((uint8_t)c[i] << 1); }
	out[6] = (uint8_t)(0x60 | ((ssid & 0x0Fu) << 1) | (cbit ? 0x80u : 0u) | (last ? 1u : 0u));
}
static size_t mkframe(uint8_t *out, const char *dst, uint8_t dssid, const char *src, uint8_t sssid,
		      uint8_t control, int pid, const uint8_t *info, size_t n, bool command)
{
	put_addr(out, dst, dssid, false, command);
	put_addr(out + 7, src, sssid, true, !command);
	size_t k = 14;
	out[k++] = control;
	if (pid >= 0) { out[k++] = (uint8_t)pid; }
	if (n != 0) { memcpy(out + k, info, n); }
	return k + n;
}

static char server_text[8192];
static size_t server_text_len;
static int server_i;
static uint8_t disc_seen;

static bool tx_cb(const uint8_t *frame, size_t n, void *ctx)
{
	(void)ctx;
	uint8_t ctrl = frame[14];
	if ((ctrl & 0x01u) == 0 && n >= 16) {	// an I frame from the server
		size_t len = n - 16;
		if (server_text_len + len + 1 < sizeof(server_text)) {
			memcpy(server_text + server_text_len, frame + 16, len);
			server_text_len += len;
			server_text[server_text_len++] = '\n';
		}
		server_i++;
	}
	if ((ctrl & 0xEFu) == AX25L_DISC) {
		disc_seen = 1;
	}
	return true;
}

static uint8_t client_ns;

static void client_i(const char *line)
{
	uint8_t f[300];
	uint8_t nr = (uint8_t)(server_i & 0x07);
	uint8_t ctrl = (uint8_t)((nr << 5) | ((client_ns & 7) << 1));
	size_t n = mkframe(f, "N0CALL", 0, "N0CALL", 1, ctrl, AX25L_PID_NONE, (const uint8_t *)line,
			   strlen(line), true);
	client_ns = (uint8_t)((client_ns + 1) & 7);
	bbs_rx_frame(f, n, 1000 * (uint32_t)(client_ns + 1));
}

static void client_rr(void)
{
	uint8_t f[300];
	uint8_t nr = (uint8_t)(server_i & 0x07);
	size_t n = mkframe(f, "N0CALL", 0, "N0CALL", 1, (uint8_t)((nr << 5) | AX25L_RR), -1, NULL, 0, false);
	bbs_rx_frame(f, n, 9000);
}

static bool text_has(const char *needle)
{
	return strstr(server_text, needle) != NULL;
}

int main(void)
{
	memset(ram, 0xFF, sizeof(ram));
	bbs_store_backend_t be = { ram_read, ram_write, NULL, 0, RAM_SIZE / BBS_REC_SIZE };
	bbs_store_t store;
	bbs_store_init(&store, &be);
	uint16_t no = bbs_store_add(&store, BBS_TYPE_MESSAGE, "N0CALL-1", "N0CALL", "Welcome", "Hello there", 1000);
	CHECK(no == 1);

	bbs_init(&store, tx_cb, NULL);
	bbs_cfg_t cfg;
	bbs_cfg_defaults(&cfg);
	bbs_set_config(&cfg);
	bbs_set_enabled(true);

	// Connect
	uint8_t f[300];
	size_t n = mkframe(f, "N0CALL", 0, "N0CALL", 1, AX25L_SABM | AX25L_PF, -1, NULL, 0, true);
	bbs_rx_frame(f, n, 1000);
	bbs_tick(1000);
	CHECK(text_has(BBS_NAME_DEFAULT));
	CHECK(text_has("N0CALL"));
	client_rr();

	// List
	client_i("L\r");
	bbs_tick(2000);
	CHECK(text_has("Welcome"));
	CHECK(text_has("N0CALL-1"));
	client_rr();

	// Read
	client_i("R 1\r");
	bbs_tick(3000);
	CHECK(text_has("Subject: Welcome"));
	CHECK(text_has("Hello there"));
	client_rr();

	// Node info
	client_i("N\r");
	bbs_tick(4000);
	CHECK(text_has("145.100"));
	client_rr();

	// Quit: the BBS says 73 and disconnects
	client_i("Q\r");
	bbs_tick(5000);
	CHECK(text_has("73 de N0CALL"));
	CHECK(disc_seen);

	// The client answers the DISC with UA, which frees the link
	n = mkframe(f, "N0CALL", 0, "N0CALL", 1, AX25L_UA | AX25L_PF, -1, NULL, 0, false);
	bbs_rx_frame(f, n, 6000);

	// The UI-facing calls over the RAM store: one message, read it, kill it
	CHECK(bbs_ui_count() == 1);
	bbs_msg_t um;
	char ubody[128];
	CHECK(bbs_ui_msg(0, &um, ubody, sizeof ubody));
	CHECK(um.msgno == 1 && strcmp(um.from, "N0CALL-1") == 0 && strcmp(um.subject, "Welcome") == 0);
	CHECK(strcmp(ubody, "Hello there") == 0);
	CHECK(bbs_ui_msg_no(1, &um, NULL, 0) && um.msgno == 1);
	bbs_ui_kill(1);
	bbs_tick(7000);
	bool job_ok = false;
	CHECK(bbs_ui_poll(&job_ok) == 2 && job_ok);
	CHECK(bbs_ui_count() == 0);

	// Disabled, a connected-mode frame is left alone
	uint8_t sabm[64];
	size_t sn = mkframe(sabm, "N0CALL", 0, "N0CALL", 1, AX25L_SABM | AX25L_PF, -1, NULL, 0, true);
	bbs_set_enabled(false);
	CHECK(!bbs_rx_frame(sabm, sn, 8000));
	bbs_set_enabled(true);

	// A new configuration's name shows in the banner
	bbs_cfg_t c;
	bbs_get_config(&c);
	strcpy(c.name, "K8BBS");
	bbs_set_config(&c);
	bbs_get_config(&c);
	CHECK(strcmp(c.name, "K8BBS") == 0);
	server_text_len = 0;
	server_i = 0;
	server_text[0] = '\0';
	client_ns = 0;
	size_t n2 = mkframe(sabm, "N0CALL", 0, "N0CALL", 1, AX25L_SABM | AX25L_PF, -1, NULL, 0, true);
	bbs_rx_frame(sabm, n2, 9000);
	bbs_tick(9000);
	CHECK(text_has("K8BBS"));

	printf("%s: %d failure(s)\n", fails == 0 ? "# PASSED" : "# FAILED", fails);
	if (fails != 0) {
		printf("--- server text ---\n%s\n", server_text);
	}
	return fails != 0;
}
