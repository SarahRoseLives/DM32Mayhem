// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// AX.25 connected-mode server (ax25_link.h). Server role only: a peer sends SABM, we answer UA; I frames carry the
// BBS text; DISC closes. Sequence numbers are modulo 8 (AX.25 2.2 4.2.4 when XID is not used).

#include <string.h>
#include "bbs/ax25_link.h"
#include "debuglink/dlog.h"

enum { LNK_FREE = 0, LNK_CONNECTED, LNK_RELEASING };
typedef ax25_link_unacked_t unacked_t;
typedef ax25_link_slot_t link_t;

static void put_addr(uint8_t *out, const ax25_addr_t *a, bool last, bool cbit)
{
	for (size_t i = 0; i < AX25_CALL_LEN; i++) {
		char c = a->call[i] != '\0' ? a->call[i] : ' ';
		out[i] = (uint8_t)((uint8_t)c << 1);
	}
	out[6] = (uint8_t)(0x60 | ((a->ssid & 0x0Fu) << 1) | (cbit ? 0x80u : 0u) | (last ? 1u : 0u));
}

// Assemble a frame without digipeaters. pid < 0: a U or S frame (no PID byte); otherwise an I or UI frame
static size_t enc(const ax25_addr_t *dst, const ax25_addr_t *src, uint8_t control, int pid,
		  const uint8_t *info, size_t n, bool command, uint8_t *out, size_t max)
{
	size_t need = 2u * AX25_ADDR_BYTES + 1u + (pid >= 0 ? 1u : 0u) + n;
	if (need > max || n > AX25L_INFO_MAX) {
		return 0;
	}
	put_addr(out, dst, false, command);
	put_addr(out + AX25_ADDR_BYTES, src, true, !command);
	size_t k = 2u * AX25_ADDR_BYTES;
	out[k++] = control;
	if (pid >= 0) {
		out[k++] = (uint8_t)pid;
	}
	if (n != 0) {
		memcpy(out + k, info, n);
	}
	return k + n;
}

static bool call_char(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static bool get_addr(const uint8_t *p, ax25_addr_t *a)
{
	size_t i;
	memset(a, 0, sizeof(*a));
	for (i = 0; i < AX25_CALL_LEN; i++) {
		if (p[i] & 1u) {
			return false;
		}
		char c = (char)(p[i] >> 1);
		if (c == ' ') {
			break;
		}
		if (!call_char(c)) {
			return false;
		}
		a->call[i] = c;
	}
	for (size_t k = i; k < AX25_CALL_LEN; k++) {
		if ((p[k] >> 1) != ' ') {
			return false;
		}
	}
	if (i == 0) {
		return false;
	}
	a->ssid = (uint8_t)((p[6] >> 1) & 0x0Fu);
	a->h = (p[6] & 0x80u) != 0;
	return true;
}

// Parse addresses and the control field. has_pid says whether a PID byte follows (I and UI frames)
static bool dec(const uint8_t *buf, size_t n, ax25_frame_t *f, bool *has_pid)
{
	memset(f, 0, sizeof(*f));
	size_t naddr = 0;
	for (;;) {
		if ((naddr + 1u) * AX25_ADDR_BYTES > n || naddr >= 2u + AX25_MAX_DIGIS) {
			return false;
		}
		const uint8_t *p = buf + naddr * AX25_ADDR_BYTES;
		ax25_addr_t *a = naddr == 0 ? &f->dst : naddr == 1 ? &f->src : &f->digi[naddr - 2u];
		if (!get_addr(p, a)) {
			return false;
		}
		naddr++;
		if (p[6] & 1u) {
			break;
		}
	}
	if (naddr < 2) {
		return false;
	}
	f->ndigi = (uint8_t)(naddr - 2u);
	size_t k = naddr * AX25_ADDR_BYTES;
	if (k + 1u > n) {
		return false;
	}
	f->control = buf[k++];
	*has_pid = (f->control & 0x01u) == 0u || (f->control & 0xEFu) == AX25L_UI;
	if (*has_pid) {
		if (k + 1u > n) {
			return false;
		}
		f->pid = buf[k++];
	}
	f->info = buf + k;
	f->info_len = (uint16_t)(n - k);
	return f->info_len <= AX25L_INFO_MAX;
}

static uint8_t i_ns(uint8_t c) { return (uint8_t)((c >> 1) & 0x07u); }
static uint8_t i_nr(uint8_t c) { return (uint8_t)((c >> 5) & 0x07u); }
static uint8_t s_nr(uint8_t c) { return (uint8_t)((c >> 5) & 0x07u); }
static uint8_t pf(uint8_t c) { return (uint8_t)((c >> 4) & 0x01u); }
static uint8_t i_ctrl(uint8_t ns, uint8_t nr, uint8_t p)
{
	return (uint8_t)(((nr & 0x07u) << 5) | ((p & 0x01u) << 4) | ((ns & 0x07u) << 1));
}
static uint8_t s_ctrl(uint8_t kind, uint8_t nr, uint8_t p)
{
	return (uint8_t)(((nr & 0x07u) << 5) | ((p & 0x01u) << 4) | (kind & 0x0Fu));
}

static void send_u(ax25_link_t *l, const ax25_addr_t *peer, uint8_t control, bool command)
{
	uint8_t out[2 * AX25_ADDR_BYTES + 1];
	size_t n = enc(peer, &l->me, control, -1, NULL, 0, command, out, sizeof(out));
	if (n != 0 && l->tx != NULL) {
		l->tx(out, n, l->tx_ctx);
		l->v.tx_frames++;
	}
}

static void send_s(ax25_link_t *l, link_t *lk, uint8_t kind, uint8_t nr, uint8_t p)
{
	uint8_t out[2 * AX25_ADDR_BYTES + 1];
	size_t n = enc(&lk->peer, &l->me, s_ctrl(kind, nr, p), -1, NULL, 0, false, out, sizeof(out));
	if (n != 0 && l->tx != NULL) {
		l->tx(out, n, l->tx_ctx);
		l->v.tx_frames++;
	}
}

static link_t *find_peer(ax25_link_t *l, const ax25_addr_t *peer)
{
	for (uint8_t i = 0; i < AX25L_MAX_LINKS; i++) {
		if (l->link[i].state != LNK_FREE && ax25_addr_equal(&l->link[i].peer, peer)) {
			return &l->link[i];
		}
	}
	return NULL;
}

static uint8_t index_of(ax25_link_t *l, link_t *lk)
{
	return (uint8_t)(lk - l->link);
}

static link_t *alloc_link(ax25_link_t *l)
{
	for (uint8_t i = 0; i < AX25L_MAX_LINKS; i++) {
		if (l->link[i].state == LNK_FREE) {
			return &l->link[i];
		}
	}
	return NULL;
}

static void reset_link(link_t *lk)
{
	lk->vs = lk->va = lk->vr = 0;
	lk->retries = 0;
	lk->nslots = 0;
	lk->t1_armed = false;
	lk->disc_len = 0;
}

static void free_link(ax25_link_t *l, link_t *lk, bool remote)
{
	uint8_t idx = index_of(l, lk);
	lk->state = LNK_FREE;
	lk->nslots = 0;
	lk->t1_armed = false;
	l->v.disconnects++;
	if (l->cb != NULL && l->cb->disconnected != NULL) {
		l->cb->disconnected(l, idx, remote);
	}
}

// The peer acknowledged frames up to nr
static void ack_to(link_t *lk, uint8_t nr)
{
	for (uint8_t guard = 0; guard < AX25L_WINDOW; guard++) {
		if (lk->va == nr || lk->nslots == 0) {
			break;
		}
		for (uint8_t i = 1; i < lk->nslots; i++) {
			lk->unacked[i - 1] = lk->unacked[i];
		}
		lk->nslots--;
		lk->va = (uint8_t)((lk->va + 1) & 0x07u);
	}
	if (lk->nslots == 0) {
		lk->t1_armed = false;
	}
}

static void retransmit(ax25_link_t *l, link_t *lk)
{
	if (l->tx == NULL) {
		return;
	}
	for (uint8_t i = 0; i < lk->nslots; i++) {
		l->tx(lk->unacked[i].data, lk->unacked[i].len, l->tx_ctx);
		l->v.tx_frames++;
	}
	l->v.retries++;
}

void ax25_link_init(ax25_link_t *l, const ax25_addr_t *me, const ax25_link_cb_t *cb,
		    bool (*tx)(const uint8_t *, size_t, void *), void *tx_ctx)
{
	memset(l, 0, sizeof(*l));
	l->me = *me;
	l->cb = cb;
	l->tx = tx;
	l->tx_ctx = tx_ctx;
}

bool ax25_link_rx(ax25_link_t *l, const uint8_t *frame, size_t n, uint32_t now_ms)
{
	ax25_frame_t f;
	bool has_pid;
	if (!dec(frame, n, &f, &has_pid)) {
		return false;
	}
	if (!ax25_addr_equal(&f.dst, &l->me)) {
		return false;
	}
	dlog("bbs rx %s-%u>%s-%u c=%02x", f.src.call, f.src.ssid, f.dst.call, f.dst.ssid, f.control);
	l->v.rx_frames++;

	bool is_i = (f.control & 0x01u) == 0u;
	bool is_s = (f.control & 0x03u) == 0x01u;
	bool is_u = (f.control & 0x03u) == 0x03u;
	uint8_t kind = (uint8_t)(f.control & 0xEFu);	// U frame without its P/F bit
	link_t *lk = find_peer(l, &f.src);

	if (is_u && kind == AX25L_UI) {
		return false;	// APRS (or another UI user) keeps UI frames
	}

	if (is_u) {
		if (kind == AX25L_SABM) {
			if (lk == NULL) {
				lk = alloc_link(l);
			}
			if (lk == NULL) {
				dlog("bbs busy -> DM");
				send_u(l, &f.src, (uint8_t)(AX25L_DM | AX25L_PF), false);	// busy
				l->v.drops++;
				return true;
			}
			lk->peer = f.src;
			reset_link(lk);
			lk->state = LNK_CONNECTED;
			dlog("bbs SABM -> UA (peer %s-%u)", f.src.call, f.src.ssid);
			send_u(l, &f.src, (uint8_t)(AX25L_UA | AX25L_PF), false);
			l->v.connects++;
			if (l->cb != NULL && l->cb->connected != NULL) {
				l->cb->connected(l, index_of(l, lk));
			}
			return true;
		}
		if (kind == AX25L_DISC) {
			if (lk != NULL) {
				send_u(l, &f.src, (uint8_t)(AX25L_UA | AX25L_PF), false);
				free_link(l, lk, true);
			} else {
				send_u(l, &f.src, (uint8_t)(AX25L_DM | AX25L_PF), false);
			}
			return true;
		}
		if (kind == AX25L_UA || kind == AX25L_DM) {
			if (lk != NULL) {
				free_link(l, lk, lk->state == LNK_RELEASING ? false : true);
			}
			return true;
		}
		return true;	// SABME, FRMR, ...: a server ignores them
	}

	if (lk == NULL) {
		if (is_i) {
			send_u(l, &f.src, (uint8_t)(AX25L_DM | AX25L_PF), false);
		}
		return true;
	}

	if (is_i) {
		uint8_t ns = i_ns(f.control);
		uint8_t nr = i_nr(f.control);
		uint8_t p = pf(f.control);
		ack_to(lk, nr);
		dlog("bbs I ns=%u nr=%u n=%u", ns, nr, (unsigned)n);
		if (ns == lk->vr) {
			if (l->cb != NULL && l->cb->data != NULL && f.info_len != 0) {
				l->cb->data(l, index_of(l, lk), f.info, f.info_len);
			}
			lk->vr = (uint8_t)((lk->vr + 1) & 0x07u);
			send_s(l, lk, AX25L_RR, lk->vr, p);	// RR acknowledges and answers a poll
		} else {
			// A duplicate or a gap: an RR that carries N(R) = vr acknowledges what we have and tells the peer
			// where to resend from. (A REJ would not acknowledge, so a retransmitted copy would never clear.)
			send_s(l, lk, AX25L_RR, lk->vr, p);
		}
		return true;
	}

	if (is_s) {
		uint8_t nr = s_nr(f.control);
		uint8_t p = pf(f.control);
		ack_to(lk, nr);
		if (p != 0) {
			send_s(l, lk, AX25L_RR, lk->vr, 1);
		}
		return true;
	}

	return true;
}

void ax25_link_tick(ax25_link_t *l, uint32_t now_ms)
{
	for (uint8_t i = 0; i < AX25L_MAX_LINKS; i++) {
		link_t *lk = &l->link[i];
		if (lk->state == LNK_FREE || !lk->t1_armed) {
			continue;
		}
		if ((int32_t)(now_ms - lk->t1_ms) < 0) {
			continue;
		}
		if (lk->state == LNK_RELEASING || lk->nslots == 0) {
			lk->retries++;
			if (lk->retries > AX25L_N2) {
				free_link(l, lk, false);
				continue;
			}
			if (lk->state == LNK_RELEASING) {
				if (l->tx != NULL && lk->disc_len != 0) {
					l->tx(lk->disc, lk->disc_len, l->tx_ctx);
					l->v.tx_frames++;
				}
			}
			lk->t1_ms = now_ms + AX25L_T1_MS;
			continue;
		}
		lk->retries++;
		if (lk->retries > AX25L_N2) {
			send_u(l, &lk->peer, (uint8_t)(AX25L_DM | AX25L_PF), false);
			free_link(l, lk, false);
			continue;
		}
		retransmit(l, lk);
		lk->t1_ms = now_ms + AX25L_T1_MS;
	}
}

bool ax25_link_send(ax25_link_t *l, uint8_t idx, const uint8_t *p, size_t n, uint32_t now_ms)
{
	if (idx >= AX25L_MAX_LINKS || n > AX25L_INFO_MAX) {
		return false;
	}
	link_t *lk = &l->link[idx];
	if (lk->state != LNK_CONNECTED || lk->nslots >= AX25L_WINDOW) {
		return false;
	}
	unacked_t *u = &lk->unacked[lk->nslots];
	uint8_t ctrl = i_ctrl(lk->vs, lk->vr, 0);
	u->len = (uint16_t)enc(&lk->peer, &l->me, ctrl, AX25L_PID_NONE, p, n, true, u->data, sizeof(u->data));
	if (u->len == 0) {
		return false;
	}
	u->ns = lk->vs;
	lk->nslots++;
	lk->vs = (uint8_t)((lk->vs + 1) & 0x07u);
	if (l->tx != NULL) {
		l->tx(u->data, u->len, l->tx_ctx);
		l->v.tx_frames++;
	}
	if (!lk->t1_armed) {
		lk->t1_armed = true;
		lk->t1_ms = now_ms + AX25L_T1_MS;
	}
	return true;
}

void ax25_link_disconnect(ax25_link_t *l, uint8_t idx, uint32_t now_ms)
{
	if (idx >= AX25L_MAX_LINKS) {
		return;
	}
	link_t *lk = &l->link[idx];
	if (lk->state != LNK_CONNECTED) {
		return;
	}
	lk->disc_len = enc(&lk->peer, &l->me, (uint8_t)(AX25L_DISC | AX25L_PF), -1, NULL, 0, true,
			   lk->disc, sizeof(lk->disc));
	lk->state = LNK_RELEASING;
	lk->nslots = 0;
	lk->retries = 0;
	lk->t1_armed = true;
	lk->t1_ms = now_ms + AX25L_T1_MS;
	if (l->tx != NULL && lk->disc_len != 0) {
		l->tx(lk->disc, lk->disc_len, l->tx_ctx);
		l->v.tx_frames++;
	}
}

bool ax25_link_connected(const ax25_link_t *l, uint8_t idx)
{
	return idx < AX25L_MAX_LINKS && l->link[idx].state == LNK_CONNECTED;
}

bool ax25_link_peer(const ax25_link_t *l, uint8_t idx, char *out, size_t max)
{
	if (idx >= AX25L_MAX_LINKS || l->link[idx].state == LNK_FREE) {
		return false;
	}
	char tmp[AX25_CALL_LEN + 8];
	size_t n = ax25_addr_format(&l->link[idx].peer, false, tmp);
	if (n == 0 || n >= max) {
		return false;
	}
	memcpy(out, tmp, n);
	out[n] = '\0';
	return true;
}

void ax25_link_stats(const ax25_link_t *l, ax25_link_stats_t *s)
{
	*s = l->v;
}
