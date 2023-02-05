/*
 * Broiler MMIO Base
 *
 * (C) 2023.02.05 BuddyZhang1 <buddy.zhang@aliyun.com>
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

#define BISCUITOS_MMIO_BASE	0xF0000000
#define BISCUITOS_MMIO_LEN	0x1000

/* BAR Space */
static char BAR[BISCUITOS_MMIO_LEN];

/* Emulate MMIO */
static void Broiler_mmio_callback(struct broiler_cpu *vcpu,
		u64 addr, u8 *data, u32 len, u8 is_write, void *ptr)
{
	u64 offset = addr - BISCUITOS_MMIO_BASE;
	assert(offset <= BISCUITOS_MMIO_LEN);

	if (is_write) { /* OUT Instruction */
		memcpy((void *)&BAR[offset], (void *)data, len);
	} else { /* In Instruction */
		memcpy((void *)data, (void *)&BAR[offset], len);
	}
}

static int Broiler_mmio_init(struct broiler *broiler)
{
	int r;

	r = broiler_ioport_register(broiler, BISCUITOS_MMIO_BASE,
			BISCUITOS_MMIO_LEN, Broiler_mmio_callback, NULL,
			DEVICE_BUS_MMIO);
	if (r < 0)
		return r;

	return 0;
}
dev_init(Broiler_mmio_init);

static int Broiler_mmio_exit(struct broiler *broiler)
{
	broiler_ioport_deregister(broiler, BISCUITOS_MMIO_BASE,
				DEVICE_BUS_MMIO);

	return 0;
}
dev_exit(Broiler_mmio_exit);
