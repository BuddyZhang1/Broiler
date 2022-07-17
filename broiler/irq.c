// SPDX-License-Identifier: GPL-2.0-only
#include "broiler/broiler.h"
#include "broiler/irq.h"
#include "broiler/kvm.h"

#define IRQCHIP_MASTER		0
#define IRQCHIP_SLAVE		1
#define IRQCHIP_IOAPIC		2

static struct kvm_irq_routing *irq_routing = NULL;
static int allocated_gsis = 0;
static int next_gsi;
static struct msi_routing_ops irq_default_routing_ops;
static struct msi_routing_ops *msi_routing_ops = &irq_default_routing_ops;

void broiler_irq_line(struct broiler *broiler, int irq, int level)
{
	struct kvm_irq_level irq_level = {
		{
			.irq = irq,
		},
		.level = level,
	};

	if (ioctl(broiler->vm_fd, KVM_IRQ_LINE, &irq_level) < 0) {
		printf("KVM_IRQ_LINE failed.\n");
		exit(1);
	}
}

void broiler_irq_trigger(struct broiler *broiler, int irq)
{
	broiler_irq_line(broiler, irq, 1);
	broiler_irq_line(broiler, irq, 0);
}

static int irq_allocate_routing_entry(void)
{
	size_t table_size = sizeof(struct kvm_irq_routing);
	size_t old_size = table_size;
	int nr_entries = 0;

	if (irq_routing)
		nr_entries = irq_routing->nr;

	if (nr_entries < allocated_gsis)
		return 0;

	old_size += sizeof(struct kvm_irq_routing_entry) * allocated_gsis;
	allocated_gsis = ALIGN(nr_entries + 1, 32);
	table_size += sizeof(struct kvm_irq_routing_entry) * allocated_gsis;
	irq_routing = realloc(irq_routing, table_size);

	if (irq_routing == NULL)
		return -ENOMEM;
	memset((void *)irq_routing + old_size, 0, table_size - old_size);

	irq_routing->nr = nr_entries;
	irq_routing->flags = 0;

	return 0;
}

static int irq_add_routing(u32 gsi, u32 type, u32 irqchip, u32 pin)
{
	int r = irq_allocate_routing_entry();

	if (r)
		return r;

	irq_routing->entries[irq_routing->nr++] =
		(struct kvm_irq_routing_entry) {
			.gsi = gsi,
			.type = type,
			.u.irqchip.irqchip = irqchip,
			.u.irqchip.pin = pin,
		};

	return 0;
}

static bool update_data(u32 *ptr, u32 newdata)
{
	if (*ptr == newdata)
		return false;
	*ptr = newdata;
	return true;
}

void irq_update_msix_route(struct broiler *broiler, u32 gsi, struct msi_msg *msg)
{
	struct kvm_irq_routing_msi *entry;
	unsigned int i;
	bool changed;

	for (i = 0; i < irq_routing->nr; i++)
		if (gsi == irq_routing->entries[i].gsi)
			break;
	if (i == irq_routing->nr)
		return;

	entry = &irq_routing->entries[i].u.msi;
	changed  = update_data(&entry->address_hi, msg->address_hi);
	changed |= update_data(&entry->address_lo, msg->address_lo);
	changed |= update_data(&entry->data, msg->data);

	if (!changed)
		return;

	if (msi_routing_ops->update_route(broiler, &irq_routing->entries[i]))
		printf("KVM_SET_GSI_ROUTING bad\n");
}

static int irq_update_msix_routes(struct broiler * broiler,
				struct kvm_irq_routing_entry *entry)
{
	return ioctl(broiler->vm_fd, KVM_SET_GSI_ROUTING, irq_routing);
}

static int irq_default_signal_msi(struct broiler *broiler, struct kvm_msi *msi)
{
	return ioctl(broiler->vm_fd, KVM_SIGNAL_MSI, msi);
}

static bool irq_default_can_signal_msi(struct broiler *broiler)
{
	return kvm_support_extension(broiler, KVM_CAP_SIGNAL_MSI);
}

static struct msi_routing_ops irq_default_routing_ops = {
	.update_route	= irq_update_msix_routes,
	.signal_msi	= irq_default_signal_msi,
	.can_signal_msi	= irq_default_can_signal_msi,
};

bool irq_can_signal_msi(struct broiler *broiler)
{
	return msi_routing_ops->can_signal_msi(broiler);
}

int irq_signal_msi(struct broiler *broiler, struct kvm_msi *msi)
{
	return msi_routing_ops->signal_msi(broiler, msi);
}

static bool check_for_irq_routing(struct broiler *broiler)
{
	static int has_irq_routing = 0;

	if (has_irq_routing == 0) {
		if (kvm_support_extension(broiler, KVM_CAP_IRQ_ROUTING))
			has_irq_routing = 1;
		else
			has_irq_routing = -1;
	}

	return has_irq_routing > 0;
}

int irq_add_msix_route(struct broiler *broiler,
				struct msi_msg *msg, u32 device_id)
{
	struct kvm_irq_routing_entry *entry;
	int r;

	if (!check_for_irq_routing(broiler))
		return -ENXIO;

	r = irq_allocate_routing_entry();
	if (r)
		return r;

	entry = &irq_routing->entries[irq_routing->nr];
	*entry = (struct kvm_irq_routing_entry) {
		.gsi = next_gsi,
		.type = KVM_IRQ_ROUTING_MSI,
		.u.msi.address_hi = msg->address_hi,
		.u.msi.address_lo = msg->address_lo,
		.u.msi.data = msg->data,
	};
	irq_routing->nr++;

	r = msi_routing_ops->update_route(broiler, entry);
	if (r)
		return r;

	return next_gsi++;
}

int broiler_irq_init(struct broiler *broiler)
{
	int i;

	/* Hook first 8 GSIs to master IRCHIP */
	for (i = 0; i < 8; i++)
		if (i != 2)
			irq_add_routing(i, 
				KVM_IRQ_ROUTING_IRQCHIP, IRQCHIP_MASTER, i);

	/* Hook next 8 GSIs to slave IRQCHIP */
	for (i = 0; i < 16; i++)
		irq_add_routing(i, 
			KVM_IRQ_ROUTING_IRQCHIP, IRQCHIP_SLAVE, i - 8);

	/* Last but not least, IOAPIC */
	for (i = 0; i < 24; i++) {
		if (i == 0)
			irq_add_routing(i, 
				KVM_IRQ_ROUTING_IRQCHIP, IRQCHIP_IOAPIC, 2);
		else if (i != 2)
			irq_add_routing(i,
				KVM_IRQ_ROUTING_IRQCHIP, IRQCHIP_IOAPIC, i);
	}

	if (ioctl(broiler->vm_fd, KVM_SET_GSI_ROUTING, irq_routing) < 0) {
		free(irq_routing);
		return errno;
	}

	next_gsi = i;

	return 0;
}

void broiler_irq_exit(struct broiler *broiler) { }
