/*
 * Broiler Synchronous MMIO
 *
 * (C) 2022.08.01 BuddyZhang1 <buddy.zhang@aliyun.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "broiler/broiler.h"
#include "broiler/ioport.h"
#include "broiler/utils.h"
#include "broiler/types.h"
#include <assert.h>

#define BISCUITOS_MMIO_BASE	0xD0000000
#define BISCUITOS_MMIO_LEN	0x10
#define SLOT_NUM_REG		0x00
#define SLOT_SEL_REG		0x04
#define MIN_FREQ_REG		0x08
#define MAX_FREQ_REG		0x0C

/* Emulate default register */
static u32 slot_num = 0x20;
static u32 slot_sel = 0x00;
static u32 freq_min = 0x10;
static u32 freq_max = 0x40;

/* Emulate MMIO */
static void Broiler_mmio_callback(struct broiler_cpu *vcpu,
		u64 addr, u8 *data, u32 len, u8 is_write, void *ptr)
{
	u64 offset = addr - BISCUITOS_MMIO_BASE;
	assert(len == 4);
	assert(offset <= BISCUITOS_MMIO_LEN);

	if (is_write) { /* OUT Instruction */
		switch (offset) {
		case SLOT_NUM_REG: /* Set Slot NUM */
			slot_num = ioport_read32((void *)data);
			break;
		case SLOT_SEL_REG:
			slot_sel = ioport_read32((void *)data);
			break;
		default:
			printf("%#llx Read-Only!\n", addr);
			break;
		}
	} else { /* In Instruction */
		switch (offset) {
		case SLOT_NUM_REG: /* Slot Number */
			ioport_write32((void *)data, slot_num);
			break;
		case SLOT_SEL_REG: /* Slot Select */
			ioport_write32((void *)data, slot_sel);
			break;
		case MIN_FREQ_REG: /* Frequency Min */
			ioport_write32((void *)data, freq_min);
			break;
		case MAX_FREQ_REG: /* Frequency Max */
			ioport_write32((void *)data, freq_max);
			break;
		}
	}
}

static int Broiler_pio_init(struct broiler *broiler)
{
	int r;

	r = broiler_ioport_register(broiler, BISCUITOS_MMIO_BASE,
			BISCUITOS_MMIO_LEN, Broiler_mmio_callback, NULL,
			DEVICE_BUS_MMIO);
	if (r < 0)
		return r;

	return 0;
}
dev_init(Broiler_pio_init);

static int Broiler_pio_exit(struct broiler *broiler)
{
	broiler_ioport_deregister(broiler, BISCUITOS_MMIO_BASE,
				DEVICE_BUS_MMIO);

	return 0;
}
dev_exit(Broiler_pio_exit);
