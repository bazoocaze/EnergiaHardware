/******************************************
 * CooperativeScheduler.cpp
 * Simply cooperative scheduler library.
 *  Created on: 16/11/2014
 *      Author: Bazoocaze
 ******************************************
 Copyright (c) 2014 Jose Ferreira

 This library is free software: you can redistribute it and/or modify
 it under the terms of the GNU Lesser General Public License as
 published by the Free Software Foundation, either version 3 of the
 License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library.
 If not, see <http://www.gnu.org/licenses/>.
 */

#include <setjmp.h>

#ifdef ENERGIA

#include <Energia.h>
#include "inc/hw_ints.h"
#include "inc/hw_timer.h"
#include "driverlib/rom.h"
#include "driverlib/sysctl.h"
#include "driverlib/timer.h"
#include "utility/CooperativeScheduler.h"

#else

#include "CooperativeScheduler.h"

#endif

cmt_data_t * cmt_data = NULL;

/*
 * Verifies if this is an Exception Handler context.
 * Returns true if this is an exception context, of false otherwise.
 */
unsigned int cmt_is_eh() {
#ifdef __ARM_ARCH_7EM__
	return HWREG(NVIC_INT_CTRL) & NVIC_INT_CTRL_VEC_ACT_M;
#elif defined(__i386__)
	return false;
#else
#error "Unsupported ARCH for Cooperative Scheduler"
#endif
}

/*
 * Verifies if interrupts are disable.
 * Returns true if interrupts are disable, and false otherwise.
 */
unsigned int cmt_is_int_disabled() {
#ifdef __ARM_ARCH_7EM__
	unsigned int ret;
	asm ("MRS %[result], PRIMASK\n" : [result] "=r" (ret));
	return (ret & 0x01);
#elif defined(__i386__)
	return false;
#else
#error "Unsupported ARCH for Cooperative Scheduler"
#endif
}

void cmt_disable_int() {
#ifdef ENERGIA
	ROM_IntMasterDisable();
#elif defined(__i386__)
	return;
#else
#error "Unsupported ARCH for Cooperative Scheduler"
#endif
}

void cmt_enable_int() {
#ifdef ENERGIA
	ROM_IntMasterEnable();
#elif defined(__i386__)
	return;
#else
#error "Unsupported ARCH for Cooperative Scheduler"
#endif
}

/*
 * Yelds the execution of the current task for the Cooperative task scheduler.
 */
void cmt_yeld() {
	int prev;
	int next;
	if (!cmt_data || !cmt_data->running)
		return;
	/* refuse to yeld if it's an exception context or interrupts disabled */
	if (cmt_is_eh() || cmt_is_int_disabled())
		return;
	cmt_disable_int();
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
	if (setjmp(cmt_data->tasks[prev].cpu_state) == 0)
		longjmp(cmt_data->tasks[next].cpu_state, 1);
	cmt_enable_int();
	return;
}

/*
 * Task entry point.
 * Executes the task entry sub-routine.
 * On exit, finalizes the current task and yeld.
 */
void cmt_task_entry() {
	int n = cmt_data->current_task;
	cmt_enable_int();
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
		setjmp(data.tasks[i].cpu_state);
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

	data.running = 1;
	cmt_yeld();
	data.running = 0;
}
