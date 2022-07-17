// SPDX-License-Identifier: GPL-2.0-only
#ifndef _BROILER_IPC_H
#define _BROILER_IPC_H

#define BROILER_SOCK_SUFFIX	".sock"
#define BROILER_IPC_MAX_MSGS	16

struct broiler_ipc_head {
	u32 type;
	u32 len;
};

enum {
	BROILER_IPC_BALLOON = 1,
	BROILER_IPC_DEBUG   = 2,
	BROILER_IPC_STAT    = 3,
	BROILER_IPC_PAUSE   = 4,
	BROILER_IPC_RESUME  = 5,
	BROILER_IPC_STOP    = 6,
	BROILER_IPC_PID     = 7,
	BROILER_IPC_VMSTATE = 8,
};

#endif
