#include "broiler/broiler.h"
#include "broiler/ioport.h"
#include "broiler/pci.h"
#include "broiler/err.h"
#include "broiler/device.h"

static u32 pci_config_address_bits;

static bool pci_device_exists(u8 bus, u8 device, u8 function)
{
	union pci_config_address pci_config_address;

	pci_config_address.w = ioport_read32(&pci_config_address_bits);

	if (pci_config_address.bus_number != bus)
		return false;
	if (pci_config_address.function_number != function)
		return false;

	return !IS_ERR_OR_NULL(device_search(DEVICE_BUS_PCI, device));
}

static void pci_config_wr(struct broiler *broiler,
		union pci_config_address addr, void *data, int size)
{
	struct pci_device *pdev;
	u8 dev_num = addr.device_number;
	u8 offset, bar;
	u32 value = 0;
	void *base;

	if (!pci_device_exists(addr.bus_number, dev_num, 0))
		return;

	offset = addr.w & PCI_DEV_CFG_MASK;
	base = pdev = device_search(DEVICE_BUS_PCI, dev_num)->data;

	if (pdev->cfg_ops.write)
		pdev->cfg_ops.write(broiler, pdev, offset, data, size);

	/*
	 * legacy hack: ignore writes to uninitialized regions (e.g. ROM BAR)
	 * Not very nice but has been working so far.
	 */
	if (*(u32 *)(base + offset) == 0)
		return;

	if (offset == PCI_COMMAND) {
		memcpy(&value, data, size);
		pci_config_command_wr(broiler, pdev, (u16)value);
		return;
	}

}

static void pci_config_data_mmio(struct kvm_cpu *vcpu, u64 addr,
			u8 *data, u32 len, u8 is_write, void *ptr)
{
	union pci_config_address pci_config_address;

	if (len > 4)
		len  = 4;

	pci_config_address.w = ioport_read32(&pci_config_address_bits);
	/*
	 * If someone accesses PCI configuration space offsets that are
	 * not aligned to 4 bytes, it uses ioport to signify that.
	 */
	pci_config_address.reg_offset = addr - PCI_CONFIG_DATA;

	if (is_write)
		pci_config_wr(vcpu->broiler, pci_config_address, data, len);
}

int broiler_pci_init(struct broiler *broiler)
{
	broiler_register_pio(broiler, PCI_CONFIG_DATA, 4,
					pci_config_data_mmio, NULL);

	return 0;
}
