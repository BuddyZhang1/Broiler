// SPDX-License-Identifier: GPL-2.0-only
#ifndef _BROILER_MEMORY_H
#define _BROILER_MEMORY_H

#include "broiler/broiler.h"
#include "linux/rbtree-interval.h"

enum memory_type {
	BROILER_MEM_TYPE_RAM      = 1 << 0,
	BROILER_MEM_TYPE_DEVICE   = 1 << 1,
	BROILER_MEM_TYPE_RESERVED = 1 << 2,
	BROILER_MEM_TYPE_READONLY = 1 << 3,

	BROILER_MEM_TYPE_ALL	    = BROILER_MEM_TYPE_RAM |
				      BROILER_MEM_TYPE_DEVICE |
				      BROILER_MEM_TYPE_RESERVED |
				      BROILER_MEM_TYPE_READONLY
};

struct broiler_memory_region {
	struct rb_int_node	node;
	u64			guest_phys_addr;
	void			*host_addr;
	u64			size;
	enum memory_type	type;
	u32			slot;
};

#define memory_node(n) rb_entry(n, struct broiler_memory_region, node)

extern int broiler_memory_init(struct broiler *broiler);
extern int broiler_memory_exit(struct broiler *broiler);
extern void *gpa_to_hva(struct broiler *broiler, u64 offset);

static inline void *gpa_flat_to_hva(struct broiler *broiler, u64 offset)
{
	return gpa_to_hva(broiler, offset);
}

static inline void *gpa_real_to_hva(struct broiler *broiler, u16 selector,
                                        u16 offset)
{
	unsigned long flat = ((u32)selector << 4) + offset;

	return gpa_to_hva(broiler, flat);
} 

#endif
