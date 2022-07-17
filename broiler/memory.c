// SPDX-License-Identifier: GPL-2.0-only
#include "broiler/memory.h"
#include "broiler/kvm.h"
#include "linux/rbtree.h"
#include "linux/mutex.h"
#include <sys/mman.h>

static DEFINE_MUTEX(memory_lock);
static struct rb_root memory_tree = RB_ROOT;
static int memory_debug = 0;

/*
 * Guest Physical address translate to Host VA
 */
void *gpa_to_hva(struct broiler *broiler, u64 offset)
{               
	struct broiler_memory_region *mr;
	struct rb_int_node *node;

	node = rb_int_search_single(&memory_tree, offset);
	if (node == NULL)
		return NULL;

	mr = memory_node(node);
	if (offset >= mr->guest_phys_addr &&
			offset <= (mr->guest_phys_addr + mr->size)) {
		void *hva = mr->host_addr + (offset - mr->guest_phys_addr);

		if (memory_debug)
			printf("GPA %#016lx HVA %#016lx\n",
				(unsigned long)offset, (unsigned long)hva);

		return hva;
	}

	printf("Invalid Guest Physical address offset %#llx\n", offset);

	return NULL;
} 

static int
memory_region_insert(struct rb_root *root, struct broiler_memory_region *mr)
{
	rb_int_insert(root, &mr->node);
}

static void
memory_region_remove(struct rb_root *root, struct broiler_memory_region *mr)
{
	rb_int_erase(root, &mr->node);
}

/*
 * Broiler memory layout:
 *
 * +--------------------+----------------+------------+------------+
 * |                    |                |            |            |
 * +--------------------+----------------+------------+------------+
 * | <----- DRAM -----> | <--- MMIO ---> | <- DRAM -> | <- MMIO -> |
 * 0                    3Gig             4Gig         X
 *                      |
 *                      BROILER_32BIT_GAP_START
 */
static int broiler_memory_layout_init(struct broiler *broiler)
{
	if (broiler->ram_size < BROILER_32BIT_GAP_START) {
		broiler->hva_start = mmap(NULL,
				broiler->ram_size,
				PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS |
				MAP_NORESERVE,
				-1, 0);
	} else {
		broiler->hva_start = mmap(NULL,
				broiler->ram_size + BROILER_32BIT_GAP_SIZE,
				PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS |
				MAP_NORESERVE,
				-1, 0);
		broiler->ram_size += BROILER_32BIT_GAP_SIZE;
		if (broiler->hva_start != MAP_FAILED) 
			mprotect(broiler->hva_start + BROILER_32BIT_GAP_START,
					BROILER_32BIT_GAP_SIZE, PROT_NONE);
	}
	if (broiler->hva_start == MAP_FAILED)
		return -ENOMEM;

	madvise(broiler->hva_start, broiler->ram_size, MADV_MERGEABLE);
	return 0;
}

static int broiler_memory_layout_exit(struct broiler *broiler)
{
	munmap(broiler->hva_start, broiler->ram_size);
}

static int 
broiler_register_memory(struct broiler *broiler, u64 guest_phys, u64 size,
			void *userspace_addr, enum memory_type type)
{
	struct kvm_userspace_memory_region mem;
	struct broiler_memory_region *mr = NULL;
	struct broiler_memory_region *tmp, *next;
	u32 flags = 0;
	u32 slot = 0;
	int ret;

	mutex_lock(&memory_lock);
	rbtree_postorder_for_each_entry_safe(tmp, next,
						&memory_tree, node.node) {
		u64 bank_end = tmp->guest_phys_addr + tmp->size;
		u64 end = guest_phys + size;

		/* Keep the banks sorted ascending by slot, so it's easiler
		 * for use to find a free slot */
		if (guest_phys > bank_end || end < tmp->guest_phys_addr) {
			if (tmp->slot == slot)
				slot++;
			continue;
		}

		/* forbidden memory region overlay */
		printf("Region [%#lx - %#lx] overlay [%#lx - %#lx]\n",
				(unsigned long)tmp->guest_phys_addr,
				(unsigned long)bank_end,
				(unsigned long)guest_phys,
				(unsigned long)end);
		ret = -EINVAL;
		goto out;

	}
	
	mr = malloc(sizeof(struct broiler_memory_region));
	if (!mr)
		return -ENOMEM;

	*mr = (struct broiler_memory_region) {
		.node = RB_INT_INIT(guest_phys, guest_phys + size),
		.guest_phys_addr = guest_phys,
		.host_addr = userspace_addr,
		.size = size,
		.type = type,
		.slot = slot,
	};

	mem = (struct kvm_userspace_memory_region) {
		.slot		= slot,
		.flags		= flags,
		.guest_phys_addr= guest_phys,
		.memory_size	= size,
		.userspace_addr	= (unsigned long)userspace_addr,
	};

	if (ioctl(broiler->vm_fd, KVM_SET_USER_MEMORY_REGION, &mem) < 0) {
		ret = -errno;
		goto out;
	}

	memory_region_insert(&memory_tree, mr);
	ret = 0;

out:
	mutex_unlock(&memory_lock);
	return ret;
}

/*
 * Allocating RAM size bigger than 4Gig requires us to leave a gap
 * in the RAM which is used for PCI MMIO, hotplug, and unconfiguraed
 * devices (see documentation of e820_setup_gap() for details).
 *
 * If we're required to initialize RAM bigger than 4Gig, we will create
 * a gap between 0xC0000000 and 0x100000000 in the guest virtual mem space.
 */
static int broiler_memory_bank_init(struct broiler *broiler)
{
	u64 phys_start, phys_size;
	void *host_mem;

	if (broiler->ram_size < BROILER_32BIT_GAP_START) {
		/* Use a single block of RAM for 32bit RAM */
		phys_start = 0;
		phys_size  = broiler->ram_size;
		host_mem   = broiler->hva_start;

		broiler_register_memory(broiler, phys_start, 
			phys_size, host_mem, BROILER_MEM_TYPE_RAM);
	} else {
		/* First RAM range from zero to the PCI gap: */

		phys_start = 0;
		phys_size  = BROILER_32BIT_GAP_START;
		host_mem   = broiler->hva_start;

		broiler_register_memory(broiler, phys_start,
			phys_size, host_mem, BROILER_MEM_TYPE_RAM);

		/* Second RAM range from 4Gig to the end of RAM: */
		phys_start = BROILER_32BIT_MAX_MEM_SIZE;
		phys_size  = broiler->ram_size - phys_start;
		host_mem   = broiler->hva_start + phys_start;

		broiler_register_memory(broiler, phys_start,
			phys_size, host_mem, BROILER_MEM_TYPE_RAM);
	}
}

static int broiler_memroy_bank_exit(struct broiler *broiler)
{
	struct broiler_memory_region *tmp, *next;

	mutex_lock(&memory_lock);
	rbtree_postorder_for_each_entry_safe(tmp, next,
						&memory_tree, node.node) {
		memory_region_remove(&memory_tree, tmp);
		free(tmp);
	}
}

int broiler_memory_init(struct broiler *broiler)
{
	int ret;

	if (broiler_memory_layout_init(broiler) < 0) {
		printf("Memory layout init failed.\n");
		ret = -errno;
		goto out;
	}

	if (broiler_memory_bank_init(broiler) < 0 ) {
		printf("Memory region init failed.\n");
		ret = -errno;
		goto out;
	}

	ret = 0;
out:
	return ret;
}

int broiler_memory_exit(struct broiler *broiler)
{
	broiler_memroy_bank_exit(broiler);
	broiler_memory_layout_exit(broiler);

	return 0;
}
