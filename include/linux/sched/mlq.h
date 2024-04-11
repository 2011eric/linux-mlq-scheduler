/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_MLQ_H
#define _LINUX_SCHED_MLQ_H
#include <linux/sched.h>

struct task_struct;

static inline int rt_prio(int prio)
{
	if (unlikely(prio >= MAX_RT_PRIO && prio < MAX_RT_PRIO + MLQ_WIDTH))
		return 1;
	return 0;
}



#endif