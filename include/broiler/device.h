#ifndef _BISCUITOS_DEVICE_H
#define _BISCUITOS_DEVICE_H

#include "linux/rbtree.h"

enum device_bus_type {
	DEVICE_BUS_PCI,
	DEVICE_BUS_MMIO,
	DEVICE_BUS_IOPORT,
	DEVICE_BUS_MAX, 
};

struct device {  
	enum device_bus_type	bus_type;
	void			*data;
	int			dev_num; 
	struct rb_node		node;
};

struct device_bus {
	struct rb_root		root;
	int			dev_num;
};

extern struct device *device_search(enum device_bus_type bus_type, u8 devnum);
extern struct device *device_first_dev(enum device_bus_type bus_type);
extern struct device *device_next_dev(struct device *dev);
extern struct device *device_find_dev(enum device_bus_type bus_type, u8 dev_num);
extern int device_register(struct device *dev);
extern void device_unregister(struct device *dev);

#endif
