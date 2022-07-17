// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "broiler/broiler.h"
#include "broiler/bios.h"
#include "broiler/bios-export.h"
#include "broiler/e820.h"
#include "broiler/kvm.h"
#include "broiler/memory.h"
#include "broiler/bios-rom.h"
#include "broiler/bios-interrupt.h"

#define BIOS_IRQ_PA_ADDR(name)	(BIOS_BEGIN + BIOS_OFFSET__##name)
#define BIOS_IRQ_FUNC(name)     ((char *)&bios_rom[BIOS_OFFSET__##name])
#define BIOS_IRQ_SIZE(name)     (BIOS_ENTRY_SIZE(BIOS_OFFSET__##name))

#define DEFINE_BIOS_IRQ_HANDLER(_irq, _handler)                 \
	{                                                       \
		.irq            = _irq,                         \
		.address        = BIOS_IRQ_PA_ADDR(_handler),   \
		.handler        = BIOS_IRQ_FUNC(_handler),      \
		.size           = BIOS_IRQ_SIZE(_handler),      \
}

static struct irq_handler bios_irq_handlers[] = {
	DEFINE_BIOS_IRQ_HANDLER(0x10, bios_int10),
	DEFINE_BIOS_IRQ_HANDLER(0x15, bios_int15),
};

static void setup_irq_handler(struct broiler *broiler,
					struct irq_handler *handler) 
{
	struct bios_intr_desc intr;
	void *p;

	p = gpa_flat_to_hva(broiler, handler->address);
	memcpy(p, handler->handler, handler->size);

	intr = (struct bios_intr_desc) {
		.segment	= REAL_SEGMENT(BIOS_BEGIN),
		.offset		= handler->address - BIOS_BEGIN,
	};

	interrupt_table_set(&broiler->interrupt_table, &intr, handler->irq);
}

/*
 * e820_setup - setup some simple E820 memory map
 */
static void e820_setup(struct broiler *broiler)
{
	struct e820_entry *entry;
	struct e820_table *table;
	unsigned int i = 0;

	table = gpa_flat_to_hva(broiler, E820_MAP_START);
	entry = table->map;

	entry[i++] = (struct e820_entry) {
		.addr		= REAL_MODE_IVT_BEGIN,
		.size		= EBDA_START - REAL_MODE_IVT_BEGIN,
		.type		= E820_RAM,
	};

	entry[i++] = (struct e820_entry) {
		.addr		= EBDA_START,
		.size		= VGA_RAM_BEGIN - EBDA_START,
		.type		= E820_RESERVED,
	};

	entry[i++] = (struct e820_entry) {
		.addr		= BIOS_BEGIN,
		.size		= BIOS_END - BIOS_BEGIN,
		.type		= E820_RESERVED,
	};

	if (broiler->ram_size < BROILER_32BIT_GAP_START) {
		entry[i++] = (struct e820_entry) {
			.addr		= BZ_KERNEL_START,
			.size		= broiler->ram_size - BZ_KERNEL_START,
			.type		= E820_RAM,
		};
	} else {
		entry[i++] = (struct e820_entry) {
			.addr		= BZ_KERNEL_START,
			.size		= BROILER_32BIT_GAP_START - 
						BZ_KERNEL_START,
			.type		= E820_RAM,
		};
		entry[i++] = (struct e820_entry) {
			.addr		= BROILER_32BIT_MAX_MEM_SIZE,
			.size		= broiler->ram_size -
						BROILER_32BIT_MAX_MEM_SIZE,
			.type		= E820_RAM,
		};
	}

	table->nr_map = i;
}

static void setup_vga_rom(struct broiler *broiler)
{
	u16 *mode;
	void *p;

	p = gpa_flat_to_hva(broiler, VGA_ROM_OEM_STRING);
	memset(p, 0, VGA_ROM_OEM_STRING_SIZE);
	strncpy(p, "Broiler VESA", VGA_ROM_OEM_STRING_SIZE);

	mode = gpa_flat_to_hva(broiler, VGA_ROM_MODES);
	mode[0] = 0x0112;
	mode[1] = 0xffff;
}

int broiler_setup_bios(struct broiler *broiler)
{
	struct bios_intr_desc intr;
	unsigned long address;
	void *p;
	int i;

	/* BIOS BDA */
	p = gpa_flat_to_hva(broiler, BDA_START);
	memset(p, 0, BDA_END - BDA_START);

	/* BIOS EBDA */
	p = gpa_flat_to_hva(broiler, EBDA_START);
	memset(p, 0, EBDA_END - EBDA_START);

	/* BIOS */
	p = gpa_flat_to_hva(broiler, BIOS_BEGIN);
	memset(p, 0, BIOS_END - BIOS_BEGIN);

	/* VGA */
	p = gpa_flat_to_hva(broiler, VGA_ROM_BEGIN);
	memset(p, 0, VGA_ROM_END - VGA_ROM_BEGIN);

	/* Copy BIOS ROM into the place */
	p = gpa_flat_to_hva(broiler, BIOS_BEGIN);
	memcpy(p, bios_rom, bios_rom_size);

	/* E820 Table */
	e820_setup(broiler);

	/* VGA */
	setup_vga_rom(broiler);

	/* FAKE IVT */
	address = BIOS_IRQ_PA_ADDR(bios_intfake);
	intr = (struct bios_intr_desc) {
		.segment	= REAL_SEGMENT(BIOS_BEGIN),
		.offset		= address - BIOS_BEGIN,
	};
	interrupt_table_setup(&broiler->interrupt_table, &intr);

	for (i = 0; i < ARRAY_SIZE(bios_irq_handlers); i++)
		setup_irq_handler(broiler, &bios_irq_handlers[i]);

	/* almost done */
	p = gpa_flat_to_hva(broiler, 0);
	interrupt_table_copy(&broiler->interrupt_table, p, REAL_INTR_SIZE);

	return 0;
}
