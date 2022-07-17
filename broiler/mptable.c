// SPDX-License-Identifier: GPL-2.0-only
#include "broiler/broiler.h"
#include "broiler/bios.h"
#include "broiler/mptable.h"
#include "broiler/bios-export.h"
#include "broiler/apic.h"
#include "broiler/device.h"
#include "broiler/pci.h"
#include "broiler/memory.h"

static unsigned int gen_cpu_flag(unsigned int cpu, unsigned int ncpu)
{
	/* sets enabled/disabled | BSP/AP processor */
	return ((cpu < ncpu) ? CPU_ENABLED : 0) |
		((cpu == 0) ? CPU_BOOTPROCESSOR : 0x00);
}

static void mptable_add_irq_src(struct mpc_intsrc *mpc_intsrc,
		u16 srcbusid, u16 srcbusirq, u16 dstapic, u16 dstirq)
{
	*mpc_intsrc = (struct mpc_intsrc) {
		.type		= MP_INTSRC,
		.irqtype	= MP_INT,
		.srcbus		= srcbusid,
		.srcbusirq	= srcbusirq,
		.dstapic	= dstapic,
		.dstirq		= dstirq,
	};
}

static unsigned int mpf_checksum(unsigned char *mp, int len)
{
	unsigned int sum = 0;

	while (len--)
		sum += *mp++;

	return sum & 0xFF;
}

/*
 * mptable_setup - create mptable and fill guest memory with it.
 */
int broiler_mptable_init(struct broiler *broiler)
{
	unsigned long real_mpc_table, real_mpf_intel, size;
	struct mpf_intel *mpf_intel;
	struct mpc_table *mpc_table;
	struct mpc_cpu *mpc_cpu;
	struct mpc_bus *mpc_bus;
	struct mpc_ioapic *mpc_ioapic;
	struct mpc_intsrc *mpc_intsrc;
	struct device *dev;	
	const int pcibusid = 0;
	const int isabusid = 1;
	unsigned int i, nentries = 0;
	unsigned long ncpus = broiler->nr_cpu;
	unsigned int ioapicid;
	void *last_addr;

	/* That is where MP table will be in guest memory */
	real_mpc_table = ALIGN(BIOS_BEGIN + bios_rom_size, 16);
	
	mpc_table = calloc(1, MPTABLE_MAX_SIZE);
	if (!mpc_table)
		return -ENOMEM;

	MPTABLE_STRNCPY(mpc_table->signature, MPC_SIGNATURE);
	MPTABLE_STRNCPY(mpc_table->oem, MPTABLE_OEM);
	MPTABLE_STRNCPY(mpc_table->productid, MPTABLE_PRODUCTID);

	mpc_table->spec		= 4;
	mpc_table->lapic	= APIC_ADDR(0);
	mpc_table->oemcount	= ncpus; /* will be updated again at end */

	/*
	 * CPUs enumeration. Technically speaking we should
	 * ask either host or HV for apic version supported
	 * but for a while we simply put some random value
	 * here.
	 */
	mpc_cpu = (void *)&mpc_table[1];
	for (i = 0; i < ncpus; i++) {
		mpc_cpu->type		= MP_PROCESSOR;
		mpc_cpu->apicid		= i;
		mpc_cpu->apicver	= KVM_APIC_VERSION;
		mpc_cpu->cpuflag	= gen_cpu_flag(i, ncpus);
		mpc_cpu->cpufeature	= 0x600; /* some default value */
		mpc_cpu->featureflag	= 0x201; /* some default value */
		mpc_cpu++;
	}

	last_addr = (void *)mpc_cpu;
	nentries += ncpus;

	/*
	 * PCI buses.
	 * FIXME: Some callback here to obtain real number
	 *        of PCI buses present in system.
	 */
	mpc_bus		= last_addr;
	mpc_bus->type	= MP_BUS;
	mpc_bus->busid	= pcibusid;
	MPTABLE_STRNCPY(mpc_bus->bustype, MPTABLE_PCIBUSTYPE);

	last_addr = (void *)&mpc_bus[1];
	nentries++;

	/*
	 * ISA bus
	 * FIXME: Same issue as for PCI Bus.
	 */
	mpc_bus		= last_addr;
	mpc_bus->type	= MP_BUS;
	mpc_bus->busid	= isabusid;
	MPTABLE_STRNCPY(mpc_bus->bustype, MPTABLE_ISABUSTYPE);

	last_addr = (void *)&mpc_bus[1];
	nentries++;

	/*
	 * IO-APIC chip
	 */
	ioapicid		= ncpus + 1;
	mpc_ioapic		= last_addr;
	mpc_ioapic->type	= MP_IOAPIC;
	mpc_ioapic->apicid	= ioapicid;
	mpc_ioapic->apicver	= KVM_APIC_VERSION;
	mpc_ioapic->flags	= MPC_APIC_USABLE;
	mpc_ioapic->apicaddr	= IOAPIC_ADDR(0);

	last_addr = (void *)&mpc_ioapic[1];
	nentries++;

	/*
	 * IRQ sources.
	 * Also note we use PCI irqs here, no for ISA bus yet.
	 */
	dev = device_first_dev(DEVICE_BUS_PCI);
	while (dev) {
		struct pci_device *pdev = dev->data;
		unsigned char srcbusirq;

		srcbusirq = (pdev->subsys_id << 2) | (pdev->irq_pin - 1);
		mpc_intsrc = last_addr;
		mptable_add_irq_src(mpc_intsrc, pcibusid,
				srcbusirq, ioapicid, pdev->irq_line);
		last_addr = (void *)&mpc_intsrc[1];
		nentries++;
		dev = device_next_dev(dev);
	}

	/*
	 * Local IRQs assignment (LINT0, LINT1)
	 */
	mpc_intsrc		= last_addr;
	mpc_intsrc->type	= MP_LINTSRC;
	mpc_intsrc->irqtype	= MP_ExtINT;
	mpc_intsrc->irqtype	= MP_INT;
	mpc_intsrc->irqflag	= MP_IRQDIR_DEFAULT;
	mpc_intsrc->srcbus	= isabusid;
	mpc_intsrc->srcbusirq	= 0;
	mpc_intsrc->dstapic	= 0; /* FIXME: BSP apic */
	mpc_intsrc->dstirq	= 0; /* LINT0 */

	last_addr = (void *)&mpc_intsrc[1];
	nentries++;

	mpc_intsrc		= last_addr;
	mpc_intsrc->type	= MP_LINTSRC;
	mpc_intsrc->irqtype	= MP_NMI;
	mpc_intsrc->irqflag	= MP_IRQDIR_DEFAULT;
	mpc_intsrc->srcbus	= isabusid;
	mpc_intsrc->srcbusirq	= 0;
	mpc_intsrc->dstapic	= 0; /* FIXME: BSP apic */
	mpc_intsrc->dstirq	= 1; /* LINT1 */

	last_addr = (void *)&mpc_intsrc[1];
	nentries++;

	/*
	 * Floating MP table finally.
	 */
	real_mpf_intel = ALIGN((unsigned long)last_addr - 
				(unsigned long)mpc_table, 16);
	mpf_intel = (void *)((unsigned long)mpc_table + real_mpf_intel);

	MPTABLE_STRNCPY(mpf_intel->signature, MPTABLE_SIG_FLOATING);
	mpf_intel->length	= 1;
	mpf_intel->specification= 4;
	mpf_intel->physptr	= (unsigned int)real_mpc_table;
	mpf_intel->checksum	= -mpf_checksum((unsigned char *)mpf_intel,
						sizeof(*mpf_intel));

	/*
	 * No last_addr inclrement here please, we need last
	 * active position here to compute table size.
	 */

	/*
	 * Don't forget to update header in fixed table.
	*/
	mpc_table->oemcount     = nentries;
	mpc_table->length       = last_addr - (void *)mpc_table;
	mpc_table->checksum     = -mpf_checksum((unsigned char *)mpc_table, mpc_table->length);


	/*
	 * We will copy the whole table, no need to separate
	 * floating structure and table itkvm.
	 */
	size = (unsigned long)mpf_intel + sizeof(*mpf_intel) - 
						(unsigned long)mpc_table;

	/*
	 * The finial check -- never get out of system bios
	 * area. Lets also check for allocated memory overrun,
	 * in real it's late but still usefull.
	 */

	if (size > (unsigned long)(BIOS_END - bios_rom_size) ||
			size > MPTABLE_MAX_SIZE) {
		free(mpc_table);
		printf("MP table is too big\n");

		return -E2BIG;
	}

	/*
	 * OK, it is time to move it to guest memory.
	 */
	memcpy(gpa_flat_to_hva(broiler, real_mpc_table), mpc_table, size);

	free(mpc_table);

	return 0;
}

int broiler_mptable_exit(struct broiler *broiler)
{
	return 0;
}
