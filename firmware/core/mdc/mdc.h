// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// MDC-1200 (Motorola Data Communications) transmit encoding for the MDC TX app: the preamble, sync word, 16-bit
// codewords with their CRC, convolutional coding and interleaving, NRZI, ready for the AFSK transmitter at 1200 baud
// (mark 1800 Hz, space 1200 Hz). The format is the public MDC-1200 signaling used for ANI / PTT ID.
#ifndef MDC_H
#define MDC_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

#if CONFIG_MDC_TX
// One codeword's 4 data bytes. Id is the unit (the alpha/ANI or the target), source the sender for the two-codeword
// forms (Call Alert, Sel Call 1/2).
typedef struct {
	uint8_t op;
	uint8_t arg;
	uint8_t id_hi, id_lo;
	uint8_t src_hi, src_lo;
} mdc_cfg_t;

// Build the burst's on-air bits into out (packed a byte at a time, each byte reversed so afsk_mod, which reads LSB
// first, sends the MDC bits MSB first): 3 preamble blocks of 7 bytes 0xAA, the 5-byte sync, then one or two codewords.
// Returns the bit count, or 0 when it does not fit cap_bits. Call Alert and Sel Call 1/2 send two codewords (the data
// opcode, then the requested op with the source ID).
uint16_t mdc_build(const mdc_cfg_t *c, uint8_t *out, uint16_t cap_bits);

// True for the ops that carry a second codeword (Call Alert, Sel Call 1 and 2)
bool mdc_op_is_double(uint8_t op);
#endif

#endif
