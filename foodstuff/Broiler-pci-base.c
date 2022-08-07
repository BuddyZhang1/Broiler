/*
 * Broiler PCI
 *
 * (C) 2022.08.01 BuddyZhang1 <buddy.zhang@aliyun.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "broiler/broiler.h"
#include "broiler/utils.h"
#include "broiler/pci.h"
#include "broiler/device.h"
#include "broiler/ioport.h"

/* BAR0 and BAR1 bitmap */
#define SLOT_NUM_REG		0x00
#define SLOT_SEL_REG		0x04
#define MIN_FREQ_REG		0x08
#define MAX_FREQ_REG		0x0C

static struct pci_device BiscuitOS_pci_device;
static struct device BiscuitOS_device = {
	.bus_type	= DEVICE_BUS_PCI,
	.data		= &BiscuitOS_pci_device,
};

/* Default value */
static u32 BiscuitOS_slot_num = 0x20;
static u32 BiscuitOS_slot_sel = 0x00;
static u32 BiscuitOS_freq_min = 0x10;
static u32 BiscuitOS_freq_max = 0x40;

static void BiscuitOS_pci_bar_callback(struct broiler_cpu *vcpu,
		u64 addr, u8 *data, u32 len, u8 is_write, void *ptr)
{
	struct pci_device *pdev = (struct pci_device *)ptr;
	u64 offset;
	u32 val;

	if (addr > 0x100000)
		offset = addr - pci_bar_address(pdev, 1);
	else
		offset = addr - pci_bar_address(pdev, 0);

	if (is_write) { /* IO Write */
		switch (offset) {
		case SLOT_NUM_REG:
			BiscuitOS_slot_num = ioport_read32((void *)data);
			break;
		case SLOT_SEL_REG:
			BiscuitOS_slot_sel = ioport_read32((void *)data);
			break;
		default:
			printf("PORT %#llx Unsupport Write OPS!\n", offset);
			break;
		}
	} else { /* IO Read */
		switch (offset) {
		case SLOT_NUM_REG:
			ioport_write32((void *)data, BiscuitOS_slot_num);
			break;
		case SLOT_SEL_REG:
			ioport_write32((void *)data, BiscuitOS_slot_sel);
			break;
		case MIN_FREQ_REG:
			ioport_write32((void *)data, BiscuitOS_freq_min);
			break;
		case MAX_FREQ_REG:
			ioport_write32((void *)data, BiscuitOS_freq_max);
			break;
		}
	}
}

static int BiscuitOS_pci_bar_active(struct broiler *broiler,
			struct pci_device *pdev, int bar, void *data)
{
	u32 bar_addr, bar_size;
	int r = -EINVAL;

	bar_addr = pci_bar_address(pdev, bar);
	bar_size = pci_bar_size(pdev, bar);

	switch (bar) {
	case 0:
		r = broiler_register_pio(broiler, bar_addr, bar_size,
				BiscuitOS_pci_bar_callback, data);
		break;
	case 1:
		r = broiler_ioport_register(broiler, bar_addr, bar_size,
				BiscuitOS_pci_bar_callback, data,
				DEVICE_BUS_MMIO);
		break;
	}

	return r;
}

static int BiscuitOS_pci_bar_deactive(struct broiler *broiler,
			struct pci_device *pdev, int bar, void *data)
{
	int r = -EINVAL;
	u32 bar_addr;

	bar_addr = pci_bar_address(pdev, bar);

	switch (bar) {
	case 0:
		r = broiler_deregister_pio(broiler, bar_addr);
		break;
	case 1:
		r = broiler_ioport_deregister(broiler,
					bar_addr, DEVICE_BUS_MMIO);
		break;
	}

	return r;
}

static int BiscuitOS_pci_init(struct broiler *broiler)
{
	struct pci_device *pdev = &BiscuitOS_pci_device;
	u32 mmio_addr;
	u16 io_addr;
	int r;

	/* IO-BAR */
	io_addr = pci_alloc_io_port_block(PCI_IO_SIZE);
	/* MM-BAR */
	mmio_addr = pci_alloc_mmio_block(PCI_IO_SIZE);

	/* PCI Configuration Space */
	BiscuitOS_pci_device = (struct pci_device) {
		.vendor_id	= 0x1016,
		.device_id	= 0x1413,
		.command	= PCI_COMMAND_IO | PCI_COMMAND_MEMORY,
		.header_type	= PCI_HEADER_TYPE_NORMAL,
		.revision_id	= 0,

		.bar[0]		= io_addr | PCI_BASE_ADDRESS_SPACE_IO,
		.bar[1]		= mmio_addr | PCI_BASE_ADDRESS_SPACE_MEMORY,
		.bar_size[0]	= PCI_IO_SIZE,
		.bar_size[1]	= PCI_IO_SIZE,

		.status		= PCI_STATUS_CAP_LIST,
	};

	r = pci_register_bar_regions(broiler, &BiscuitOS_pci_device,
				BiscuitOS_pci_bar_active,
				BiscuitOS_pci_bar_deactive,
				(void *)&BiscuitOS_pci_device);
	if (r < 0)
		return r;

	r = device_register(&BiscuitOS_device);
	if (r < 0)
		return r;

	return 0;
}
dev_init(BiscuitOS_pci_init);

static int BiscuitOS_pci_exit(struct broiler *broiler)
{
	device_unregister(&BiscuitOS_device);
	
	return 0;
}
dev_exit(BiscuitOS_pci_exit);
