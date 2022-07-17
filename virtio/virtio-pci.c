// SPDX-License-Identifier: GPL-2.0-only
#include "broiler/broiler.h"
#include "broiler/virtio.h"
#include "broiler/irq.h"
#include "broiler/pci.h"
#include "broiler/ioport.h"
#include "broiler/ioeventfd.h"
#include <sys/eventfd.h>
#include <sys/epoll.h>


static u16 virtio_pci_port_addr(struct virtio_pci *vpci)
{
	return pci_bar_address(&vpci->pdev, 0);
}

static u32 virtio_pci_mmio_addr(struct virtio_pci *vpci)
{
	return pci_bar_address(&vpci->pdev, 1);
}

static u32 virtio_pci_msix_io_addr(struct virtio_pci *vpci)
{
	return pci_bar_address(&vpci->pdev, 2);
}

static inline bool virtio_pci_msix_enabled(struct virtio_pci *vpci)
{
	return vpci->pdev.msix.ctrl & PCI_MSIX_FLAGS_ENABLE;
}

static void virtio_pci_signal_msi(struct broiler *broiler,
				struct virtio_pci *vpci, int vec)
{
	struct kvm_msi msi = {
		.address_lo = vpci->msix_table[vec].msg.address_lo,
		.address_hi = vpci->msix_table[vec].msg.address_hi,
		.data = vpci->msix_table[vec].msg.data,
	};

	irq_signal_msi(broiler, &msi);
}

int virtio_pci_signal_vq(struct broiler *broiler,
				struct virtio_device *vdev, u32 vq)
{
	struct virtio_pci *vpci = vdev->virtio;
	int tbl = vpci->vq_vector[vq];

	printf("TRACE %s\n", __func__);
	if (virtio_pci_msix_enabled(vpci) && tbl != VIRTIO_MSI_NO_VECTOR) {
		if (vpci->pdev.msix.ctrl & PCI_MSIX_FLAGS_MASKALL ||
			vpci->msix_table[tbl].ctrl &
				PCI_MSIX_ENTRY_CTRL_MASKBIT) {
			vpci->msix_pba |= 1 << tbl;
			return 0;
		}

		if (vpci->features & VIRTIO_PCI_F_SIGNAL_MSI)
			virtio_pci_signal_msi(broiler, 
					vpci, vpci->vq_vector[vq]);
		else
			broiler_irq_trigger(broiler, vpci->gsis[vq]);
	} else {
		vpci->isr = VIRTIO_IRQ_HIGH;
		broiler_irq_line(broiler,
				vpci->legacy_irq_line, VIRTIO_IRQ_HIGH);
	}
	return 0;
}

int virtio_pci_signal_config(struct broiler *broiler,
					struct virtio_device *vdev)
{
	struct virtio_pci *vpci = vdev->virtio;

	printf("TRace %s\n", __func__);
	return 0;
}

static bool virtio_pci_specific_data_in(struct broiler *broiler,
	struct virtio_device *vdev, void *data, int size, unsigned long offset)
{
	struct virtio_pci *vpci = vdev->virtio;
	u32 config_offset;
	int type;

	type = virtio_get_dev_specific_field(offset - 20,
			virtio_pci_msix_enabled(vpci), &config_offset);
	if (type == VIRTIO_PCI_O_MSIX) {
		switch (offset) {
		case VIRTIO_MSI_CONFIG_VECTOR:
			ioport_write16(data, vpci->config_vector);
			break;
		case VIRTIO_MSI_QUEUE_VECTOR:
			ioport_write16(data,
				vpci->vq_vector[vpci->queue_selector]);
			break;
		}
		return true;
	} else if (type == VIRTIO_PCI_O_CONFIG) {
		u8 cfg;

		cfg = vdev->ops->get_config(broiler, vpci->data)[config_offset];
		ioport_write8(data, cfg);
		return true;
	}
	return false;
}

static bool
virtio_pci_data_in(struct broiler_cpu *vcpu, struct virtio_device *vdev,
				unsigned long offset, void *data, int size)
{
	struct virtio_pci *vpci;
	struct virt_queue *vq;
	struct broiler *broiler;
	bool ret = true;
	u32 val;

	broiler = vcpu->broiler;
	vpci = vdev->virtio;

	switch (offset) {
	case VIRTIO_PCI_HOST_FEATURES:
		val = vdev->ops->get_host_features(broiler, vpci->data);
		ioport_write32(data, val);
		break;
	case VIRTIO_PCI_QUEUE_PFN:
		vq = vdev->ops->get_vq(broiler, vpci->data,
						vpci->queue_selector);
		ioport_write32(data, vq->pfn);
		break;
	case VIRTIO_PCI_QUEUE_NUM:
		val = vdev->ops->get_size_vq(broiler, 
					vpci->data, vpci->queue_selector);
		ioport_write16(data, val);
		break;
	case VIRTIO_PCI_STATUS:
		ioport_write8(data, vpci->isr);
		broiler_irq_line(broiler, vpci->legacy_irq_line, VIRTIO_IRQ_LOW);
		vpci->isr = VIRTIO_IRQ_LOW;
		break;
	default:
		ret = virtio_pci_specific_data_in(broiler,
						vdev, data, size, offset);
		break;
	}
	return ret;
}

static void virtio_pci_ioevent_callback(struct broiler *broiler, void *param)
{
	struct virtio_pci_ioevent_param *ioeventfd = param;
	struct virtio_pci *vpci = ioeventfd->vdev->virtio;

	ioeventfd->vdev->ops->notify_vq(broiler, vpci->data, ioeventfd->vq);
}

static int virtio_pci_init_ioeventfd(struct broiler *broiler,
				struct virtio_device *vdev, u32 vq)
{
	struct virtio_pci *vpci = vdev->virtio;
	u32 mmio_addr = virtio_pci_mmio_addr(vpci);
	u32 port_addr = virtio_pci_port_addr(vpci);
	struct ioevent ioevent;
	int r, flags = 0;
	int fd;

	vpci->ioeventfds[vq] = (struct virtio_pci_ioevent_param) {
		.vdev		= vdev,
		.vq		= vq,
	};

	ioevent = (struct ioevent) {
		.fn		= virtio_pci_ioevent_callback,
		.fn_ptr		= &vpci->ioeventfds[vq],
		.datamatch	= vq,
		.broiler	= broiler,
	};

	/*
	 * Vhost will poll the eventfd in host kernel side, otherwise we
	 * need to poll in userspace.
	 */
	if (!vdev->use_vhost)
		flags |= IOEVENTFD_FLAG_USER_POLL;

	/* ioport */
	ioevent.io_addr	= port_addr + VIRTIO_PCI_QUEUE_NOTIFY;
	ioevent.io_len	= sizeof(u16);
	ioevent.fd	= fd = eventfd(0, 0);
	r = ioeventfd_add_event(&ioevent, flags | IOEVENTFD_FLAG_PIO);
	if (r)
		return r;

	/* mmio */
	ioevent.io_addr	= mmio_addr + VIRTIO_PCI_QUEUE_NOTIFY;
	ioevent.io_len	= sizeof(u16);
	ioevent.fd	= eventfd(0, 0);
	r = ioeventfd_add_event(&ioevent, flags);
	if (r)
		goto free_ioport_evt;

	if (vdev->ops->notify_vq_eventfd)
		vdev->ops->notify_vq_eventfd(broiler, vpci->data, vq, fd);
	return 0;

free_ioport_evt:
	ioeventfd_del_event(port_addr + VIRTIO_PCI_QUEUE_NOTIFY, vq);
	return r;
}

static void
virtio_pci_exit_vq(struct broiler *broiler, struct virtio_device *vdev, int vq)
{
	struct virtio_pci *vpci = vdev->virtio;
	u32 mmio_addr = virtio_pci_mmio_addr(vpci);
	u16 port_addr = virtio_pci_port_addr(vpci);

	ioeventfd_del_event(mmio_addr + VIRTIO_PCI_QUEUE_NOTIFY, vq);
	ioeventfd_del_event(port_addr + VIRTIO_PCI_QUEUE_NOTIFY, vq);
	virtio_exit_vq(broiler, vdev, vpci->data, vq);
}

static int virtio_pci_add_msix_route(struct virtio_pci *vpci, u32 vec)
{
	struct msi_msg *msg;
	int gsi;

	if (vec == VIRTIO_MSI_NO_VECTOR)
		return -EINVAL;

	msg = &vpci->msix_table[vec].msg;
	gsi = irq_add_msix_route(vpci->broiler, msg, vpci->dev.dev_num << 3);
	/*
	 * We don't need IRQ routing if we can use
	 * MSI injection via the KVM_SIGNAL_MSI ioctl.
	 */
	if (gsi == -ENXIO && vpci->features & VIRTIO_PCI_F_SIGNAL_MSI)
		return gsi;

	if (gsi < 0)
		die("XXXfailed to configure MSIs");

	return gsi;
}

static bool virtio_pci_specific_data_out(struct broiler *broiler,
	struct virtio_device *vdev, void *data, int size, unsigned long offset)
{
	struct virtio_pci *vpci = vdev->virtio;
	u32 config_offset, vec;
	int gsi, type;

	type = virtio_get_dev_specific_field(offset - 20,
			virtio_pci_msix_enabled(vpci), &config_offset);
	if (type == VIRTIO_PCI_O_MSIX) {
		switch (offset) {
		case VIRTIO_MSI_CONFIG_VECTOR:
			vec = vpci->config_vector = ioport_read16(data);

			gsi = virtio_pci_add_msix_route(vpci, vec);
			if (gsi < 0)
				break;

			vpci->config_gsi = gsi;
			break;
		case VIRTIO_MSI_QUEUE_VECTOR:
			vec = ioport_read16(data);
			vpci->vq_vector[vpci->queue_selector] = vec;

			gsi = virtio_pci_add_msix_route(vpci, vec);
			if (gsi < 0)
				break;

			vpci->gsis[vpci->queue_selector] = gsi;
			if (vdev->ops->notify_vq_gsi)
				vdev->ops->notify_vq_gsi(broiler, vpci->data,
						vpci->queue_selector, gsi);
			break;
		}

		return true;
	} else if (type == VIRTIO_PCI_O_CONFIG) {
		return virtio_access_config(broiler,
			vdev, vpci->data, config_offset, data, size, true); 
	}
	return false;
}

static bool
virtio_pci_data_out(struct broiler_cpu *vcpu, struct virtio_device *vdev,
				unsigned long offset, void *data, int size)
{
	struct virtio_pci *vpci;
	struct broiler *broiler;
	bool ret = true;
	u32 val;

	broiler = vcpu->broiler;
	vpci = vdev->virtio;

	switch (offset) {
	case VIRTIO_PCI_GUEST_FEATURES:
		val = ioport_read32(data);
		virtio_set_guest_features(broiler, vdev, vpci->data, val);
		break;
	case VIRTIO_PCI_QUEUE_PFN:
		val = ioport_read32(data);
		if (val) {
			virtio_pci_init_ioeventfd(broiler, vdev,
						vpci->queue_selector);
			vdev->ops->init_vq(broiler, vpci->data,
				vpci->queue_selector,
				1 << VIRTIO_PCI_QUEUE_ADDR_SHIFT,
				VIRTIO_PCI_VRING_ALIGN, val);
		} else
			virtio_pci_exit_vq(broiler, vdev, vpci->queue_selector);
		break;
	case VIRTIO_PCI_QUEUE_SEL:
		vpci->queue_selector = ioport_read16(data);
		break;
	case VIRTIO_PCI_QUEUE_NOTIFY:
		val = ioport_read16(data);
		vdev->ops->notify_vq(broiler, vpci->data, val);
		break;
	case VIRTIO_PCI_STATUS:
		vpci->status = ioport_read8(data);
		if (!vpci->status) /* Sample endianness on reset */
			vdev->endian = broiler_cpu_get_endianness(vcpu);
		virtio_notify_status(broiler, vdev, vpci->data, vpci->status);
		break;
	default:
		ret = virtio_pci_specific_data_out(broiler,
					vdev, data, size, offset);
		break;
	}

	return ret;
}

static void virtio_pci_io_mmio_callback(struct broiler_cpu *vcpu,
		u64 addr, u8 *data, u32 len, u8 is_write, void *ptr)
{
	struct virtio_device *vdev = ptr;
	struct virtio_pci *vpci = vdev->virtio;
	u32 ioport_addr = virtio_pci_port_addr(vpci);
	u32 base_addr;

	if (addr >= ioport_addr &&
		addr < ioport_addr + pci_bar_size(&vpci->pdev, 0))
		base_addr = ioport_addr;
	else
		base_addr = virtio_pci_mmio_addr(vpci);

	if (!is_write)
		virtio_pci_data_in(vcpu, vdev, addr - base_addr, data, len);
	else
		virtio_pci_data_out(vcpu, vdev, addr - base_addr, data, len);
}

static void update_msix_map(struct virtio_pci *vpci,
		struct msix_table *msix_entry, u32 vecnum)
{
	u32 gsi, i;

	/* Find the GSI number used for that vector */
	if (vecnum == vpci->config_vector)
		gsi = vpci->config_gsi;
	else {
		for (i = 0; i < VIRTIO_PCI_MAX_VQ; i++)
			if (vpci->vq_vector[i] == vecnum)
				break;
		if (i == VIRTIO_PCI_MAX_VQ)
			return;
		gsi = vpci->gsis[i];
	}

	if (gsi == 0)
		return;

	msix_entry = &msix_entry[vecnum];
	irq_update_msix_route(vpci->broiler, gsi, &msix_entry->msg);
}

static void virtio_pci_msix_mmio_callback(struct broiler_cpu *vcpu,
		u64 addr, u8 *data, u32 len, u8 is_write, void *ptr)
{
	struct virtio_device *vdev = ptr;
	struct virtio_pci *vpci = vdev->virtio;
	struct msix_table *table;
	u32 msix_io_addr, pba_offset;
	size_t offset;
	int vecnum;

	pba_offset = vpci->pdev.msix.pba_offset & ~PCI_MSIX_TABLE_BIR;
	if (addr >= msix_io_addr + pba_offset) {
		/* Read access to PBA */
		if (is_write)
			return;
		offset = addr - (msix_io_addr + pba_offset);
		if ((offset + len) > sizeof(vpci->msix_pba))
			return;
		memcpy(data, (void *)&vpci->msix_pba + offset, len);
		return;
	}

	table = vpci->msix_table;
	offset = addr - msix_io_addr;

	vecnum = offset / sizeof(struct msix_table);
	offset = offset % sizeof(struct msix_table);

	if (!is_write) {
		memcpy(data, (void *)&table[vecnum] + offset, len);
		return;
	}

	memcpy((void *)&table[vecnum] + offset, data, len);

	/* Did we just update the address or payload */
	if (offset < offsetof(struct msix_table, ctrl))
		update_msix_map(vpci, table, vecnum);
}

static int virtio_pci_bar_activate(struct broiler *broiler,
		struct pci_device *pdev, int bar, void *data)
{
	struct virtio_device *vdev = data;
	u32 bar_addr, bar_size;
	int r = -EINVAL;

	bar_addr = pci_bar_address(pdev, bar);
	bar_size = pci_bar_size(pdev, bar);

	switch (bar) {
	case 0:
		r = broiler_register_pio(broiler, bar_addr, bar_size,
				virtio_pci_io_mmio_callback, vdev);
		break;
	case 1:
		r = broiler_ioport_register(broiler, bar_addr, bar_size,
				virtio_pci_io_mmio_callback, vdev,
				DEVICE_BUS_MMIO);
		break;
	case 2:
		r = broiler_ioport_register(broiler, bar_addr, bar_size,
				virtio_pci_msix_mmio_callback, vdev,
				DEVICE_BUS_MMIO);
		break;
	}
	return r;
}

static int virtio_pci_bar_deactivate(struct broiler *broiler,
		struct pci_device *pdev, int bar, void *data)
{
	u32 bar_addr;
	bool success;
	int r = -EINVAL;

	bar_addr = pci_bar_address(pdev, bar);

	switch (bar) {
	case 0:
		r = broiler_deregister_pio(broiler, bar_addr);
		break;
	case 1:
	case 2:
		success = broiler_ioport_deregister(broiler,
						bar_addr, DEVICE_BUS_MMIO);
		/* fails when the region is not found */
		r = (success ? 0 : -ENOENT);
		break;
	}

	return r;
}

int virtio_pci_init(struct broiler *broiler, void *dev,
	struct virtio_device *vdev, int device_id, int subsys_id, int class)
{
	struct virtio_pci *vpci = vdev->virtio;
	u32 mmio_addr, msix_io_block;
	u16 port_addr;
	int r;

	vpci->broiler = broiler;
	vpci->data = dev;

	port_addr = pci_alloc_io_port_block(PCI_IO_SIZE);
	mmio_addr = pci_alloc_mmio_block(PCI_IO_SIZE);
	msix_io_block = pci_alloc_mmio_block(VIRTIO_MSIX_BAR_SIZE);

	/* pci device */
	vpci->pdev = (struct pci_device) {
		.vendor_id		= PCI_VENDOR_ID_REDHAT_QUMRANET,
		.device_id		= device_id,
		.command		= PCI_COMMAND_IO | PCI_COMMAND_MEMORY,
		.header_type		= PCI_HEADER_TYPE_NORMAL,
		.revision_id		= 0,
		.class[0]		= class & 0xff,
		.class[1]		= (class >> 8) & 0xff,
		.class[2]		= (class >> 16) & 0xff,
		.subsys_vendor_id	= 
					PCI_SUBSYSTEM_VENDOR_ID_REDHAT_QUMRANET,
		.subsys_id		= subsys_id,
		.bar[0]			= port_addr |
						PCI_BASE_ADDRESS_SPACE_IO,
		.bar[1]			= mmio_addr |
						PCI_BASE_ADDRESS_SPACE_MEMORY,
		.bar[2]			= msix_io_block |
						PCI_BASE_ADDRESS_SPACE_MEMORY,
		.status			= PCI_STATUS_CAP_LIST,
		.capabilities		= (void *)&vpci->pdev.msix -
					  		(void *)&vpci->pdev,
		.bar_size[0]		= PCI_IO_SIZE,
		.bar_size[1]		= PCI_IO_SIZE,
		.bar_size[2]		= VIRTIO_MSIX_BAR_SIZE,
	};

	r = pci_register_bar_regions(broiler, &vpci->pdev,
					virtio_pci_bar_activate,
					virtio_pci_bar_deactivate, vdev);
	if (r < 0)
		return r;

	vpci->dev = (struct device) {
		.bus_type	= DEVICE_BUS_PCI,
		.data		= &vpci->pdev,
	};

	vpci->pdev.msix.cap = PCI_CAP_ID_MSIX;
	vpci->pdev.msix.next = 0;

	/*
	 * We at most have VIRTIO_NR_MSIX entries (VIRTIO_PCI_MAX_VQ)
	 * entries for virt queue, VIRTIO_PCI_MAX_CONFIG entries for
	 * config).
	 *
	 * To quote the PCI spec:
	 *
	 * System software reads this field to determine the
	 * MSI-X Table Size N, which is encoded as N-1.
	 * For example, a returned value of "00000000011"
	 * indicates a table size of 4.
	 */
	vpci->pdev.msix.ctrl = VIRTIO_NR_MSIX - 1;

	/* Both table and PBA are mapped to the same BAR(2) */
	vpci->pdev.msix.table_offset = 2;
	vpci->pdev.msix.pba_offset = 2 | VIRTIO_MSIX_TABLE_SIZE;
	vpci->config_vector = 0;

	if (irq_can_signal_msi(broiler))
		vpci->features |= VIRTIO_PCI_F_SIGNAL_MSI;

	vpci->legacy_irq_line = pci_assign_irq(&vpci->pdev);

	r = device_register(&vpci->dev);
	if (r < 0)
		return r;

	return 0;
}

int virtio_pci_reset(struct broiler *broiler, struct virtio_device *vdev)
{
	struct virtio_pci *vpci = vdev->virtio;
	int vq;

	printf("Trace %s\n", __func__);
	for (vq = 0; vq < vdev->ops->get_vq_count(broiler, vpci->data); vq++)
		virtio_pci_exit_vq(broiler, vdev, vq);

	return 0;
}

int virtio_pci_exit(struct broiler *broiler, struct virtio_device *vdev)
{
	struct virtio_pci *vpci = vdev->virtio;

	printf("Trace %s\n", __func__);
	virtio_pci_reset(broiler, vdev);
	broiler_ioport_deregister(broiler,
			virtio_pci_mmio_addr(vpci), DEVICE_BUS_MMIO);
	broiler_ioport_deregister(broiler,
			virtio_pci_msix_io_addr(vpci), DEVICE_BUS_MMIO);
	broiler_deregister_pio(broiler, virtio_pci_port_addr(vpci));

	return 0;
}
