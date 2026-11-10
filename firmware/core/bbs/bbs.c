// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The packet BBS application (bbs.h). A small personal mailbox: list, read, send, kill, node info, quit. Text lines
// go out as AX.25 I frames, one frame per chunk of the outbound buffer, paced by the link window.

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "bbs/bbs.h"
#include "bbs/ax25_link.h"
#include "debuglink/dlog.h"

#define BBS_LINE_MAX	128
#define BBS_OUT_MAX	1024
#define BBS_LIST_MAX	24	// messages one L or B lists before "use R <n>"

enum { ST_CMD = 0, ST_TO, ST_SUBJ, ST_BODY };

typedef struct {
	bool active;
	uint8_t state;
	char line[BBS_LINE_MAX];
	uint16_t line_len;
	char to[BBS_TO_MAX + 1];
	char subject[BBS_SUBJECT_MAX + 1];
	char body[BBS_BODY_MAX];
	uint16_t body_len;
	char out[BBS_OUT_MAX];
	uint16_t out_len;
	uint16_t out_sent;
} bbs_sess_t;

static ax25_link_t link;
static ax25_addr_t me_addr;
static bbs_store_t *store;
static bbs_sess_t sess[AX25L_MAX_LINKS];
static char call_str[AX25_CALL_LEN + 4];
static bbs_cfg_t cfg;
static bool enabled;
static uint32_t g_now;		// the clock of the last tick or frame, for sends
static bool (*g_tx)(const uint8_t *frame, size_t n, void *ctx);
static void *g_tx_ctx;

// Shared with the UI task: the radio task and the UI both read the store. bbs_radio.c points these at storage_lock
static void (*store_lock)(void);
static void (*store_unlock)(void);
#define BSTORE_LOCK()	do { if (store_lock != NULL) { store_lock(); } } while (0)
#define BSTORE_UNLOCK()	do { if (store_unlock != NULL) { store_unlock(); } } while (0)

// A kill or a clear queued by the UI, run in the radio task (flash work must not run in the UI task)
static bool kill_pending, kill_done, kill_ok;
static uint16_t kill_no;
static bool clear_pending, clear_done, clear_ok;

static void fmt_freq(char *out, size_t max, uint32_t hz)
{
	snprintf(out, max, "%lu.%03lu", (unsigned long)(hz / 1000000u), (unsigned long)((hz / 1000u) % 1000u));
}

static void emit_raw(uint8_t idx, const char *s)
{
	bbs_sess_t *ss = &sess[idx];
	while (*s != '\0' && ss->out_len < BBS_OUT_MAX) {
		ss->out[ss->out_len++] = *s++;
	}
}

static void emitf(uint8_t idx, const char *fmt, ...)
{
	char line[BBS_LINE_MAX];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	emit_raw(idx, line);
	emit_raw(idx, "\r\n");
}

static void prompt(uint8_t idx)
{
	emit_raw(idx, call_str);
	emit_raw(idx, "> ");
}

static void flush_session(uint8_t idx)
{
	bbs_sess_t *ss = &sess[idx];
	while (ss->out_sent < ss->out_len) {
		uint16_t left = (uint16_t)(ss->out_len - ss->out_sent);
		uint16_t chunk = left > AX25L_INFO_MAX ? (uint16_t)AX25L_INFO_MAX : left;
		if (!ax25_link_send(&link, idx, (const uint8_t *)ss->out + ss->out_sent, chunk, g_now)) {
			break;	// window full: the next tick continues
		}
		ss->out_sent = (uint16_t)(ss->out_sent + chunk);
	}
	if (ss->out_sent >= ss->out_len) {
		ss->out_sent = 0;
		ss->out_len = 0;
	}
}

static char upper(char c)
{
	return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static const char *skip_space(const char *p)
{
	while (*p == ' ' || *p == '\t') {
		p++;
	}
	return p;
}

static bool parse_no(const char *p, uint16_t *out)
{
	p = skip_space(p);
	uint32_t v = 0;
	bool any = false;
	while (*p >= '0' && *p <= '9') {
		v = v * 10u + (uint32_t)(*p - '0');
		if (v > 0xFFFFu) {
			v = 0xFFFFu;
		}
		any = true;
		p++;
	}
	*out = (uint16_t)v;
	return any;
}

static void list_messages(uint8_t idx, uint8_t type)
{
	static bbs_msg_t list[BBS_LIST_MAX];	// too big for the radio task's stack
	uint16_t n = 0;
	uint16_t total = 0;
	BSTORE_LOCK();
	if (store != NULL) {
		total = bbs_store_count(store);
		for (uint16_t i = 0; i < total && n < BBS_LIST_MAX; i++) {
			bbs_msg_t m;
			if (!bbs_store_get(store, i, &m, NULL, 0) || m.type != type) {
				continue;
			}
			list[n++] = m;
		}
	}
	BSTORE_UNLOCK();
	for (uint16_t i = 0; i < n; i++) {
		emitf(idx, "%3u %-9s %s", list[i].msgno, list[i].from, list[i].subject);
	}
	if (n == 0) {
		emitf(idx, "(no messages)");
	} else if (n == BBS_LIST_MAX && total > n) {
		emitf(idx, "... more: R <number>");
	}
}

static void read_message(uint8_t idx, const char *arg)
{
	uint16_t no;
	bbs_msg_t m;
	static char body[BBS_BODY_MAX];	// the radio task's stack is small; one message read at a time
	bool ok;
	BSTORE_LOCK();
	ok = store != NULL && parse_no(arg, &no) && bbs_store_get_no(store, no, &m, body, sizeof(body));
	BSTORE_UNLOCK();
	if (!ok) {
		emitf(idx, "No such message.");
		return;
	}
	emitf(idx, "Message #%u  from %s  to %s", m.msgno, m.from, m.to);
	emitf(idx, "Subject: %s", m.subject);
	emitf(idx, "----------------------------------------");
	const char *p = body;
	while (*p != '\0') {
		const char *nl = strchr(p, '\n');
		if (nl == NULL) {
			emitf(idx, "%s", p);
			break;
		}
		char line[BBS_LINE_MAX];
		size_t n = (size_t)(nl - p);
		if (n >= sizeof(line)) {
			n = sizeof(line) - 1;
		}
		memcpy(line, p, n);
		line[n] = '\0';
		emitf(idx, "%s", line);
		p = nl + 1;
	}
	emitf(idx, "----------------------------------------");
}

static void body_append(bbs_sess_t *ss, const char *p)
{
	while (*p != '\0' && ss->body_len < BBS_BODY_MAX - 1u) {
		ss->body[ss->body_len++] = *p++;
	}
	if (ss->body_len < BBS_BODY_MAX - 1u) {
		ss->body[ss->body_len++] = '\n';
	}
	ss->body[ss->body_len] = '\0';
}

static const char *peer_of(uint8_t idx)
{
	static char peer[AX25_CALL_LEN + 4];
	if (!ax25_link_peer(&link, idx, peer, sizeof(peer))) {
		return "?";
	}
	return peer;
}

static void save_message(uint8_t idx)
{
	bbs_sess_t *ss = &sess[idx];
	BSTORE_LOCK();
	uint16_t no = store != NULL ? bbs_store_add(store, BBS_TYPE_MESSAGE, peer_of(idx), ss->to, ss->subject,
						    ss->body, 0) : 0;
	BSTORE_UNLOCK();
	if (no != 0) {
		emitf(idx, "Saved as message #%u.", no);
	} else {
		emitf(idx, "The message store is full.");
	}
	ss->state = ST_CMD;
}

static void help(uint8_t idx)
{
	emitf(idx, "Commands:");
	emitf(idx, "  L        list messages");
	emitf(idx, "  R n      read message n");
	emitf(idx, "  S        send a message");
	emitf(idx, "  K n      kill message n");
	emitf(idx, "  N        node information");
	emitf(idx, "  H        this help");
	emitf(idx, "  Q        disconnect");
}

static void handle_command(uint8_t idx, const char *line)
{
	bbs_sess_t *ss = &sess[idx];
	const char *p = skip_space(line);
	char cmd = upper(*p);
	const char *arg = skip_space(p + 1);

	switch (cmd) {
	case 'L':
		list_messages(idx, BBS_TYPE_MESSAGE);
		prompt(idx);
		break;
	case 'B':
		list_messages(idx, BBS_TYPE_BULLETIN);
		prompt(idx);
		break;
	case 'R':
		read_message(idx, arg);
		prompt(idx);
		break;
	case 'K': {
		uint16_t no;
		bool ok = false;
		BSTORE_LOCK();
		if (store != NULL && parse_no(arg, &no)) {
			ok = bbs_store_kill(store, no);
		}
		BSTORE_UNLOCK();
		if (ok) {
			emitf(idx, "Message #%u killed.", no);
		} else {
			emitf(idx, "No such message.");
		}
		prompt(idx);
		break;
	}
	case 'S':
		if (!ax25_link_connected(&link, idx)) {
			break;
		}
		ss->state = ST_TO;
		ss->to[0] = '\0';
		ss->subject[0] = '\0';
		ss->body_len = 0;
		ss->body[0] = '\0';
		emit_raw(idx, "To: ");
		break;
	case 'N': {
		char fbuf[16];
		fmt_freq(fbuf, sizeof(fbuf), cfg.freq_hz);
		emitf(idx, "%s  %s  sysop %s", cfg.name, call_str, cfg.sysop);
		emitf(idx, "%s MHz, 1200 baud packet. %u message(s).", fbuf, bbs_count());
		prompt(idx);
		break;
	}
	case 'H':
	case '?':
		help(idx);
		prompt(idx);
		break;
	case 'Q':
		emitf(idx, "73 de %s", call_str);
		flush_session(idx);
		ax25_link_disconnect(&link, idx, g_now);
		break;
	case '\0':
		prompt(idx);
		break;
	default:
		emitf(idx, "?  Type H for help.");
		prompt(idx);
		break;
	}
}

static void process_line(uint8_t idx, const char *line)
{
	bbs_sess_t *ss = &sess[idx];
	switch (ss->state) {
	case ST_CMD:
		handle_command(idx, line);
		break;
	case ST_TO:
		snprintf(ss->to, sizeof(ss->to), "%s", line);
		ss->state = ST_SUBJ;
		emit_raw(idx, "Subject: ");
		break;
	case ST_SUBJ:
		snprintf(ss->subject, sizeof(ss->subject), "%s", line);
		ss->state = ST_BODY;
		emitf(idx, "Enter the body, one line at a time. End with /EX.");
		break;
	case ST_BODY:
		if (strcmp(line, "/EX") == 0) {
			save_message(idx);
			prompt(idx);
		} else {
			body_append(ss, line);
		}
		break;
	}
}

static void feed_byte(uint8_t idx, uint8_t byte)
{
	bbs_sess_t *ss = &sess[idx];
	if (byte == '\r' || byte == '\n') {
		if (ss->line_len != 0) {
			ss->line[ss->line_len] = '\0';
			ss->line_len = 0;
			process_line(idx, ss->line);
		} else if (ss->state == ST_CMD) {
			process_line(idx, "");
		}
		return;
	}
	if (byte == 0x7F || byte == 0x08) {	// backspace
		if (ss->line_len != 0) {
			ss->line_len--;
		}
		return;
	}
	if (byte >= 0x20 && byte < 0x7F && ss->line_len < BBS_LINE_MAX - 1u) {
		ss->line[ss->line_len++] = (char)byte;
	}
}

// --- ax25_link callbacks ---

static void on_connected(ax25_link_t *l, uint8_t idx)
{
	(void)l;
	bbs_sess_t *ss = &sess[idx];
	memset(ss, 0, sizeof(*ss));
	ss->active = true;
	ss->state = ST_CMD;
	char fbuf[16];
	fmt_freq(fbuf, sizeof(fbuf), cfg.freq_hz);
	emitf(idx, "");
	emitf(idx, "*** %s  %s  %s MHz ***", cfg.name, call_str, fbuf);
	emitf(idx, "Sysop %s. Type H for help, Q to quit.", cfg.sysop);
	prompt(idx);
}

static void on_data(ax25_link_t *l, uint8_t idx, const uint8_t *p, size_t n)
{
	(void)l;
	if (idx >= AX25L_MAX_LINKS) {
		return;
	}
	for (size_t i = 0; i < n; i++) {
		feed_byte(idx, p[i]);
	}
}

static void on_disconnected(ax25_link_t *l, uint8_t idx, bool remote)
{
	(void)l;
	(void)remote;
	if (idx < AX25L_MAX_LINKS) {
		sess[idx].active = false;
		sess[idx].out_len = 0;
		sess[idx].out_sent = 0;
	}
}

static const ax25_link_cb_t callbacks = { on_connected, on_data, on_disconnected };

void bbs_set_store_lock(void (*lock)(void), void (*unlock)(void))
{
	store_lock = lock;
	store_unlock = unlock;
}

void bbs_set_config(const bbs_cfg_t *c)
{
	bbs_cfg_t n = *c;
	bbs_cfg_normalize(&n);
	if (!bbs_cfg_valid(&n)) {
		bbs_cfg_defaults(&n);
	}
	if (n.name[0] == '\0') {
		strcpy(n.name, BBS_NAME_DEFAULT);
	}
	if (n.sysop[0] == '\0') {
		strcpy(n.sysop, BBS_SYSOP_DEFAULT);
	}
	cfg = n;
	bbs_cfg_format_call(&cfg, call_str, sizeof(call_str));
	memset(&me_addr, 0, sizeof(me_addr));
	ax25_addr_parse(cfg.call, strlen(cfg.call), &me_addr);
	me_addr.ssid = cfg.ssid;
	me_addr.h = false;
	memset(sess, 0, sizeof(sess));
	ax25_link_init(&link, &me_addr, &callbacks, g_tx, g_tx_ctx);
}

void bbs_get_config(bbs_cfg_t *out)
{
	*out = cfg;
}

void bbs_set_enabled(bool on)
{
	dlog("bbs: set enabled %u (was %u)", (unsigned)on, (unsigned)enabled);
	if (enabled == on) {
		return;
	}
	enabled = on;
	if (!on) {
		memset(sess, 0, sizeof(sess));
		ax25_link_init(&link, &me_addr, &callbacks, g_tx, g_tx_ctx);
	}
}

bool bbs_enabled(void)
{
	return enabled;
}

void bbs_init(bbs_store_t *s, bool (*tx)(const uint8_t *frame, size_t n, void *ctx), void *tx_ctx)
{
	store = s;
	g_tx = tx;
	g_tx_ctx = tx_ctx;
	bbs_cfg_t d;
	bbs_cfg_defaults(&d);
	bbs_set_config(&d);
	enabled = false;
}

bool bbs_rx_frame(const uint8_t *frame, size_t n, uint32_t now_ms)
{
	g_now = now_ms;
	if (!enabled) {
		return false;
	}
	return ax25_link_rx(&link, frame, n, now_ms);
}

static void run_ui_jobs(void)
{
	if (kill_pending) {
		BSTORE_LOCK();
		kill_ok = store != NULL && bbs_store_kill(store, kill_no);
		BSTORE_UNLOCK();
		kill_pending = false;
		kill_done = true;
	}
	if (clear_pending) {
		BSTORE_LOCK();
		if (store != NULL && bbs_store_count(store) > 0) {
			bbs_msg_t m;
			if (bbs_store_get(store, 0, &m, NULL, 0)) {
				(void)bbs_store_kill(store, m.msgno);	// one a tick: flash work, spread out
			} else {
				clear_ok = false;
				clear_pending = false;
				clear_done = true;
			}
		} else {
			clear_ok = true;
			clear_pending = false;
			clear_done = true;
		}
		BSTORE_UNLOCK();
	}
}

void bbs_tick(uint32_t now_ms)
{
	g_now = now_ms;
	run_ui_jobs();
	if (!enabled) {
		return;
	}
	ax25_link_tick(&link, now_ms);
	for (uint8_t i = 0; i < AX25L_MAX_LINKS; i++) {
		if (sess[i].active && ax25_link_connected(&link, i)) {
			flush_session(i);
		}
	}
}

uint16_t bbs_count(void)
{
	BSTORE_LOCK();
	uint16_t n = store != NULL ? bbs_store_count(store) : 0;
	BSTORE_UNLOCK();
	return n;
}

bool bbs_peer(uint8_t idx, char *out, size_t max)
{
	return ax25_link_peer(&link, idx, out, max);
}

void bbs_get_status(bbs_status_t *out)
{
	memset(out, 0, sizeof(*out));
	out->enabled = enabled;
	out->messages = bbs_count();
	ax25_link_stats_t st;
	ax25_link_stats(&link, &st);
	out->rx_frames = st.rx_frames;
	out->tx_frames = st.tx_frames;
	out->connects = st.connects;
	for (uint8_t i = 0; i < AX25L_MAX_LINKS; i++) {
		if (ax25_link_connected(&link, i)) {
			out->links++;
			if (out->peer[0] == '\0') {
				ax25_link_peer(&link, i, out->peer, sizeof(out->peer));
			}
		}
	}
}

uint16_t bbs_ui_count(void)
{
	return bbs_count();
}

bool bbs_ui_msg(uint16_t i, bbs_msg_t *out, char *body, size_t body_max)
{
	BSTORE_LOCK();
	bool ok = store != NULL && bbs_store_get(store, i, out, body, body_max);
	BSTORE_UNLOCK();
	return ok;
}

bool bbs_ui_msg_no(uint16_t msgno, bbs_msg_t *out, char *body, size_t body_max)
{
	BSTORE_LOCK();
	bool ok = store != NULL && bbs_store_get_no(store, msgno, out, body, body_max);
	BSTORE_UNLOCK();
	return ok;
}

void bbs_ui_kill(uint16_t msgno)
{
	kill_no = msgno;
	kill_pending = true;
	kill_done = false;
}

void bbs_ui_clear(void)
{
	clear_pending = true;
	clear_done = false;
}

uint8_t bbs_ui_poll(bool *ok)
{
	if (kill_done) {
		kill_done = false;
		*ok = kill_ok;
		return 2;
	}
	if (clear_done) {
		clear_done = false;
		*ok = clear_ok;
		return 2;
	}
	return (kill_pending || clear_pending) ? 1 : 0;
}
