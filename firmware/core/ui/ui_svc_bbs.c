// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The ui_svc.h calls for the packet BBS (CONFIG_BBS): configure it, start/stop it (tuning the radio), and browse the
// mailbox the radio keeps. The engine runs in the radio task; this file is the UI task's side, using the BBS's
// thread-safe calls.
#include "config.h"

#if CONFIG_BBS
#include <string.h>
#include "ui_svc.h"
#include "bbs/bbs.h"
#include "bbs/bbs_config.h"
#include "bbs/bbs_radio.h"
#include "radio/radio.h"
#include "board.h"
#include "hal_time.h"

static void from_ui(const ui_bbs_t *in, bbs_cfg_t *c)
{
	memset(c, 0, sizeof(*c));
	memcpy(c->call, in->call, sizeof(c->call));
	c->call[sizeof(c->call) - 1] = '\0';
	memcpy(c->name, in->name, sizeof(c->name));
	c->name[sizeof(c->name) - 1] = '\0';
	memcpy(c->sysop, in->sysop, sizeof(c->sysop));
	c->sysop[sizeof(c->sysop) - 1] = '\0';
	c->ssid = in->ssid;
	c->freq_hz = in->freq_hz;
	c->bw = in->bw;
	c->txdelay = in->txdelay;
	c->cwid = in->cwid;
	c->on_at_boot = in->on_at_boot;
	bbs_cfg_normalize(c);
}

bool ui_svc_bbs_get(ui_bbs_t *out)
{
	bbs_cfg_t c;
	bbs_status_t st;
	bbs_get_config(&c);
	bbs_get_status(&st);
	memset(out, 0, sizeof(*out));
	memcpy(out->call, c.call, sizeof(out->call));
	memcpy(out->name, c.name, sizeof(out->name));
	memcpy(out->sysop, c.sysop, sizeof(out->sysop));
	out->ssid = c.ssid;
	out->freq_hz = c.freq_hz;
	out->bw = c.bw;
	out->txdelay = c.txdelay;
	out->cwid = c.cwid;
	out->on_at_boot = c.on_at_boot;
	out->running = st.enabled;
	out->messages = st.messages;
	out->links = st.links;
	memcpy(out->peer, st.peer, sizeof(out->peer));
	return true;
}

static uint8_t radio_bw(uint8_t bbs_bw)
{
	return bbs_bw == BBS_BW_12K5 ? RADIO_BW_12K5 : RADIO_BW_25K;
}

static void tune(const bbs_cfg_t *c)
{
	radio_req_vfo(true);
	radio_req_mode(RADIO_MODE_FM);
	radio_req_vfo_set(c->freq_hz);
	radio_req_bandwidth((radio_bw_t)radio_bw(c->bw));
}

bool ui_svc_bbs_set(const ui_bbs_t *in)
{
	bbs_cfg_t c;
	from_ui(in, &c);
	if (!bbs_cfg_valid(&c)) {
		return false;
	}
	bool running = bbs_enabled();
	bbs_radio_save(&c, hal_millis());
	if (running) {
		tune(&c);	// follow a changed frequency or bandwidth at once
	}
	return true;
}

bool ui_svc_bbs_start(void)
{
	bbs_cfg_t c;
	bbs_get_config(&c);
	if (!bbs_cfg_valid(&c) || !board_in_band(c.freq_hz, true)) {
		return false;
	}
	tune(&c);
	bbs_radio_set_enabled(true, hal_millis());
	return true;
}

void ui_svc_bbs_stop(void)
{
	bbs_radio_set_enabled(false, hal_millis());
}

bool ui_svc_bbs_running(void)
{
	return bbs_enabled();
}

static void msg_out(const bbs_msg_t *m, ui_bbs_msg_t *out)
{
	memset(out, 0, sizeof(*out));
	out->msgno = m->msgno;
	out->type = m->type;
	out->time = m->time;
	out->body_len = m->body_len;
	memcpy(out->from, m->from, sizeof(out->from));
	out->from[sizeof(out->from) - 1] = '\0';
	memcpy(out->subject, m->subject, sizeof(out->subject));
	out->subject[sizeof(out->subject) - 1] = '\0';
}

uint16_t ui_svc_bbs_msg_count(void)
{
	return bbs_ui_count();
}

bool ui_svc_bbs_msg(uint16_t i, ui_bbs_msg_t *out)
{
	bbs_msg_t m;
	if (!bbs_ui_msg(i, &m, NULL, 0)) {
		return false;
	}
	msg_out(&m, out);
	return true;
}

bool ui_svc_bbs_body(uint16_t i, char *buf, size_t len)
{
	bbs_msg_t m;
	return bbs_ui_msg(i, &m, buf, len);
}

bool ui_svc_bbs_msg_no(uint16_t msgno, ui_bbs_msg_t *out)
{
	bbs_msg_t m;
	if (!bbs_ui_msg_no(msgno, &m, NULL, 0)) {
		return false;
	}
	msg_out(&m, out);
	return true;
}

bool ui_svc_bbs_body_no(uint16_t msgno, char *buf, size_t len)
{
	bbs_msg_t m;
	return bbs_ui_msg_no(msgno, &m, buf, len);
}

void ui_svc_bbs_delete(uint16_t msgno)
{
	bbs_ui_kill(msgno);
}

void ui_svc_bbs_clear(void)
{
	bbs_ui_clear();
}

uint8_t ui_svc_bbs_poll(bool *ok)
{
	return bbs_ui_poll(ok);
}

#endif
