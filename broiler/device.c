#include "broiler/broiler.h"
#include "broiler/err.h"
#include "broiler/device.h"

static struct device_bus device_trees[DEVICE_BUS_MAX] = {
	[0 ... (DEVICE_BUS_MAX - 1)] = { RB_ROOT, 0 },
};

struct device *device_search(enum device_bus_type bus_type, u8 devnum)
{
	struct rb_node *node;

	if (bus_type >= DEVICE_BUS_MAX)
		return ERR_PTR(-EINVAL);

	node = device_trees[bus_type].root.rb_node;
	while (node) {
		struct device *dev = rb_entry(node, struct device, node);

		if (devnum < dev->dev_num)
			node = node->rb_left;
		else if (devnum > dev->dev_num)
			node = node->rb_right;
		else
			return dev;
	}

	return NULL;
}
