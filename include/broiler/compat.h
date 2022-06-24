#ifndef _BROILER_COMPAT_H
#define _BROILER_COMPAT_H

#include "linux/list.h"

struct compat_message {
	int id; 
	char *title;
	char *desc;
	struct list_head list;
};

extern int compat_add_message(const char *title, const char *desc);
extern int compat_remove_message(int id);

#endif
