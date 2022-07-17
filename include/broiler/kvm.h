// SPDX-License-Identifier: GPL-2.0-only
#ifndef _BROILER_KVM_H
#define _BROILER_KVM_H

#include <stdbool.h>
#include <pthread.h>
#include <linux/kvm.h>
#include <signal.h>

#define BOOT_LOADER_SELECTOR	0x1000
#define BOOT_LOADER_IP		0x0000
#define BOOT_LOADER_SP		0x8000
#define BOOT_PROTOCOL_REQUIRED	0x206
#define BZ_KERNEL_START		0x100000UL
#define BOOT_CMDLINE_OFFSET	0x20000

#define KVM_CAP_IOEVENTFD	36
#define KVM_IRQ_OFFSET		5

#define SIG_BROILER_EXIT	(SIGRTMIN + 0)
#define SIG_BROILER_PAUSE	(SIGRTMIN + 1)
#define SIG_BROILER_TASK	(SIGRTMIN + 2)

#define MAX_KVM_CPUID_ENTRIES	100
#define MAX_SYM_LEN		128
#define SYMBOL_DEFAULT_UNKNOWN	"<unknown>"

#define MSR_IA32_SYSENTER_CS	0x00000174
#define MSR_IA32_SYSENTER_ESP	0x00000175
#define MSR_IA32_SYSENTER_EIP	0x00000176

#define MSR_STAR		0xc0000081 /* legacy mode SYSCALL target */
#define MSR_LSTAR		0xc0000082 /* long mode SYSCALL target */
#define MSR_CSTAR		0xc0000083 /* compat mode SYSCALL target */
#define MSR_SYSCALL_MASK	0xc0000084 /* EFLAGS mask for syscall */
#define MSR_KERNEL_GS_BASE	0xc0000102 /* SwapGS GS shadow */

#define MSR_IA32_TSC		0x00000010
#define MSR_IA32_MISC_ENABLE	0x000001a0

#define MSR_IA32_MISC_ENABLE_FAST_STRING_BIT	0
#define MSR_IA32_MISC_ENABLE_FAST_STRING	(1ULL << MSR_IA32_MISC_ENABLE_FAST_STRING_BIT)

#define BROILER_MSR_ENTRY(_index, _data)	\
	(struct kvm_msr_entry) { .index = _index, .data = _data }

#define KVM_EXIT_UNKNOWN		0
#define KVM_EXIT_EXCEPTION		1
#define KVM_EXIT_IO			2
#define KVM_EXIT_HYPERCALL		3
#define KVM_EXIT_DEBUG			4
#define KVM_EXIT_HLT			5
#define KVM_EXIT_MMIO			6
#define KVM_EXIT_IRQ_WINDOW_OPEN	7
#define KVM_EXIT_SHUTDOWN		8
#define KVM_EXIT_FAIL_ENTRY		9
#define KVM_EXIT_INTR			10
#define KVM_EXIT_SET_TPR		11
#define KVM_EXIT_TPR_ACCESS		12
#define KVM_EXIT_S390_SIEIC		13
#define KVM_EXIT_S390_RESET		14
#define KVM_EXIT_DCR			15 /* deprecated */
#define KVM_EXIT_NMI			16 
#define KVM_EXIT_INTERNAL_ERROR		17
#define KVM_EXIT_OSI			18
#define KVM_EXIT_PAPR_HCALL		19
#define KVM_EXIT_S390_UCONTROL		20
#define KVM_EXIT_WATCHDOG		21
#define KVM_EXIT_S390_TSCH		22
#define KVM_EXIT_EPR			23
#define KVM_EXIT_SYSTEM_EVENT		24
#define KVM_EXIT_S390_STSI		25
#define KVM_EXIT_IOAPIC_EOI		26
#define KVM_EXIT_HYPERV			27
#define KVM_EXIT_ARM_NISV		28
#define KVM_EXIT_X86_RDMSR		29
#define KVM_EXIT_X86_WRMSR		30
#define KVM_EXIT_DIRTY_RING_FULL	31
#define KVM_EXIT_AP_RESET_HOLD		32
#define KVM_EXIT_X86_BUS_LOCK		33
#define KVM_EXIT_XEN			34
#define KVM_EXIT_RISCV_SBI		35

/* CPUID flags we need to deal with */
#define KVM_X86_FEATURE_VMX		5 /* Hardware virtuallization */
#define KVM_X86_FEATURE_SVM		2 /* Secure virtual machine */
#define KVM_X86_FEATURE_XSAVE		26 /* XSAVE/XRSTOR/XSETBV/XGETBV */

#define CPUID_VENDOR_INTEL_1		0x756e6547 /* "Genu" */
#define CPUID_VENDOR_INTEL_2		0x49656e69 /* "ineI" */
#define CPUID_VENDOR_INTEL_3		0x6c65746e /* "ntel" */

#define CPUID_VENDOR_AMD_1		0x68747541 /* "Auth" */
#define CPUID_VENDOR_AMD_2		0x69746e65 /* "enti" */
#define CPUID_VENDOR_AMD_3		0x444d4163 /* "cAMD" */

#define SIGBROILEREXIT			(SIGRTMIN + 0)
#define SIGBROILERPAUSE			(SIGRTMIN + 1)
#define SIGBROILERTASK			(SIGRTMIN + 2)

#define DEFINE_KVM_EXT(ext)		\
	.name = #ext,			\
	.code = ext

struct broiler_cpu {
	pthread_t thread; /* VCPU thread */ 

	unsigned long cpu_id; 

	struct broiler *broiler; /* parent KVM */
	int vcpu_fd; /* For VCPU ioctls() */
	struct kvm_run *kvm_run;
	struct broiler_cpu_task *task;

	struct kvm_regs regs;
	struct kvm_sregs sregs;
	struct kvm_fpu fpu;

	struct kvm_msrs *msrs; /* dynamically allocated */

	u8 is_running;
	u8 paused;
	u8 needs_nmi;

	struct kvm_coalesced_mmio_ring *ring;
};

struct broiler_cpu_task {   
	void (*func)(struct broiler_cpu *vcpu, void *data);
	void *data;
};

struct kvm_ext {
	const char *name;
	int code;
};

struct cpuid_regs {
	u32	eax;
	u32	ebx;
	u32	ecx;
	u32	edx;
};

static inline void host_cpuid(struct cpuid_regs *regs)
{               
	asm volatile("cpuid"
		   : "=a" (regs->eax),
		     "=b" (regs->ebx),
		     "=c" (regs->ecx),
		     "=d" (regs->edx)
		   : "0" (regs->eax), "2" (regs->ecx));
}

extern int kvm_init(struct broiler *broiler);
extern void kvm_exit(struct broiler *broiler);
extern bool kvm_support_extension(struct broiler *broiler,
					unsigned int extension);
extern void broiler_reboot(struct broiler *broiler);
extern int __attribute__((weak)) broiler_cpu_get_endianness(struct broiler_cpu *);
extern __thread struct broiler_cpu *current_broiler_cpu;
extern void broiler_pause(struct broiler *broiler);
extern void broiler_continue(struct broiler *broiler);

#endif
