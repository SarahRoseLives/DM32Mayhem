// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// ACARS receive against a raw 8 kHz s16 audio file (the AM-demodulated sigidwiki IQ sample by default). Prints the
// frames; the demo with the sample is five (VQ-BKV, TC-JSN, VQ-BRE, EI-UNP, VQ-BRE).
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "acars/acars.h"

static void on_msg(const acars_msg_t *m, void *ctx)
{
	(void)ctx;
	printf("mode %c addr %-8s label %s bid %c err %u lvl %d :: %s\n", m->mode, m->addr, m->label, m->bid,
	       (unsigned)m->err, (int)m->level, m->text);
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/tmp/acars/acars_env8000.raw";
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		printf("acarsTest: skipped (no %s)\n", path);
		return 0;
	}
	acars_rx_t d;
	acars_rx_init(&d, argc > 2 ? (uint32_t)atoi(argv[2]) : 8000u, on_msg, NULL);
	if (argc > 3) {
		acars_rx_set_fo(&d, (float)atof(argv[3]));
	}
	int16_t buf[160];
	size_t n;
	while ((n = fread(buf, sizeof buf[0], sizeof buf / sizeof buf[0], f)) != 0) {
		acars_rx_feed(&d, buf, n);
	}
	fclose(f);
	printf("acarsTest: frames %u, crc errors %u, samples %u\n", (unsigned)d.frames, (unsigned)d.errors,
	       (unsigned)d.samples);
	return d.frames >= 4 ? 0 : 1;
}
