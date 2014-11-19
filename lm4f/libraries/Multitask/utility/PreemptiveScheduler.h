/*
 * PreemptiveScheduler.h
 *
 *  Created on: 17/11/2014
 *      Author: Jose
 */

#ifndef PREEMPTIVESCHEDULER_H_
#define PREEMPTIVESCHEDULER_H_

#include <setjmp.h>

/* PreemptiveScheduler configuration */
#define PMT_MAX_TASKS               8
#define PMT_STACK_SIZE           2048
#define PMT_SWITCH_FREQ    (F_CPU)
// #define PMT_SWITCH_FREQ    (F_CPU/100)
#define PMT_PREALLOC_STACK          0

/* Constants */
#define PMT_MAIN_TASK         0   /* MAIN task must be task 0 */
#define PMT_CPU_REG_SP        8   /* setjmp register #8 contains the SP */
#define PMT_CPU_REG_PC        9   /* setjmp register #9 contains the PC */

#define PMT_STATUS_FREE       0
#define PMT_STATUS_CREATED    1
#define PMT_STATUS_PREPARING  2
#define PMT_STATUS_READY_JMP  3
#define PMT_STATUS_READY_INT  4

typedef struct {
	jmp_buf cpu_context;
	int status;
	void (*entry_point)();
} pmt_task_t;

typedef struct {
	int running;
	pmt_task_t tasks[PMT_MAX_TASKS];
	volatile int current_task;
	void * stack_adr;
	int stack_size;
} pmt_data_t;

class PreemptiveScheduler
{
private:
	pmt_data_t data;
	void fork_task(int num_task);
	void fork_all();
	bool has_tasks();
	void enable_timers();
	void disable_timers();
public:
	PreemptiveScheduler();
	void begin();
	void begin(int stack_size);
	bool create_task(void (*entry)(void));
	void run(void);
};



#endif /* PREEMPTIVESCHEDULER_H_ */
