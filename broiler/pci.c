#include "broiler/broiler.h"
#include "broiler/ioport.h"
#include "broiler/pci.h"
#include "broiler/err.h"
#include "broiler/device.h"

static u32 pci_config_address_bits;
static u8 next_line = KVM_IRQ_OFFSET;

/*
 * This is within our PCI gap - in an unused area.
 * Note this is a PCI *bus address*, is used to assign BARs etc.!
 * (That's why it can still 32bit even with 64bit guests -- 64bit
 * PCI isn't currently supported.)
 */
static u32 pci_mmio_blocks 	= BROILER_PCI_MMIO_AREA;
static u32 pci_io_port_blocks	= PCI_IOPORT_START;

int irq_alloc_line(void)
{
	return next_line++;
}

u16 pci_alloc_io_port_block(u32 size)
{
	u16 port = ALIGN(pci_io_port_blocks, PCI_IO_SIZE);

	pci_io_port_blocks = port + size;
	return port;
}

/* BARs must be naturally aligned, so enforce this in the allocator */
u32 pci_alloc_mmio_block(u32 size)
{
	u32 block = ALIGN(pci_mmio_blocks, size);

	pci_mmio_blocks = block + size;
	return block;
}

int pci_assign_irq(struct pci_device *pdev)
{
	/*
	 * PCI supports only INTA#,B#,C#,D# per device.
	 *
	 * A#, B#, C#, D# are allowed for multifunctional devices so stick
	 * with A# for our singal function devices.
	 */
	pdev->irq_pin		= 1;
	pdev->irq_line		= irq_alloc_line();

	if (!pdev->irq_type)
		pdev->irq_type = IRQ_TYPE_LEVEL_HIGH;

	return pdev->irq_line;
}

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

static bool pci_bar_is_implemented(struct pci_device *pdev, int bar)
{
	return pci_bar_size(pdev, bar);
}

static bool pci_bar_is_active(struct pci_device *pdev, int bar)
{
	return pdev->bar_active[bar];
}

static int pci_activate_bar(struct broiler *broiler,
					struct pci_device *pdev, int bar)
{
	int r = 0;

	if (pci_bar_is_active(pdev, bar))	
		goto out;

	r = pdev->bar_activate_fn(broiler, pdev, bar, pdev->data);
	if (r < 0) {
		printf("Error activating emulation for BAR %d\n", bar);
		goto out;
	}
	pdev->bar_active[bar] = true;

out:
	return r;
}

static int pci_deactivate_bar(struct broiler *broiler,
				struct pci_device *pdev, int bar)
{
	int r = 0;

	if (!pci_bar_is_active(pdev, bar))
		goto out;

	r = pdev->bar_deactivate_fn(broiler, pdev, bar, pdev->data);
	if (r < 0) {
		printf("Error deactivating emulation for BAR %d\n", bar);
		goto out;
	}
	pdev->bar_active[bar] = true;

out:
	return r;
}

static void pci_config_command_wr(struct broiler *broiler,
			struct pci_device *pdev, u16 command)
{
	bool io, mem;
	int i;

	io = (pdev->command ^ command) & PCI_COMMAND_IO;
	mem = (pdev->command ^ command) & PCI_COMMAND_MEMORY;

	for (i = 0; i < 6; i++) {
		if (!pci_bar_is_implemented(pdev, i))
			continue;

		if (io && pci_bar_is_io(pdev, i)) {
			if (pci_io_space_enabled(command))
				pci_activate_bar(broiler, pdev, i);
			else
				pci_deactivate_bar(broiler, pdev, i);
		}

		if (mem && pci_bar_is_memory(pdev, i)) {
			if (pci_memory_space_enabled(command))
				pci_activate_bar(broiler, pdev, i);
			else
				pci_deactivate_bar(broiler, pdev, i);
		}
	}
	pdev->command = command;
}

static int pci_trigger_bar_regions(bool activate,
			struct broiler *broiler, u32 start, u32 size)
{
	u32 pci_start, pci_size;
	struct pci_device *pdev;
	struct device *dev;
	int i, r;

	dev = device_first_dev(DEVICE_BUS_PCI);
	while (dev) {
		pdev = dev->data;
		for (i = 0; i < 6; i ++) {
			if (!pci_bar_is_implemented(pdev, i))
				continue;

			pci_start = pci_bar_address(pdev, i);
			pci_size  = pci_bar_size(pdev, i);
			if (pci_start >= start + size)
				continue;

			if (activate)
				r = pci_activate_bar(broiler, pdev, i);
			else
				r = pci_deactivate_bar(broiler, pdev, i);
			if (r < 0)
				return r;
		}
		dev = device_next_dev(dev);
	}
	return 0;
}

static inline int
pci_activate_bar_regions(struct broiler *broiler, u32 start, u32 size)
{
	return pci_trigger_bar_regions(true, broiler, start, size);
}

static inline int 
pci_deactivate_bar_regions(struct broiler *broiler, u32 start, u32 size)
{
	return pci_trigger_bar_regions(false, broiler, start, size);
}

static void pci_config_bar_wr(struct broiler *broiler,
		struct pci_device *pdev, int bar, u32 value)
{
	u32 old_addr, new_addr, bar_size;
	u32 mask;
	int r;

	if (pci_bar_is_io(pdev, bar))
		mask = (u32)PCI_BASE_ADDRESS_IO_MASK;
	else
		mask = (u32)PCI_BASE_ADDRESS_MEM_MASK;

	/*
	 * If the kernel mask the BAR, it will expect to find the size of the
	 * BAR, there next time it reads from it. After the kernel reads the
	 * size, it will write the address back.
	 *
	 * According to the PCI local bus specification REV 3.0: The number of
	 * upper bits that a device actually implements depends on how much of
	 * the address space the device will respond to. A device that wants a
	 * 1MB memory address space (using a 32-bit base address register)
	 * would build the top 12 bits of the address register, hardwiring
	 * the other bits to 0.
	 *
	 * Furthermore, software can determine how much address space the device
	 * requires by writing a value of all 1's to the register and then
	 * reading the value back. The device will return 0's in all don't-care
	 * address bits, effectively specifying the address space required.
	 *
	 * Software computes the size of the address space with the formula
	 * S = ~B + 1, where S is the memory size and B is the value read from
	 * the BAR. This means that the BAR value that Broiler should return
	 * is B = ~(S - 1).
	 */
	if (value == 0xffffffff) {
		value = ~(pci_bar_size(pdev, bar) - 1);
		/* Preserve the special bits */
		value = (value & mask) | (pdev->bar[bar] & ~mask);
		pdev->bar[bar] = value;
		return;
	}

	value = (value & mask) | (pdev->bar[bar] & ~mask);

	/* Don't emulation when region type access is disabled. */
	if (pci_bar_is_io(pdev, bar) &&
			!pci_io_space_enabled(pdev->command)) {
		pdev->bar[bar] = value;
		return;
	}

	if (pci_bar_is_memory(pdev, bar) &&
			!pci_memory_space_enabled(pdev->command)) {
		pdev->bar[bar] = value;
		return;
	}

	/*
	 * BAR reassignment can be while device access is enabled and
	 * memory regions for different devices can overlap as long as no
	 * access is made to the overlapping memory regions. To implement
	 * BAR reasignment, we deactivate emulation for the region described
	 * by the BAR value that the guest is changing, we disable emulation
	 * for the regions that overlap with the new one (by scanning through
	 * all PCI devices), we enable emulation for the new BAR value and
	 * finally we enable emulation for all device regions that were
	 * overlapping with the old value.
	 */
	old_addr = pci_bar_address(pdev, bar);
	new_addr = pci_bar_address_value(value);
	bar_size = pci_bar_size(pdev, bar);

	r = pci_deactivate_bar(broiler, pdev, bar);
	if (r < 0)
		return;

	r = pci_deactivate_bar_regions(broiler, new_addr, bar_size);
	if (r < 0) {
		/*
		 * We cannot update the BAR because of an overlapping region
		 * that failed to deactivate emulation, so keep the old BAR
		 * value and re-activate emulation for it.
		 */
		pci_activate_bar(broiler, pdev, bar);
		return;
	}

	pdev->bar[bar] = value;
	r = pci_activate_bar(broiler, pdev, bar);
	if (r < 0) {
		/*
		 * New region cannot be emulated, re-enable the regions that
		 * were overlapping.
		 */
		pci_activate_bar_regions(broiler, new_addr, bar_size);
		return;
	}

	pci_activate_bar_regions(broiler, old_addr, bar_size);
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

	bar = (offset - PCI_BAR_OFFSET(0)) / sizeof(u32);
	if (bar < 6) {
		memcpy(&value, data, size);
		pci_config_bar_wr(broiler, pdev, bar, value);
		return;
	}

	memcpy(base + offset, data, size);
}

void pci_config_rd(struct broiler *broiler,
		union pci_config_address addr, void *data, int size)
{
	u8 dev_num = addr.device_number;
	struct pci_device *pdev;
	u16 offset;

	if (pci_device_exists(addr.bus_number, dev_num, 0)) {
		pdev = device_find_dev(DEVICE_BUS_PCI, dev_num)->data;
		offset = addr.w & PCI_DEV_CFG_MASK;

		if (pdev->cfg_ops.read)
			pdev->cfg_ops.read(broiler, pdev, offset, data, size);

		memcpy(data, (void *)pdev + offset, size);
	} else
		memset(data, 0xff, size);
}

static void pci_config_data_mmio(struct broiler_cpu *vcpu, u64 addr,
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
	else
		pci_config_rd(vcpu->broiler, pci_config_address, data, len);
}

static void *pci_config_address_ptr(u16 port)
{
	unsigned long offset;
	void *base;

	offset = port - PCI_CONFIG_ADDRESS;
	base   = &pci_config_address_bits;

	return base + offset;
}

static void pci_config_address_mmio(struct broiler_cpu *vcpu, u64 addr,
			u8 *data, u32 len, u8 is_write, void *ptr)
{
	void *p = pci_config_address_ptr(addr);

	if (is_write)
		memcpy(p, data, len);
	else
		memcpy(data, p, len);
}

static void pci_config_mmio_access(struct broiler_cpu *vcpu, u64 addr,
			u8 *data, u32 len, u8 is_write, void *broiler)
{
	union pci_config_address cfg_addr;

	addr			-= BROILER_PCI_CFG_AREA;
	cfg_addr.w		= (u32)addr;
	cfg_addr.enable_bit	= 1;

	if (len  > 4)
		len = 4;

	if (is_write)
		pci_config_wr(broiler, cfg_addr, data, len);
	else
		pci_config_rd(broiler, cfg_addr, data, len);
}

int pci_register_bar_regions(struct broiler *broiler, struct pci_device *pdev,
		bar_activate_fn_t bar_activate_fn,
		bar_deactivate_fn_t bar_deactivate_fn, void *data)
{
	int i, r;

	pdev->bar_activate_fn = bar_activate_fn;
	pdev->bar_deactivate_fn = bar_deactivate_fn;
	pdev->data = data;

	for (i = 0; i < 6; i++) {
		if (!pci_bar_is_implemented(pdev, i))
			continue;
		if (pci_bar_is_active(pdev, i))
			continue;

		if (pci_bar_is_io(pdev, i) &&
			pci_io_space_enabled(pdev->command)) {
			r = pci_activate_bar(broiler, pdev, i);
			if (r < 0)
				return r;
		}

		if (pci_bar_is_memory(pdev, i) &&
			pci_memory_space_enabled(pdev->command)) {
			r = pci_activate_bar(broiler, pdev, i);
			if (r < 0)
				return r;
		}
	}
	return 0;
}

int broiler_pci_init(struct broiler *broiler)
{
	int r;

	r = broiler_register_pio(broiler, PCI_CONFIG_DATA, 4,
					pci_config_data_mmio, NULL);
	if (r < 0)
		return r;

	r = broiler_register_pio(broiler, PCI_CONFIG_ADDRESS, 4,
					pci_config_address_mmio, NULL);
	if (r < 0)
		goto err_data_register;

	r = broiler_register_mmio(broiler, BROILER_PCI_CFG_AREA,
			PCI_CFG_SIZE, false, pci_config_mmio_access, broiler);
	if (r < 0)
		goto err_mmio;

	return 0;

err_mmio:
	broiler_deregister_pio(broiler, PCI_CONFIG_ADDRESS);	
err_data_register:
	broiler_deregister_pio(broiler, PCI_CONFIG_DATA);
	return r;
}

int broiler_pci_exit(struct broiler *broiler)
{
	broiler_deregister_pio(broiler, PCI_CONFIG_DATA);
	broiler_deregister_pio(broiler, PCI_CONFIG_ADDRESS);

	return 0;
}
