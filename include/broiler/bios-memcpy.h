// SPDX-License-Identifier: GPL-2.0-only
#ifndef _BROILER_BIOS_MEMCPY_H
#define _BROILER_BIOS_MEMCPY_H

#include "broiler/types.h"
#include <stddef.h>

void memcpy16(u16 dst_seg, void *dst, u16 src_seg, const void *src, size_t len);

#endif
