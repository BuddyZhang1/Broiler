#ifndef _BISCUITOS_KVM_H
#define _BISCUITOS_KVM_H

#define BOOT_LOADER_SELECTOR	0x1000
#define BOOT_LOADER_IP		0x0000
#define BOOT_LOADER_SP		0x8000
#define BOOT_PROTOCOL_REQUIRED	0x206
#define BZ_KERNEL_START		0x100000UL
#define BOOT_CMDLINE_OFFSET	0x20000

extern int kvm_init(struct broiler *broiler);
extern void kvm_exit(struct broiler *broiler);

#endif
