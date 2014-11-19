/*
 * CooperativeMultitask.h
 *
 *  Created on: 16/11/2014
 *      Author: Bazoocaze
 */

#ifndef COOPERATIVE_SCHEDULER_H_
#define COOPERATIVE_SCHEDULER_H_

#include <setjmp.h>

/* Cooperative scheduler configuration */
#define CMT_MAX_TASKS           8
#define CMT_STACK_SIZE       2048
#define CMT_PREALLOC_STACK      1

/* constants */
#define CMT_STATUS_FREE     0
#define CMT_STATUS_CREATED  1
#define CMT_STATUS_READY    2
#define CMT_MAIN_TASK       0   /* MAIN task must be task 0 */
#define CMT_CPU_REG_SP      8   /* setjmp register #8 contains SP */
#define CMT_CPU_REG_PC      9   /* setjmp register #9 contains PC */


typedef struct {
	jmp_buf cpu_state;
	int status;
	void (*entry_point)();
} cmt_task_t;

typedef struct {
	cmt_task_t tasks[CMT_MAX_TASKS];
	int current_task;
	void * stack_adr;
	int stack_size;
	int running;
} cmt_data_t;

extern "C" void cmt_yeld();

class CooperativeScheduler {
private:
	cmt_data_t data;
public:
	CooperativeScheduler();
	void begin();
	void begin(int stack_size);
	bool create_task(void (*entry)(void));
	void run(void);
};

#endif /* COOPERATIVE_SCHEDULER_H_ */
