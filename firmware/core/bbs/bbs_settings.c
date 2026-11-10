// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
#include <string.h>
#include "bbs/bbs_settings.h"
#include "settings/settings.h"

static void copy_str(char *dst, size_t max, const char *src)
{
	size_t i = 0;
	for (; i < max && src[i] != '\0'; i++) {
		dst[i] = src[i];
	}
	for (; i < max; i++) {
		dst[i] = '\0';
	}
}

void bbs_settings_load(bbs_cfg_t *c)
{
	settings_t st;
	settings_read(&st);
	bbs_cfg_defaults(c);
	if ((st.bbs_flags & BBS_SET_STORED) == 0) {
		return;
	}
	if (st.bbs_freq_hz != 0) {
		c->freq_hz = st.bbs_freq_hz;
	}
	copy_str(c->call, sizeof(c->call), st.bbs_call);
	copy_str(c->name, sizeof(c->name), st.bbs_name);
	copy_str(c->sysop, sizeof(c->sysop), st.bbs_sysop);
	if (c->call[0] == '\0') {
		strcpy(c->call, BBS_CALL_DEFAULT);
	}
	c->ssid = st.bbs_ssid;
	c->bw = st.bbs_bw;
	c->txdelay = st.bbs_txdelay;
	c->cwid = st.bbs_cwid;
	c->on_at_boot = (st.bbs_flags & BBS_SET_BOOT) != 0;
	bbs_cfg_normalize(c);
}

void bbs_settings_save(const bbs_cfg_t *c, uint32_t now_ms)
{
	bbs_cfg_t n = *c;
	bbs_cfg_normalize(&n);
	settings_t *st = settings_begin();
	st->bbs_freq_hz = n.freq_hz;
	st->bbs_flags = (uint8_t)(BBS_SET_STORED | (n.on_at_boot ? BBS_SET_BOOT : 0));
	st->bbs_ssid = n.ssid;
	st->bbs_bw = n.bw;
	st->bbs_txdelay = n.txdelay;
	st->bbs_cwid = n.cwid;
	copy_str(st->bbs_call, sizeof(st->bbs_call), n.call);
	copy_str(st->bbs_name, sizeof(st->bbs_name), n.name);
	copy_str(st->bbs_sysop, sizeof(st->bbs_sysop), n.sysop);
	settings_end(now_ms);
}
