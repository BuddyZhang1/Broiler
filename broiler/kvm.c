#include "broiler/broiler.h"
#include "broiler/virtio.h"
#include "broiler/kvm.h"
#include "broiler/utils.h"
#include <sys/prctl.h>
#include <signal.h>
#include <sys/mman.h>
#include <limits.h>

static int pause_event;
static DEFINE_MUTEX(pause_lock);
__thread struct broiler_cpu *current_broiler_cpu;

bool kvm_support_extension(struct broiler *broiler, unsigned int extension)
{
	if (ioctl(broiler->kvm_fd, KVM_CHECK_EXTENSION, extension) < 0)
		return false;
	return true;
}

int __attribute__((weak)) broiler_cpu_get_endianness(struct broiler_cpu *vcpu)
{
	return VIRTIO_ENDIAN_HOST;
}

void broiler_reboot(struct broiler *broiler)
{
	/* Check if the guest is running */
	printf("Broiler Reboot!\n");
}

static void broiler_notify_paused(void)
{
	u64 p = 1;

	if (write(pause_event, &p, sizeof(p)))
		die("Failed notifying of paused VCPU.");

	mutex_lock(&pause_lock);
	current_broiler_cpu->paused = 0;
	mutex_unlock(&pause_lock);
}

static void broiler_cpu_signal_handler(int signum)
{
	if (signum == SIG_BROILER_EXIT) {
		if (current_broiler_cpu && current_broiler_cpu->is_running)
			current_broiler_cpu->is_running = false;
	} else if (signum == SIG_BROILER_PAUSE) {
		if (current_broiler_cpu->paused)
			die("Pause signaled for already paused CPU\n");

		/* pause_lock is held by broiler_pause() */
		current_broiler_cpu->paused = 1;

		/*
		 * This is a blocking function and uses locks. It is safe
		 * to call it for this signal as a second pause event should
		 * not be send to this thread until it acquires and releases
		 * the pause_lock.
		 */
		broiler_notify_paused();
	}

	/* For SIG_BROILER_TASK cpu->task is already set */
}

static void filter_cpuid(struct kvm_cpuid2 *broiler_cpuid, int cpu_id)
{
	unsigned int i;

	/*
	 * Filer CPUID functions that are not supported by the hypervisor
	 */
	for (i = 0; i < broiler_cpuid->nent; i++) {
		struct kvm_cpuid_entry2 *entry = &broiler_cpuid->entries[i];

		switch (entry->function) {
		case 1:
			entry->ebx & ~(0xff << 24);
			entry->ebx |= cpu_id << 24;
			/* Set X86_FEATURE_HYPERVISOR */
			if (entry->index == 0)
				entry->ecx |= (1 << 31);
			break;
		case 6:
			/* Clear X86_FEATURE_EPB */
			entry->ecx = entry->ecx & ~(1 << 3);
			break;
		case 10: { /* Architectural Performance Monitoring */
			union cpuid10_eax {
				struct {
					unsigned int version_id		:8;
					unsigned int num_counters	:8;
					unsigned int bit_width		:8;
					unsigned int mask_length	:8;
				} split;
				unsigned int full;
			} eax;

			/*
			 * If the host has perf system running,
			 * but no architectural events available
			 * through kvm pmu -- disable perf support,
			 * thus guest won't event try to access msr
			 * registers.
			 */
			if (entry->eax) {
				eax.full = entry->eax;
				if (eax.split.version_id != 2 ||
					!eax.split.num_counters)
					entry->eax = 0;
			}
			break;
		}
		default:
			/* Keep the CPUID function */
			break;
		}
	}
}

static void broiler_cpu_setup_cpuid(struct broiler_cpu *vcpu)
{
	struct kvm_cpuid2 *broiler_cpuid;

	broiler_cpuid = calloc(1, sizeof(*broiler_cpuid) +
			MAX_KVM_CPUID_ENTRIES * sizeof(*broiler_cpuid->entries));
	broiler_cpuid->nent = MAX_KVM_CPUID_ENTRIES;

	if (ioctl(vcpu->broiler->kvm_fd, 
				KVM_GET_SUPPORTED_CPUID, broiler_cpuid) < 0)
		die_perror("KVM_GET_SUPPORT_CPUID failed");

	filter_cpuid(broiler_cpuid, vcpu->cpu_id);

	if (ioctl(vcpu->vcpu_fd, KVM_SET_CPUID2, broiler_cpuid) < 0)
		die_perror("KVM_SET_CPUID2 failed");

	free(broiler_cpuid);
}

static inline u32 selector_to_base(u16 selector)
{
	/*
	 * KVM on Intel requires 'base' to be 'selector * 16' in real mode.
	 */
	return (u32)selector << 4;
}

static void broiler_cpu_setup_sregs(struct broiler_cpu *vcpu)
{
	struct broiler *broiler = vcpu->broiler;

	if (ioctl(vcpu->vcpu_fd, KVM_GET_SREGS, &vcpu->sregs) < 0)
		die_perror("KVM_GET_SREGS failed");

	/* CS Segment */
	vcpu->sregs.cs.selector	= broiler->boot_selector;
	vcpu->sregs.cs.base	= selector_to_base(broiler->boot_selector);
	/* SS Segment */
	vcpu->sregs.ss.selector	= broiler->boot_selector;
	vcpu->sregs.ss.base	= selector_to_base(broiler->boot_selector);
	/* DS Segment */
	vcpu->sregs.ds.selector	= broiler->boot_selector;
	vcpu->sregs.ds.base	= selector_to_base(broiler->boot_selector);
	/* ES Segment */
	vcpu->sregs.es.selector	= broiler->boot_selector;
	vcpu->sregs.es.base	= selector_to_base(broiler->boot_selector);
	/* FS Segment */
	vcpu->sregs.fs.selector	= broiler->boot_selector;
	vcpu->sregs.fs.base	= selector_to_base(broiler->boot_selector);
	/* GS Segment */
	vcpu->sregs.gs.selector = broiler->boot_selector;
	vcpu->sregs.gs.base	= selector_to_base(broiler->boot_selector);

	if (ioctl(vcpu->vcpu_fd, KVM_SET_SREGS, &vcpu->sregs) < 0)
		die_perror("KVM_SET_SREGS failed");
}

static void broiler_cpu_setup_regs(struct broiler_cpu *vcpu)
{
	vcpu->regs = (struct kvm_regs) {
		/* We start the guest in 16-bit real mode */
		.rflags = 0x0000000000000002ULL,
		.rip	= vcpu->broiler->boot_ip,
		.rsp	= vcpu->broiler->boot_sp,
		.rbp	= vcpu->broiler->boot_sp,
	};

	if (vcpu->regs.rip > USHRT_MAX)
		die("IP %0xllx is too high for real mode", (u64)vcpu->regs.rip);

	if (ioctl(vcpu->vcpu_fd, KVM_SET_REGS, &vcpu->regs) < 0)
		die_perror("KVM_SET_REGS failed");
}

static void broiler_cpu_setup_msrs(struct broiler_cpu *vcpu)
{
	unsigned long ndx = 0;

	vcpu->msrs = calloc(1, sizeof(struct broiler_cpu) +
			      (sizeof(struct kvm_msr_entry) * 100));

	vcpu->msrs->entries[ndx++] =
			BROILER_MSR_ENTRY(MSR_IA32_SYSENTER_CS, 0x00);
	vcpu->msrs->entries[ndx++] =
			BROILER_MSR_ENTRY(MSR_IA32_SYSENTER_ESP, 0x00);
	vcpu->msrs->entries[ndx++] =
			BROILER_MSR_ENTRY(MSR_IA32_SYSENTER_EIP, 0x00);
	vcpu->msrs->entries[ndx++] =
			BROILER_MSR_ENTRY(MSR_STAR, 0x00);
	vcpu->msrs->entries[ndx++] =
			BROILER_MSR_ENTRY(MSR_CSTAR, 0x00);
	vcpu->msrs->entries[ndx++] =
			BROILER_MSR_ENTRY(MSR_KERNEL_GS_BASE, 0x00);
	vcpu->msrs->entries[ndx++] =
			BROILER_MSR_ENTRY(MSR_SYSCALL_MASK, 0x00);
	vcpu->msrs->entries[ndx++] =
			BROILER_MSR_ENTRY(MSR_LSTAR, 0x00);
	vcpu->msrs->entries[ndx++] =
			BROILER_MSR_ENTRY(MSR_IA32_TSC, 0x00);
	vcpu->msrs->entries[ndx++] =
			BROILER_MSR_ENTRY(MSR_IA32_MISC_ENABLE,
					  MSR_IA32_MISC_ENABLE_FAST_STRING);
	vcpu->msrs->nmsrs = ndx;

	if (ioctl(vcpu->vcpu_fd, KVM_SET_MSRS, vcpu->msrs) < 0)
		die_perror("KVM_SET_MSRS failed");
}

static void broiler_cpu_setup_fpu(struct broiler_cpu *vcpu)
{
	vcpu->fpu = (struct kvm_fpu) {
		.fcw	= 0x37f,
		.mxcsr	= 0x1f80,
	};

	if (ioctl(vcpu->vcpu_fd, KVM_SET_FPU, &vcpu->fpu) < 0)
		die_perror("KVM_SET_FPU failed");
}

static void broiler_cpu_reset_vcpu(struct broiler_cpu *vcpu)
{
	broiler_cpu_setup_cpuid(vcpu);
	broiler_cpu_setup_sregs(vcpu);
	broiler_cpu_setup_regs(vcpu);
	broiler_cpu_setup_fpu(vcpu);
	broiler_cpu_setup_msrs(vcpu);
}

static void broiler_cpu_run(struct broiler_cpu *vcpu)
{
	int err;

	if (!vcpu->is_running)
		return;

	err = ioctl(vcpu->vcpu_fd, KVM_RUN, 0);
	if (err < 0 && (errno != EINTR && errno != EAGAIN))
		die_perror("KVM_RUN failed");
}

static int broiler_cpu_start(struct broiler_cpu *cpu)
{
	sigset_t sigset;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGALRM);

	pthread_sigmask(SIG_BLOCK, &sigset, NULL);

	signal(SIG_BROILER_EXIT, broiler_cpu_signal_handler);
	signal(SIG_BROILER_PAUSE, broiler_cpu_signal_handler);
	signal(SIG_BROILER_TASK, broiler_cpu_signal_handler);

	broiler_cpu_reset_vcpu(cpu);

	while (cpu->is_running) {
		/* KVM RUN */
		broiler_cpu_run(cpu);

		printf("REASONE: %d\n", cpu->kvm_run->exit_reason);
		switch (cpu->kvm_run->exit_reason) {
		case KVM_EXIT_UNKNOWN:
			break;	
		default:
			return 1;
		}
	}

	return 0;
}

static void *broiler_cpu_thread(void *arg)
{
	char name[20];

	current_broiler_cpu = arg;
	sprintf(name, "Broiler-vcpu-%lu", current_broiler_cpu->cpu_id);
	prctl(PR_SET_NAME, name);

	if (broiler_cpu_start(current_broiler_cpu))
		goto panic_broiler;

	return (void *)(intptr_t) 0;

panic_broiler:

	return (void *)(intptr_t) 1;
}

int broiler_cpu_running(struct broiler *broiler)
{
	int i;

	for (i = 0; i < broiler->nr_cpu; i++) {
		if (pthread_create(&broiler->cpus[i]->thread, NULL,
				broiler_cpu_thread, broiler->cpus[i]) != 0)
			printf("Unable to create KVM VCPU thread");
	}

	/* Only VCPU #0 is going to exit by itself when shutting down */
	if (pthread_join(broiler->cpus[0]->thread, NULL) != 0)
		printf("unable to join with vcpu 0\n");

	return 0;
}

int broiler_cpu_exit(struct broiler *broiler)
{
	return 0;
}

int kvm_init(struct broiler *broiler)
{
	struct kvm_userspace_memory_region mem;
	int ret;

	/* Open KVM */
	broiler->kvm_fd = open("/dev/kvm", O_RDWR);
	if (broiler->kvm_fd < 0) {
		printf("/dev/kvm doesn't exist\n");
		ret = -errno;
		goto err_open_kvm;
	}

	/* Check Version */
	ret = ioctl(broiler->kvm_fd, KVM_GET_API_VERSION, 0);
	if (ret != KVM_API_VERSION) {
		printf("KVM_API_VERSION ioctl.\n");
		ret = -errno;
		goto err_kvm_version;
	}

	/* Create KVM VM */
	broiler->vm_fd = ioctl(broiler->kvm_fd, KVM_CREATE_VM, 0);
	if (broiler->vm_fd < 0) {
		printf("KVM_CREATE_VM failed\n");
		ret = -errno;
		goto err_create_vm;
	}
	
	/* Set TSS */
	ret = ioctl(broiler->vm_fd, KVM_SET_TSS_ADDR, 0xFFFBD000);
	if (ret < 0) {
		printf("KVM_SET_TSS_ADDR error.\n");
		ret = -errno;
		goto err_set_tss;
	}

	/* Alloc HVA */
	broiler->hva_start = mmap(NULL, broiler->ram_size,
				  PROT_READ | PROT_WRITE,
				  MAP_PRIVATE | MAP_ANONYMOUS |
				  MAP_NORESERVE,
				  -1, 0);
	if (broiler->hva_start == MAP_FAILED) {
		printf("HVA alloc failed.\n");
		ret = -errno;
		goto err_hva_alloc;
	}

	/* KVM create memory slot */
	mem = (struct kvm_userspace_memory_region) {
		.slot			= 0,
		.flags			= 0,
		.guest_phys_addr	= 0,
		.memory_size		= broiler->ram_size,
		.userspace_addr		= (unsigned long)broiler->hva_start,
	};
	ret = ioctl(broiler->vm_fd, KVM_SET_USER_MEMORY_REGION, &mem);
	if (ret < 0) {
		ret = -errno;
		goto err_memslot;
	}

	/* PCI Region */
	broiler->pci_base = mem.guest_phys_addr + mem.memory_size;

	/* IRQ */
	ret = ioctl(broiler->vm_fd, KVM_CREATE_IRQCHIP);
	if (ret < 0) {
		printf("KVM_CREATE_IRQCHIP\n");
		ret = -errno;
		goto err_create_irqchip;
	}

	return 0;

err_create_irqchip:
err_memslot:
	munmap(broiler->hva_start, broiler->ram_size);
err_hva_alloc:
err_set_tss:
err_create_vm:
err_kvm_version:
	close(broiler->kvm_fd);
err_open_kvm:
	return ret;
}

void kvm_exit(struct broiler *broiler)
{

}
