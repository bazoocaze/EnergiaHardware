/*
 * CooperativeScheduler.cpp
 *
 *  Created on: 16/11/2014
 *      Author: Bazoocaze
 */
#include <Energia.h>

#include <setjmp.h>

#include "inc/hw_ints.h"
#include "inc/hw_timer.h"
#include "driverlib/rom.h"
#include "driverlib/sysctl.h"
#include "driverlib/timer.h"

#include "Multitask.h"
#include "utility/CooperativeScheduler.h"

cmt_data_t * cmt_data = NULL;

/*
 * Verifies if this is an Exception Handler context.
 * Returns true if this is an exception context, of false otherwise.
 */
unsigned int cmt_is_eh() {
//	unsigned int ret;
//	/* the nine low order bits of xPSR on Cortext-M4 contains the exception number */
//	asm ("MRS %[result], xPSR\n" : [result] "=r" (ret));
//	return ret & 0x01FF;
	return HWREG(NVIC_INT_CTRL) & NVIC_INT_CTRL_VEC_ACT_M;
}

/*
 * Yelds the execution of the current task for the Cooperative task scheduler.
 */
void cmt_yeld() {
	int prev;
	int next;
	if (!cmt_data || !cmt_data->running)
		return;
	/* refuse to yeld if it's an exception context */
	if (cmt_is_eh())
		return;
	prev = cmt_data->current_task;
	next = prev;
	for (int n = 0; n < CMT_MAX_TASKS; n++) {
		next = (next + 1) % CMT_MAX_TASKS;
		if (cmt_data->tasks[next].status == CMT_STATUS_READY)
			break;
	}
	cmt_data->current_task = next;
	if (cmt_data->tasks[next].status != CMT_STATUS_READY) {
		/* no tasks left - return to calling task (MAIN)*/
		longjmp(cmt_data->tasks[CMT_MAIN_TASK].cpu_state, 1);
	}
	if (setjmp(cmt_data->tasks[prev].cpu_state) != 0)
		return;
	longjmp(cmt_data->tasks[next].cpu_state, 1);
}

/*
 * Task entry point.
 * Executes the task entry sub-routine.
 * On exit, finalizes the current task and yeld.
 */
void cmt_task_entry() {
	int n = cmt_data->current_task;
	cmt_data->tasks[n].entry_point();
	/* the task terminated */
	cmt_data->tasks[n].status = CMT_STATUS_FREE;
	cmt_yeld();
}

/*
 * Create a new instance of the Cooperative task scheduler.
 */
CooperativeScheduler::CooperativeScheduler() {
	data.stack_adr = NULL;
	data.running = 0;
}

/*
 * Initialize the Cooperative task scheduler with the informed stack size.
 */
void CooperativeScheduler::begin(int stack_size) {
	cmt_data = &data;
	data.stack_size = stack_size;
	data.stack_adr = NULL;
	data.running = 0;
	for (int i = 0; i < CMT_MAX_TASKS; i++)
		data.tasks[i].status = CMT_STATUS_FREE;
}

/*
 * Initilize the Cooperative task scheduler with the default settings.
 */
void CooperativeScheduler::begin() {
	begin(CMT_STACK_SIZE);
}

/*
 * Create a task with 'entry' entry point.
 * Returns true/false if the task was created.
 * If the constant CMT_PREALLOC_STACK was 1
 * a task can be created inside running tasks.
 */
bool CooperativeScheduler::create_task(void (*entry)(void)) {
	for (int i = 0; i < CMT_MAX_TASKS; i++) {
		if (i == CMT_MAIN_TASK || data.tasks[i].status != CMT_STATUS_FREE)
			continue;
		/* initilize the task structure */
		data.tasks[i].cpu_state[CMT_CPU_REG_PC] = (int) &cmt_task_entry;
		data.tasks[i].entry_point = entry;
		if (data.running) {
#if CMT_PREALLOC_STACK
			data.tasks[i].cpu_state[CMT_CPU_REG_SP] = (int) (data.stack_adr + (data.stack_size * i));
			data.tasks[i].status = CMT_STATUS_READY;
#else
			/* running tasks and no stack pre-allocated */
			return false;
#endif
		} else {
			/* if not running, stack is initilized later in cmt_run_tasks() */
			data.tasks[i].status = CMT_STATUS_CREATED;
		}
		return true;
	}
	return false;
}

/*
 * Alocates stack space and then run the scheduled tasks.
 * Blocks until the last task exits.
 */
void CooperativeScheduler::run() {
	int num_tasks = 0;
	int size;

	if (data.running)
		return;

#if CMT_PREALLOC_STACK
	num_tasks = CMT_MAX_TASKS - 1;
#else
	/* determine number os tasks to run */
	for (int i = 0; i < CMT_MAX_TASKS; i++)
		if (data.tasks[i].status != CMT_STATUS_FREE)
			num_tasks++;
#endif

	if (data.stack_size < 256)
		data.stack_size = CMT_STACK_SIZE;

	data.stack_size = data.stack_size & 0xFFFFFFFC;
	size = num_tasks * data.stack_size / sizeof(int);

	/* create the stack memory for the tasks */
	unsigned int temp_stack[size];

	/* alloc stack for each task */
	data.stack_adr = (void *) temp_stack;
	for (int i = 0; i < CMT_MAX_TASKS; i++) {
		if (i == CMT_MAIN_TASK)
			continue;
		if (data.tasks[i].status == CMT_STATUS_CREATED) {
			data.tasks[i].cpu_state[CMT_CPU_REG_SP] = (int) (data.stack_adr + (data.stack_size * i));
			data.tasks[i].status = CMT_STATUS_READY;
		}
	}

	/* set current task MAIN and execute first task */
	data.current_task = CMT_MAIN_TASK;

	data.running = true;
	cmt_yeld();
	data.running = false;
}
