#ifndef _BISCUITOS_KVM_H
#define _BISCUITOS_KVM_H

#include <stdbool.h>
#include <pthread.h>
#include <linux/kvm.h>

#define BOOT_LOADER_SELECTOR	0x1000
#define BOOT_LOADER_IP		0x0000
#define BOOT_LOADER_SP		0x8000
#define BOOT_PROTOCOL_REQUIRED	0x206
#define BZ_KERNEL_START		0x100000UL
#define BOOT_CMDLINE_OFFSET	0x20000

#define KVM_CAP_IOEVENTFD	36

struct kvm_cpu {
	pthread_t thread; /* VCPU thread */ 

	unsigned long cpu_id; 

	struct broiler *broiler; /* parent KVM */
	int vcpu_fd; /* For VCPU ioctls() */
	struct kvm_run *kvm_run;
	struct kvm_cpu_task *task;

	struct kvm_regs regs;
	struct kvm_sregs sregs;
	struct kvm_fpu fpu;

	struct kvm_msrs *msrs; /* dynamically allocated */

	u8 is_running;
	u8 paused;
	u8 needs_nmi;

	struct kvm_coalesced_mmio_ring *ring;
};

struct kvm_cpu_task {   
	void (*func)(struct kvm_cpu *vcpu, void *data);
	void *data;
};

extern int kvm_init(struct broiler *broiler);
extern void kvm_exit(struct broiler *broiler);
extern bool kvm_support_extension(struct broiler *broiler,
					unsigned int extension);

#endif
