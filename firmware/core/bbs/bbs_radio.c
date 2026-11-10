// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Radio glue for the packet BBS (bbs_radio.h). The APRS engine already has the AFSK modulator, the HDLC framing, the
// CSMA and the keying, so the BBS rides on it: park the APRS channel on the BBS frequency with receive on, turn the
// APRS beacons off, and queue BBS frames through aprs_queue_raw_frame().

#include <string.h>
#include "bbs/bbs_radio.h"
#include "bbs/bbs.h"
#include "bbs/bbs_flash.h"
#include "bbs/bbs_settings.h"
#include "aprs/aprs.h"
#include "storage/storage.h"

static bbs_store_t store;

static bool bbs_tx(const uint8_t *frame, size_t n, void *ctx)
{
	(void)ctx;
	return aprs_queue_raw_frame(frame, n);
}

void bbs_radio_apply(const bbs_cfg_t *cfg, uint32_t now_ms)
{
	aprs_cfg_t a;
	aprs_get_cfg(&a);
	a.region = APRS_REGION_CUSTOM;
	a.freq_hz = cfg->freq_hz;
	a.narrow = cfg->bw == BBS_BW_12K5;	// 12.5 kHz, else 25 kHz
	a.rx_on = true;				// hear callers on the BBS channel
	a.tx_on = false;			// no APRS beacons on the BBS channel
	if (cfg->txdelay != 0) {
		a.txdelay = cfg->txdelay;
	}
	aprs_set_cfg(&a, now_ms);
}

void bbs_radio_set_enabled(bool on, uint32_t now_ms)
{
	(void)now_ms;
	bbs_set_enabled(on);
}

void bbs_radio_save(const bbs_cfg_t *cfg, uint32_t now_ms)
{
	bbs_settings_save(cfg, now_ms);
	bbs_radio_apply(cfg, now_ms);
	bbs_set_config(cfg);
}

void bbs_start(uint32_t now_ms)
{
	bbs_cfg_t cfg;
	bbs_settings_load(&cfg);

	bbs_set_store_lock(storage_lock, storage_unlock);
	bbs_store_backend_t be;
	if (bbs_flash_backend(&be)) {
		bbs_store_init(&store, &be);
	} else {
		memset(&store, 0, sizeof(store));	// no usable flash: a read-only, empty mailbox
	}
	bbs_init(&store, bbs_tx, NULL);
	bbs_radio_apply(&cfg, now_ms);
	bbs_set_config(&cfg);
	bbs_set_enabled(cfg.on_at_boot);
}
