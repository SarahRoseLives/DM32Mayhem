// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Bit streams for the OOK TX app (core/ook): a typed hex code, or a de Bruijn sequence of order N encoded as the common
// PWM remotes expect (0 = 1000, 1 = 1110). Bits are MSB first in each byte, the order board.ook sends them.
#ifndef OOK_H
#define OOK_H

#include <stdint.h>
#include "config.h"

#if CONFIG_OOK_TX
// Parse a hex string into bits: each hex digit is four bits, MSB first; other characters are ignored. Returns the bit
// count, or 0 when there were no digits or they did not fit cap_bits. out must hold cap_bits/8 bytes
uint16_t ook_hex(const char *s, uint8_t *out, uint16_t cap_bits);
// The binary de Bruijn sequence B(2, order) (each possible order-bit code once, order 1..10), each symbol encoded to
// four bits (0 = 1000, 1 = 1110). Returns the bit count, or 0 when the order is out of range or does not fit cap_bits
uint16_t ook_debruijn(uint8_t order, uint8_t *out, uint16_t cap_bits);
#endif

#endif
