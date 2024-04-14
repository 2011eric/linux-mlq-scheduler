/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_MLQ_H
#define _LINUX_SCHED_MLQ_H
#include <linux/sched.h>

struct task_struct;

static inline int mlq_prio(int prio)
{
	if (unlikely(prio >= MAX_RT_PRIO && prio < MAX_RT_PRIO + MLQ_WIDTH))
		return 1;
	return 0;
}

#define MLQ_FIRST_TIMESLICE   (50 * HZ / 1000)
#define MLQ_SECOND_TIMESLICE   (100 * HZ / 1000)

#endif
