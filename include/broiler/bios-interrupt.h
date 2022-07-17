// SPDX-License-Identifier: GPL-2.0-only
#ifndef _BROILER_BIOS_INTERRUPT_H
#define _BROILER_BIOS_INTERRUPT_H

#include "broiler/bios.h"
#include "broiler/types.h"

struct bios_intr_desc {
	u16 offset;
	u16 segment;
} __attribute__((packed));

#define REAL_SEGMENT_SHIFT	4
#define REAL_SEGMENT(addr)	((addr) >> REAL_SEGMENT_SHIFT)
#define REAL_INTR_SIZE		(REAL_INTR_VECTORS * sizeof(struct bios_intr_desc))

struct interrupt_table {
	struct bios_intr_desc entries[REAL_INTR_VECTORS];
};

struct irq_handler {
	unsigned long           address;
	unsigned int            irq;
	void                    *handler;
	size_t                  size;
};

extern void interrupt_table_setup(struct interrupt_table *table,
                                        struct bios_intr_desc *intr);
extern void interrupt_table_set(struct interrupt_table *table,
                struct bios_intr_desc *entry, unsigned int num);
extern void interrupt_table_copy(struct interrupt_table *table,
                                void *dst, unsigned int size);
#endif
