#include "broiler/broiler.h"

#define IRQCHIP_MASTER		0
#define IRQCHIP_SLAVE		1
#define IRQCHIP_IOAPIC		2

static struct kvm_irq_routing *irq_routing = NULL;
static int allocated_gsis = 0;
static int next_gsi;

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

int broiler_irq_init(struct broiler *broiler)
{
	int i;

	/* Hook first 8 GSIs to master IRCHIP */
	for (i = 0; i < 8; i++)
		if (i != 2)
			irq_add_routing(i, 
				KVM_IRQ_ROUTING_IRQCHIP, IRQCHIP_MASTER, i);

	/* Hook next 8 GSIs to slave IRQCHIP */
	for (i = 0; i < 24; i++)
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
