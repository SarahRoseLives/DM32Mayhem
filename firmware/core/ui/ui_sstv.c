// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// SSTV receive (CONFIG_SSTV_RX, docs/spec/sstv.md): the FM receive audio goes to the decoder in the UI tick, like the
// RTTY/ACARS receivers, and each pixel is drawn straight into the frame buffer as it arrives (no image buffer, so the
// ~15 KB free RAM is enough). 320 x 256 scales to 240 x 192 on the DM32.
#include "config.h"
#if CONFIG_SSTV_RX
#include "ui_nav.h"
#include "fmt.h"
#include "i18n.h"
#include "board.h"
#include "radio/radio.h"
#include "sstv/sstv_rx.h"

#define SSTV_HZ		144500000u	// the 2 m SSTV calling frequency; the decoder takes any FM audio
#define SSTV_IMG_TOP	44
#define SSTV_IMG_W	240
#define SSTV_IMG_H	192

static sstv_rx_t rx;
static bool active, starting;
static uint32_t hz = SSTV_HZ, start_ms;
static uint8_t mode, lines_done;
static uint16_t lines_total;
static bool dirty, needs_clear = true;
static const char *sstv_err;

static const char *mode_name(uint8_t m)
{
	switch (m) {
	case SSTV_MODE_MARTIN_M1: return "Martin M1";
	case SSTV_MODE_MARTIN_M2: return "Martin M2";
	case SSTV_MODE_SCOTTIE_S1: return "Scottie S1";
	case SSTV_MODE_SCOTTIE_S2: return "Scottie S2";
	default: return "SSTV";
	}
}

static void on_pixel(void *ctx, uint16_t x, uint16_t y, uint16_t color)
{
	(void)ctx;
	int sx = (int)((uint32_t)x * SSTV_IMG_W / SSTV_LINE_MAX);
	int sy = (int)((uint32_t)y * SSTV_IMG_H / SSTV_LINES_MAX);
	gfx_pixel(sx, SSTV_IMG_TOP + sy, color);
}

static void on_line(void *ctx, uint16_t line, uint16_t lines)
{
	(void)ctx; (void)lines;
	lines_done = (uint8_t)(line + 1);
	dirty = true;
}

static void on_mode(void *ctx, uint8_t vis, uint8_t m, uint16_t w, uint16_t h)
{
	(void)ctx; (void)vis; (void)w;
	if (m != SSTV_MODE_NONE) {
		mode = m;
		lines_done = 0;
		lines_total = h;
		gfx_fill(0, SSTV_IMG_TOP, SSTV_IMG_W, SSTV_IMG_H, RGB565(0, 0, 0));	// a new picture
	}
	dirty = true;
}

static void sstv_stop(void)
{
	if (active) {
		if (board.afsk != NULL && board.afsk->rx_stop != NULL) {
			board.afsk->rx_stop();
		}
		active = false;
	}
}

// Drain the receive ring into the decoder (a few reads a tick, as RTTY does)
static bool sstv_pump(void)
{
	if (!active || board.afsk == NULL || board.afsk->rx_read == NULL) {
		return false;
	}
	int16_t buf[160];
	uint16_t n;
	for (int budget = 8; budget > 0 && (n = board.afsk->rx_read(buf, (uint16_t)(sizeof buf / sizeof buf[0]))) != 0;
	     budget--) {
		sstv_rx_feed(&rx, buf, n);
	}
	bool redraw = dirty;
	dirty = false;
	return redraw;
}

// Once the radio task reports VFO FM (the app queued it on entry), open the receive audio. Opening it before the
// mode changes is what returned "audio busy": the DMR glue's FM-RX hook is not free while the old channel is up.
static void sstv_open(void)
{
	if (board.afsk == NULL || board.afsk->rx_start == NULL || board.afsk->rx_read == NULL) {
		sstv_err = TR("No audio path");
		starting = false;
		return;
	}
	if (!board.afsk->rx_start()) {
		return;	// the ring is held a poll by the APRS: sstv_tick retries until the timeout
	}
	sstv_rx_begin(&rx, board.afsk->rx_rate_hz != 0 ? board.afsk->rx_rate_hz : 8000u,
		      on_pixel, on_line, on_mode, NULL);
	active = true;
	starting = false;
}

static bool sstv_tick(uint32_t now)
{
	if (starting) {
		const radio_view_t *v = ui_view();
		if (v != NULL && v->vfo && v->ch.mode == RADIO_MODE_FM && v->state != RS_TX) {
			sstv_open();
			if (active) {
				ui_dirty();
				return true;
			}
		}
		if (start_ms == 0) {
			start_ms = now;
		} else if (now - start_ms > 2000u) {
			if (sstv_err == NULL) {
				sstv_err = TR("Audio busy");
			}
			starting = false;
		}
		ui_dirty();
		return true;
	}
	return sstv_pump();
}

static void sstv_draw(void)
{
	// The picture lives in the frame buffer, drawn as it decodes: clear only the strip and the footer, never the image
	if (needs_clear) {
		gfx_fill(0, SSTV_IMG_TOP, SSTV_IMG_W, SSTV_IMG_H, RGB565(0, 0, 0));
		needs_clear = false;
	}
	gfx_fill(0, 0, gfx_width(), SSTV_IMG_TOP, ui_col.bg);
	gfx_fill(0, SSTV_IMG_TOP + SSTV_IMG_H, gfx_width(), gfx_height() - (SSTV_IMG_TOP + SSTV_IMG_H), ui_col.bg);
	char sub[48];
	sbuf_t b;
	sb_init(&b, sub, sizeof sub);
	if (sstv_err != NULL) {
		sb_str(&b, sstv_err);
	} else if (mode != SSTV_MODE_NONE) {
		sb_str(&b, mode_name(mode));
		sb_str(&b, "  ");
		sb_u32(&b, lines_done, 1);
		sb_chr(&b, '/');
		sb_u32(&b, lines_total, 1);
	} else if (starting) {
		sb_str(&b, TR("Tuning"));
	} else {
		sb_str(&b, TR("Waiting"));
		sb_str(&b, " ");
		sb_u32(&b, rx.samples / 1000u, 1);
		sb_str(&b, "k st");
		sb_u32(&b, rx.state, 1);
		sb_str(&b, " ");
		sb_u32(&b, rx.vis_bits, 1);
		sb_str(&b, "/");
		sb_u32(&b, rx.vis_count, 1);
		sb_str(&b, " ");
		sb_i32(&b, (int)rx.hz);
	}
	ui_draw_top(ui_view(), &ui_st, "SSTV", sub);
	ui_draw_bottom(&ui_st, TR("Back stop"));
	ui_draw_banner(&ui_st);
}

static bool sstv_key(const input_event_t *ev)
{
	if (ev->gesture != GESTURE_PRESS && ev->gesture != GESTURE_REPEAT) {
		return false;
	}
	if (ev->action == ACT_NAV_UP || ev->action == ACT_NAV_DOWN) {
		int dir = ev->action == ACT_NAV_UP ? 1 : -1;
		if (hz >= 144000000u && hz <= 145000000u) {
			hz = (uint32_t)((int32_t)hz + dir * 25000);
			if (active) {
				radio_req_vfo_set(hz);
			}
			ui_dirty();
		}
		return true;
	}
	return false;
}

static void sstv_enter(void)
{
	needs_clear = true;
	sstv_err = NULL;
	active = false;
	starting = true;
	start_ms = 0;
	mode = SSTV_MODE_NONE;
	lines_done = 0;
	lines_total = 0;
	radio_req_vfo(true);
	radio_req_vfo_set(hz);
	radio_req_mode(RADIO_MODE_FM);
	ui_dirty();
}

static void sstv_leave(void)
{
	sstv_stop();
	starting = false;
}

const ui_scr_def_t ui_sstv_screen = {
	.name = "sstv", .flags = SF_FIELD, .hint = "Back stop", .key = sstv_key, .draw = sstv_draw,
	.enter = sstv_enter, .leave = sstv_leave, .tick = sstv_tick,
};
#endif
