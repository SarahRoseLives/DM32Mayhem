// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// RTTY (Baudot/ITA2) transmit encoding for the RTTY TX app: LTRS/FIGS shifts, 5 data bits LSB first, one start and two
// stop bits, packed LSB first for the AFSK modulator. The code table is the public ITA2 standard.
#ifndef RTTY_H
#define RTTY_H

#include <stdint.h>
#include "config.h"

#if CONFIG_RTTY_TX
// Encode text into the on-air bit stream (LSB first in each byte). Uppercase letters use LTRS, digits and punctuation
// FIGS; anything unsupported becomes a space. Returns the bit count, or 0 when it does not fit cap_bits
uint16_t rtty_encode(const char *text, uint8_t *out, uint16_t cap_bits);
#endif

#endif
