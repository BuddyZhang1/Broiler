// SPDX-License-Identifier: GPL-2.0-only
#ifndef _BROILER_E820_H
#define _BROILER_E820_H

#include "broiler/types.h"

#define SMAP	0x534d4150      /* ASCII "SMAP" */
                
#define E820MAX	128             /* number of entries in E820MAP */
#define E820_X_MAX	E820MAX

#define E820_RAM	1
#define E820_RESERVED	2

struct e820_entry {
	u64 addr;     /* start of memory segment */
	u64 size;     /* size of memory segment */
	u32 type;     /* type of memory segment */ 
} __attribute__((packed));

struct e820_table {
	u32 nr_map;
	struct e820_entry map[E820_X_MAX];
};

#endif
