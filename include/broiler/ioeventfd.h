#ifndef _BISCUITOS_IOEVENTFD_H
#define _BISCUITOS_IOEVENTFD_H

#include "broiler/broiler.h"
#include "linux/list.h"

#define IOEVENTFD_FLAG_PIO		(1 << 0)
#define IOEVENTFD_FLAG_USER_POLL	(1 << 1)
#define KVM_IOEVENTFD_HAS_PIO		1

struct ioevent {
	u64		io_addr;
	u8		io_len;
	struct broiler	*broiler;
	void		*fn_ptr;
	int		fd;
	u64		datamatch;
	u32		flags;

	struct list_head list;
	void(*fn)(struct broiler *broiler, void *ptr);
};

extern int ioeventfd_add_event(struct ioevent *ioevent, int flags);
extern int ioeventfd_del_event(u64 addr, u64 datamatch);

#endif
