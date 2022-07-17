// SPDX-License-Identifier: GPL-2.0-only
#include <string.h>

#include "broiler/bios-interrupt.h"

void interrupt_table_setup(struct interrupt_table *table,
					struct bios_intr_desc *entry)
{
	unsigned int i;

	for (i = 0; i < REAL_INTR_VECTORS; i++)
		table->entries[i] = *entry;
}

void interrupt_table_set(struct interrupt_table *table,
		struct bios_intr_desc *entry, unsigned int num)
{
	if (num < REAL_INTR_VECTORS)
		table->entries[num] = *entry;
}

void interrupt_table_copy(struct interrupt_table *table,
				void *dst, unsigned int size)
{
	memcpy(dst, table->entries, sizeof(table->entries));
}
