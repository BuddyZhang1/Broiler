#include "broiler/broiler.h"
#include "broiler/utils.h"
#include "broiler/pci.h"
#include "broiler/device.h"
#include "broiler/ioport.h"

static struct pci_device BiscuitOS_pci_device;
static struct device BiscuitOS_device = {
	.bus_type	= DEVICE_BUS_PCI,
	.data		= &BiscuitOS_pci_device,
};
static u32 BiscuitOS_signatures = 0xBD;
static u32 BiscuitOS_PCI_VERSION = 0x10;
static u32 BiscuitOS_TODO = 0x28;
static u32 BiscuitOS_MODE = 0x89;

static void BiscuitOS_pci_io_callback(struct broiler_cpu *vcpu,
		u64 addr, u8 *data, u32 len, u8 is_write, void *ptr)
{
	u32 val;

	switch (addr) {
	case 0x00: /* R/W Signatures */
		if (!is_write)
			ioport_write32((void *)data, BiscuitOS_signatures);
		else
			BiscuitOS_signatures = ioport_read32((void *)data);
		break;
	case 0x04: /* RO PCI Version */
		ioport_write32((void *)data, BiscuitOS_PCI_VERSION);
		break;
	default:
		break;
	}
}

static void BiscuitOS_pci_mmio_callback(struct broiler_cpu *vcpu,
		u64 addr, u8 *data, u32 len, u8 is_write, void *ptr)
{
	u32 val;

	switch (addr) {
	case 0x00: /* R/W TODO */
		if (!is_write)
			ioport_write32((void *)data, BiscuitOS_TODO);
		else
			BiscuitOS_TODO = ioport_read32((void *)data);
		break;
	case 0x04: /* R/W MODE */
		if (!is_write)
			ioport_write32((void *)data, BiscuitOS_MODE);
		else
			BiscuitOS_MODE = ioport_read32((void *)data);
		break;
	default:
		break;
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
				BiscuitOS_pci_io_callback, data);
		break;
	case 1:
		r = broiler_ioport_register(broiler, bar_addr, bar_size,
				BiscuitOS_pci_mmio_callback, data,
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
	};

	r = pci_register_bar_regions(broiler, &BiscuitOS_pci_device,
				BiscuitOS_pci_bar_active,
				BiscuitOS_pci_bar_deactive, NULL);
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
	return 0;
}
dev_exit(BiscuitOS_pci_exit);
