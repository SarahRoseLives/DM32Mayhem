// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// The data-flash backend of the BBS message store (bbs_flash.h). It claims the native 1 MB area with a header, then
// presents 512-byte records. Reads and writes go through the board's flash HAL; a write merges into its 4 KB sector.
// The header keeps the store off a flash that holds someone else's data.
#ifndef RFOUNDRY_BBS_FLASH_H
#define RFOUNDRY_BBS_FLASH_H

#include "bbs/bbs_store.h"

// Fill be with the message store's backend on the radio's data flash. False when there is no usable, writable flash
// or the region holds another firmware's data
bool bbs_flash_backend(bbs_store_backend_t *be);

#endif
