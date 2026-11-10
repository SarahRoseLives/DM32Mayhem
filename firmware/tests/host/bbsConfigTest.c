// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Host test of the BBS configuration (core/bbs/bbs_config.c): defaults, validation, normalization and the call text.

#include <stdio.h>
#include <string.h>
#include "bbs/bbs_config.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
	bbs_cfg_t c;
	bbs_cfg_defaults(&c);
	CHECK(strcmp(c.call, "N0CALL") == 0);
	CHECK(c.ssid == 0);
	CHECK(c.freq_hz == BBS_FREQ_DEFAULT);
	CHECK(c.bw == BBS_BW_25K);
	CHECK(strcmp(c.name, BBS_NAME_DEFAULT) == 0);
	CHECK(strcmp(c.sysop, "N0CALL") == 0);
	CHECK(c.cwid == 0);
	CHECK(bbs_cfg_valid(&c));

	char out[16];
	CHECK(bbs_cfg_format_call(&c, out, sizeof out) > 0);
	CHECK(strcmp(out, "N0CALL") == 0);
	c.ssid = 3;
	CHECK(bbs_cfg_format_call(&c, out, sizeof out) > 0);
	CHECK(strcmp(out, "N0CALL-3") == 0);
	c.ssid = 0;

	// Lower case is folded, the sysop may be empty
	strcpy(c.call, "n0call");
	c.sysop[0] = '\0';
	bbs_cfg_normalize(&c);
	CHECK(strcmp(c.call, "N0CALL") == 0);
	CHECK(bbs_cfg_valid(&c));

	// A bad call or no frequency is refused
	strcpy(c.call, "");
	CHECK(!bbs_cfg_valid(&c));
	strcpy(c.call, "N0-CAL");
	CHECK(!bbs_cfg_valid(&c));
	strcpy(c.call, "N0CALL");
	c.freq_hz = 0;
	CHECK(!bbs_cfg_valid(&c));
	c.freq_hz = BBS_FREQ_DEFAULT;
	CHECK(bbs_cfg_valid(&c));

	// A bad sysop is refused, an empty one allowed
	strcpy(c.sysop, "N0-T");
	CHECK(!bbs_cfg_valid(&c));
	c.sysop[0] = '\0';
	CHECK(bbs_cfg_valid(&c));

	// A bad CW ID interval collapses to off
	c.cwid = 0x7F;
	bbs_cfg_normalize(&c);
	CHECK(c.cwid == 0);

	// The bandwidth collapses to a known code
	c.bw = 0x7F;
	bbs_cfg_normalize(&c);
	CHECK(c.bw == BBS_BW_25K);

	printf("%s: %d failure(s)\n", fails == 0 ? "# PASSED" : "# FAILED", fails);
	return fails != 0;
}
