#ifndef _BISCUITOS_IOEVENTFD_H
#define _BISCUITOS_IOEVENTFD_H

#include "broiler/broiler.h"
#include "linux/list.h"

struct ioevent {
	u64	io_addr;
	u8	io_len;
	void(*fn)(struct broiler *broiler, void *ptr);
	struct broiler *broiler;
	void	*fn_ptr;
	int	fd;
	u64	datamatch;
	u32	flags;

	struct list_head list;
};

#endif
