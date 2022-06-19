#ifndef _BISCUITOS_BROILER_H
#define _BISCUITOS_BROILER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include "broiler/types.h"
#include "broiler/bios-interrupt.h"
#include "broiler/kvm.h"
#include "broiler/disk.h"

/* Memory layout */
#define BROILER_IOPORT_AREA	(0x000000000)
#define BROILER_MMIO_START	(0x100000000)
#define BROILER_PCI_CFG_AREA	(BROILER_MMIO_START + 0x1000000)
#define BROILER_PCI_MMIO_AREA	(BROILER_MMIO_START + 0x2000000)

#define BROILER_MAX_CPUS	32
#define ARRAY_SIZE(x)		(sizeof(x) / sizeof((x)[0]))
#define PAGE_SIZE		4096

#define ALIGN(x,a)		__ALIGN_MASK(x,(typeof(x))(a)-1)
#define __ALIGN_MASK(x,mask)	(((x)+(mask))&~(mask))

#undef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

#ifndef container_of
/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:        the pointer to the member.
 * @type:       the type of the container struct this is embedded in.
 * @member:     the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({                      \
	const typeof(((type *)0)->member) * __mptr = (ptr);     \
	(type *)((char *)__mptr - offsetof(type, member)); })
#endif

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

	/* Disk */
	char *disk_name[MAX_DISK_IMAGES];
	struct disk_image **disks;
	int nr_disks;

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
extern int broiler_irq_init(struct broiler *broiler);
extern int broiler_ioport_setup(struct broiler *broiler);
extern int broiler_pci_init(struct broiler *broiler);
extern int broiler_pci_exit(struct broiler *broiler);
extern int broiler_disk_image_init(struct broiler *broiler);
extern int broiler_disk_image_exit(struct broiler *broiler);
extern int broiler_keyboard_init(struct broiler *broiler);
extern int broiler_terminal_init(struct broiler *broiler);
extern int broiler_terminal_exit(struct broiler *broiler);

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
