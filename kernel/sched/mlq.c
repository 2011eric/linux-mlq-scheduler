#include "sched.h"

static inline struct task_struct *mlq_task_of(struct sched_mlq_entity *mlq_se)
{
	return container_of(mlq_se, struct task_struct, mlq);
}

static inline struct rq *rq_of_mlq_rq(struct mlq_rq *mlq_rq)
{
	return container_of(mlq_rq, struct rq, mlq);
}

static inline struct rq *rq_of_mlq_se(struct sched_mlq_entity *mlq_se)
{
	struct task_struct *p = mlq_task_of(mlq_se);

	return task_rq(p);
}

static inline struct mlq_rq *mlq_rq_of_se(struct sched_mlq_entity *mlq_se)
{
	struct rq *rq = rq_of_mlq_se(mlq_se);

	return &rq->mlq;
}
static inline struct list_head *internal_queue_of_se(struct sched_mlq_entity *mlq_se)
{
    int priority = mlq_task_of(mlq_se)->mlq_priority;
    struct mlq_rq *mlq_rq = mlq_rq_of_se(mlq_se);
    return &(mlq_rq->queues[priority-1]);
}
/* this function is invoked in core.c when calling setscheduler or setparam, here we initilize mlq_se */
void __setparam_mlq(struct task_struct *p, const struct sched_attr *attr)
{
    struct sched_mlq_entity *mlq_se = &p->mlq;

    INIT_LIST_HEAD(&mlq_se->run_list);
    if (attr->sched_priority == 1)
        mlq_se->time_slice = MLQ_FIRST_TIMESLICE;
    else if (attr->sched_priority == 2)
        mlq_se->time_slice = MLQ_SECOND_TIMESLICE;
    else
        mlq_se->time_slice = 0; /* Timeslice is not needed for FCFS */
}

static void update_curr_mlq(struct rq *rq)
{
    struct task_struct *curr = rq->curr;
    struct sched_mlq_entity *mlq_se = &curr->mlq;
    struct mlq_rq *mlq_rq = mlq_rq_of_se(mlq_se);
    u64 delta_exec;
    u64 now;

    if (curr->sched_class != &mlq_sched_class)
        return;

    now = rq_clock_task(rq);
    delta_exec = now - curr->se.exec_start;
    
    schedstat_set(curr->se.statistics.exec_max,
            max(curr->se.statistics.exec_max, delta_exec));

	curr->se.sum_exec_runtime += delta_exec;
	account_group_exec_runtime(curr, delta_exec);

	curr->se.exec_start = now;
	cgroup_account_cputime(curr, delta_exec);
}
/*
 * Adding/removing a task to/from a priority array:
 */
static void enqueue_mlq_entity(struct sched_mlq_entity *mlq_se)
{
    struct list_head *queue = internal_queue_of_se(mlq_se);

    if (!list_empty(&mlq_se->run_list))
        list_del_init(&mlq_se->run_list);
    list_add_tail(&mlq_se->run_list, queue);
    mlq_se->on_rq = 1;
}

static inline void dequeue_mlq_entity(struct sched_mlq_entity *mlq_se)
{
    if (!list_empty(&mlq_se->run_list))
        list_del_init(&mlq_se->run_list);
    mlq_se->on_rq = 0;
}

static void requeue_mlq_entity(struct sched_mlq_entity *mlq_se)
{
    struct list_head *queue = internal_queue_of_se(mlq_se);
    list_move_tail(&mlq_se->run_list, queue);
}

static void enqueue_task_mlq(struct rq *rq, struct task_struct *p, int flags)
{
    struct sched_mlq_entity *mlq_se = &p->mlq;
    enqueue_mlq_entity(mlq_se);
}

static void dequeue_task_mlq(struct rq *rq, struct task_struct *p, int flags)
{
    struct sched_mlq_entity *mlq_se = &p->mlq;
    update_curr_mlq(rq);
    dequeue_mlq_entity(mlq_se);
}

static void yield_task_mlq(struct rq *rq)
{
    struct task_struct *p = rq->curr;
    struct sched_mlq_entity *mlq_se = &p->mlq;
    requeue_mlq_entity(mlq_se);
}

static void put_prev_task_mlq(struct rq *rq, struct task_struct *p)
{
    update_curr_mlq(rq);
}

static void set_next_task_mlq(struct rq *rq, struct task_struct *p, bool first)
{
    p->se.exec_start = rq_clock_task(rq);
}

static struct sched_mlq_entity *pick_next_entity_mlq(struct list_head *queue)
{
    struct sched_mlq_entity *mlq_se;

    if (list_empty(queue))
        return NULL;
    
    return list_first_entry(queue->next, struct sched_mlq_entity, run_list);
}

static struct task_struct *pick_task_mlq(struct rq *rq)
{
    struct sched_mlq_entity *mlq_se;
    struct task_struct *p;
    int i;
    for (i = 0;i < MLQ_WIDTH; i++)
    {
        mlq_se = pick_next_entity_mlq(&rq->mlq.queues[i]);
        if (mlq_se)
        {
            p = mlq_task_of(mlq_se);
            return p;
        }
    }
    return NULL;    
}

static struct task_struct *pick_next_task_mlq(struct rq *rq)
{
	struct task_struct *p = pick_task_mlq(rq);

	if (p)
		set_next_task_mlq(rq, p, true);

	return p;
}

static void check_preempt_curr_mlq(struct rq *rq, struct task_struct *p, int flags)
{
    if (p->prio < rq->curr->prio)
        resched_curr(rq);
}

static void task_tick_mlq(struct rq *rq, struct task_struct *p, int queued)
{
    struct sched_mlq_entity *mlq_se = &p->mlq;

    update_curr_mlq(rq);

    if (p->mlq_priority == 3)
        return;

    if (--mlq_se->time_slice)
        return;

    /* used up all the timeslice */
    if (p->mlq_priority == 1)
        mlq_se->time_slice = MLQ_FIRST_TIMESLICE;
    else
        mlq_se->time_slice = MLQ_SECOND_TIMESLICE;

    if (mlq_se->run_list.next != internal_queue_of_se(mlq_se)){
        requeue_mlq_entity(mlq_se);
        resched_curr(rq);
    }
}

static unsigned int get_rr_interval_mlq(struct rq *rq, struct task_struct *task)
{
    if (task->mlq_priority == 1)
        return MLQ_FIRST_TIMESLICE;
    else if (task->mlq_priority == 2)
        return MLQ_SECOND_TIMESLICE;
    else
        return 0;
}

static void
prio_changed_mlq(struct rq *rq, struct task_struct *p, int oldprio)
{
    struct sched_mlq_entity *mlq_se = &p->mlq;

    if (mlq_se->on_rq)
    {
        dequeue_mlq_entity(mlq_se);
        enqueue_mlq_entity(mlq_se);
    }
    if (task_current(rq, p)){
        if (p->prio > oldprio)
            resched_curr(rq);
    } else {
        if (p->prio < rq->curr->prio)
            resched_curr(rq);
    }

}

static void switched_to_mlq(struct rq *rq, struct task_struct *p)
{
    if (task_on_rq_queued(p) && p->prio < rq->curr->prio)
        resched_curr(rq);
}

static int
balance_mlq(struct rq *rq, struct task_struct *prev, struct rq_flags *rf)
{
    for (int i = 0; i < MLQ_WIDTH; i++){
        if (!list_empty(&rq->mlq.queues[i]))
            return 1;
    }
    //TODO: try to do some balancing
    return 0;
}

static int
select_task_rq_mlq(struct task_struct *p, int cpu, int flags)
{
    //TODO: implement this
    return cpu;
}
void init_mlq_rq(struct mlq_rq *mlq_rq)
{
    int i;
    for (i = 0; i < MLQ_WIDTH; i++)
        INIT_LIST_HEAD(&mlq_rq->queues[i]);
}

/* Assumes rq->lock is held */
static void rq_online_mlq(struct rq *rq)
{
	cpupri_set(&rq->rd->cpupri, rq->cpu, CPUPRI_NORMAL);
}

/* Assumes rq->lock is held */
static void rq_offline_mlq(struct rq *rq)
{
	cpupri_set(&rq->rd->cpupri, rq->cpu, CPUPRI_INVALID);
}

DEFINE_SCHED_CLASS(mlq) = {

	.enqueue_task		= enqueue_task_mlq,
	.dequeue_task		= dequeue_task_mlq,
	.yield_task		= yield_task_mlq,

	.check_preempt_curr	= check_preempt_curr_mlq,

	.pick_next_task		= pick_next_task_mlq,
	.put_prev_task		= put_prev_task_mlq,
	.set_next_task          = set_next_task_mlq,

#ifdef CONFIG_SMP
	.balance		= balance_mlq,
	.pick_task		= pick_task_mlq,
	.select_task_rq		= select_task_rq_mlq,
	.set_cpus_allowed       = set_cpus_allowed_common,
	.rq_online              = rq_online_mlq,
	.rq_offline             = rq_offline_mlq,
	.task_woken		= task_woken_rt,
	.switched_from		= switched_from_rt,
	.find_lock_rq		= find_lock_lowest_rq,
#endif */

	.task_tick		= task_tick_mlq,

	.get_rr_interval	= get_rr_interval_mlq,

	.prio_changed		= prio_changed_mlq,
	.switched_to		= switched_to_mlq,

	.update_curr		= update_curr_mlq,

#ifdef CONFIG_UCLAMP_TASK
	.uclamp_enabled		= 1,
#endif
};