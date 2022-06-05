#ifndef _BISCUITOS_BROILER_H
#define _BISCUITOS_BROILER_H

#include "broiler/types.h"
#include "broiler/bios-interrupt.h"
#include "broiler/kvm.h"

#define BROILER_MAX_CPUS	32
#define ARRAY_SIZE(x)		(sizeof(x) / sizeof((x)[0]))
#define PAGE_SIZE		4096

/* broiler as vm */
struct broiler {
	int kvm_fd;
	int vm_fd;

	/* opts */
	char *kernel_name;
	char *rootfs_name;
	char *cmdline;

	/* CPU */
	int nr_cpu;
	struct kvm_cpu **cpus;

	/* Memory */
	u64 ram_size;
	void *hva_start;

	/* PCI */
	unsigned long pci_base;

	/* boot */
	u16 boot_selector;
	u16 boot_ip;
	u16 boot_sp;

	/* BIOS interrupt */
	struct interrupt_table interrupt_table;
};

extern int broiler_base_init(struct broiler *broiler);
extern int broiler_load_kernel(struct broiler *broiler);
extern int broiler_setup_bios(struct broiler *broiler);
extern int ioeventfd_init(struct broiler *broiler);
extern int ioeventfd_exit(struct broiler *broiler);
extern int broiler_cpu_init(struct broiler *broiler);

static inline void *gpa_flat_to_hva(struct broiler *broiler, u64 offset)
{
	return broiler->hva_start + offset;
}

static inline void *gpa_real_to_hva(struct broiler *broiler, u16 selector,
                                        u16 offset)
{
	unsigned long flat = ((u32)selector << 4) + offset;

	return gpa_flat_to_hva(broiler, flat);
}

#endif
