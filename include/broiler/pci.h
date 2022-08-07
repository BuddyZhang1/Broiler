// SPDX-License-Identifier: GPL-2.0-only
#ifndef _BROILER_PCI_H
#define _BROILER_PCI_H

#include <linux/pci_regs.h>
#include "broiler/irq.h"
#include "broiler/msi.h"

#define PCI_VENDOR_ID_REDHAT_QUMRANET		0x1af4
#define PCI_VENDOR_ID_PCI_SHMEM			0x0001
#define PCI_SUBSYSTEM_VENDOR_ID_REDHAT_QUMRANET	0x1af4

/*
 * PCI Configuration Mechanism #1 I/O ports. See Section 3.7.4.1.
 * ("Configuration Mechanism #1") of the PCI Local Bus Specification 2.1 for
 * details.
 */     
#define PCI_CONFIG_ADDRESS	0xcf8
#define PCI_CONFIG_DATA		0xcfc
#define PCI_CONFIG_BUS_FORWARD	0xcfa
#define PCI_IO_SIZE		0x100
#define PCI_MEM_SIZE		0x1000000
#define PCI_IOPORT_START	0x6200
#define PCI_CFG_SIZE		(1ULL << 24)

struct pci_device;

typedef int (*bar_activate_fn_t)(struct broiler *broiler,
				struct pci_device *pci_hdr,
				int bar_num, void *data);
typedef int (*bar_deactivate_fn_t)(struct broiler *broiler,
				struct pci_device *pci_hdr,
				int bar_num, void *data);

#define PCI_BAR_OFFSET(b)	(offsetof(struct pci_device, bar[b]))
#define PCI_DEV_CFG_SIZE	256
#define PCI_DEV_CFG_MASK	(PCI_DEV_CFG_SIZE - 1)

struct pci_config_operations {
	void (*write)(struct broiler *broiler, struct pci_device *pdev,
				u8 offset, void *data, int sz);
	void (*read)(struct broiler *broiler, struct pci_device *pdev,
				u8 offset, void *data, int sz);
};

union pci_config_address {
        struct {
                unsigned        reg_offset      : 2;            /* 1  .. 0  */
                unsigned        register_number : 6;            /* 7  .. 2  */
                unsigned        function_number : 3;            /* 10 .. 8  */
                unsigned        device_number   : 5;            /* 15 .. 11 */
                unsigned        bus_number      : 8;            /* 23 .. 16 */
                unsigned        reserved        : 7;            /* 30 .. 24 */
                unsigned        enable_bit      : 1;            /* 31       */
	};
	u32 w;
};

struct msix_table {
	struct msi_msg msg;
	u32 ctrl;
};

struct msix_cap {
	u8 cap;
	u8 next;
	u16 ctrl;
	u32 table_offset;
	u32 pba_offset;
};

struct pci_device {
	/* Configuration space, as seen by the guest */
	union {
		struct {
			u16	vendor_id;
			u16	device_id;
			u16	command;
			u16	status;
			u8	revision_id;
			u8	class[3];
			u8	cacheline_size;
			u8	latency_timer;
			u8	header_type;
			u8	bist;
			u32	bar[6];
			u32	card_bus;
			u16	subsys_vendor_id;
			u16	subsys_id;
			u32	exp_rom_bar;
			u8	capabilities;
			u8	reserved1[3];
			u32	reserved2;
			u8	irq_line;
			u8	irq_pin;
			u8	min_gnt;
			u8	max_lat;
			struct msix_cap msix;
		} __attribute__((packed));
		/* Pad to PCI config space size */
		u8 		__pad[PCI_DEV_CFG_SIZE];
	};

	/* Private to lkvm */
	u32			bar_size[6];
	bool			bar_active[6];
	bar_activate_fn_t	bar_activate_fn;
	bar_deactivate_fn_t	bar_deactivate_fn;
	void 			*data;
	struct pci_config_operations	cfg_ops;
	/*
	 * PCI INTx# are level-triggered, but virtual device often feature
	 * edge-triggered INTx# for convenience.
	 */
	enum irq_type		irq_type;
};

static inline u32 pci_bar_size(struct pci_device *pdev, int bar)
{
	return pdev->bar_size[bar];
}

static inline bool pci_bar_is_io(struct pci_device *pdev, int bar)
{
	return pdev->bar[bar] & PCI_BASE_ADDRESS_SPACE_IO;
}

static inline bool pci_io_space_enabled(u16 command)
{
	return command & PCI_COMMAND_IO;
}

static inline bool pci_memory_space_enabled(u16 command)
{
	return command & PCI_COMMAND_MEMORY;
}

static inline bool pci_bar_is_memory(struct pci_device *pdev, int bar)
{
	return !pci_bar_is_io(pdev, bar);
}

static inline u32 pci_bar_address_value(u32 bar)
{
	if (bar & PCI_BASE_ADDRESS_SPACE_IO)
		return bar & PCI_BASE_ADDRESS_IO_MASK;
	return bar & PCI_BASE_ADDRESS_MEM_MASK;
}

static inline u32 pci_bar_address(struct pci_device *pdev, int bar)
{
	return pci_bar_address_value(pdev->bar[bar]);
}

extern u16 pci_alloc_io_port_block(u32 size);
extern u32 pci_alloc_mmio_block(u32 size);
extern int pci_register_bar_regions(struct broiler *, struct pci_device *,
		bar_activate_fn_t, bar_deactivate_fn_t, void *);
extern int pci_assign_irq(struct pci_device *pdev);

#endif
