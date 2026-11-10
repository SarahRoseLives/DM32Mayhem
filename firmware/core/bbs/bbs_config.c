// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
#include <stdio.h>
#include <string.h>
#include "bbs/bbs_config.h"

void bbs_cfg_defaults(bbs_cfg_t *c)
{
	memset(c, 0, sizeof(*c));
	strcpy(c->call, BBS_CALL_DEFAULT);
	strcpy(c->name, BBS_NAME_DEFAULT);
	strcpy(c->sysop, BBS_SYSOP_DEFAULT);
	c->freq_hz = BBS_FREQ_DEFAULT;
	c->ssid = BBS_SSID_DEFAULT;
	c->bw = BBS_BW_25K;
	c->txdelay = BBS_TXDELAY_DEFAULT;
	c->cwid = 0;
	c->on_at_boot = false;
}

static void upcase(char *s)
{
	for (; *s != '\0'; s++) {
		if (*s >= 'a' && *s <= 'z') {
			*s = (char)(*s - 'a' + 'A');
		}
	}
}

void bbs_cfg_normalize(bbs_cfg_t *c)
{
	c->call[BBS_CALL_MAX] = '\0';
	c->name[BBS_NAME_MAX] = '\0';
	c->sysop[BBS_SYSOP_MAX] = '\0';
	upcase(c->call);
	upcase(c->sysop);
	c->ssid &= 0x0Fu;
	if (c->bw != BBS_BW_12K5 && c->bw != BBS_BW_25K) {
		c->bw = BBS_BW_25K;
	}
	if (c->cwid > 4) {
		c->cwid = 0;
	}
}

static bool call_ok(const char *s, size_t max)
{
	size_t n = 0;
	for (; s[n] != '\0'; n++) {
		char c = s[n];
		if (n >= max || !((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
			return false;
		}
	}
	return n >= 1;
}

bool bbs_cfg_valid(const bbs_cfg_t *c)
{
	bbs_cfg_t n = *c;
	bbs_cfg_normalize(&n);
	if (!call_ok(n.call, BBS_CALL_MAX) || n.freq_hz == 0) {
		return false;
	}
	if (n.sysop[0] != '\0' && !call_ok(n.sysop, BBS_SYSOP_MAX)) {
		return false;
	}
	return true;
}

size_t bbs_cfg_format_call(const bbs_cfg_t *c, char *out, size_t max)
{
	bbs_cfg_t n = *c;
	bbs_cfg_normalize(&n);
	char tmp[BBS_CALL_MAX + 4];
	int len;
	if (n.ssid != 0) {
		len = snprintf(tmp, sizeof(tmp), "%s-%u", n.call, n.ssid);
	} else {
		len = snprintf(tmp, sizeof(tmp), "%s", n.call);
	}
	if (len <= 0 || (size_t)len >= max) {
		return 0;
	}
	memcpy(out, tmp, (size_t)len + 1u);
	return (size_t)len;
}
