/******************************************
 * CooperativeScheduler.cpp
 * Simply preemptive scheduler library.
 *  Created on: 17/11/2014
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

#ifdef ENERGIA

#include "Energia.h"
#include "inc/hw_ints.h"
#include "inc/hw_timer.h"
#include "driverlib/rom.h"
#include "driverlib/sysctl.h"
#include "driverlib/timer.h"
#include "utility/PreemptiveScheduler.h"

#else

#include "PreemptiveScheduler.h"

#error "Unsupported arch for the Preemptive Scheduler"

#endif

#define INT_PROTECT_INIT(oldLevel)  int oldLevel
#define INT_PROTECT(oldLevel)       do{oldLevel=pmt_disable_int();}while(0)
#define INT_UNPROTECT(oldLevel)     do{if(!oldLevel)pmt_enable_int();}while(0)

pmt_data_t * pmt_data = 0;

/*
 * Disable interrupts.
 * Return true if interrupts are already disabled, or false otherwise.
 */
int pmt_disable_int() {
#ifdef ENERGIA
	return ROM_IntMasterDisable();
#else
#error "Unsupported arch for the preemptive scheduler"
#endif
}

/*
 * Enable interrupts.
 */
void pmt_enable_int() {
#ifdef ENERGIA
	ROM_IntMasterEnable();
#else
#error "Unsupported arch for the preemptive scheduler"
#endif
}

void pmt_switch_context() {
	if (!pmt_data || !pmt_data->running)
		return;
	int prev = pmt_data->current_task;
	int next = prev;
	pmt_task_t * prevTask = &pmt_data->tasks[prev];
	pmt_task_t * nextTask;
	for (int n = 0; n < PMT_MAX_TASKS; n++) {
		next = ((next + 1) % PMT_MAX_TASKS);
		nextTask = &pmt_data->tasks[next];
		if (nextTask->status == PMT_STATUS_READY_INT)
			break;
	}
	pmt_data->current_task = next;
	int prevStatus = prevTask->status;
	if (prevStatus == PMT_STATUS_READY_JMP || prevStatus == PMT_STATUS_PREPARING)
		prevTask->status = PMT_STATUS_READY_INT;
	if (setjmp(prevTask->cpu_context) != 0)
		return;
	if (prev == next && nextTask->status != PMT_STATUS_READY_INT) {
		/* no tasks left - return to MAIN task */
		pmt_data->current_task = PMT_MAIN_TASK;
		nextTask = nextTask = &pmt_data->tasks[PMT_MAIN_TASK];
	}
	longjmp(nextTask->cpu_context, 1);
}

#if defined(__ARM_ARCH_7EM__) && defined(ENERGIA)
extern "C" void ToneIntHandler(void) {
	ROM_TimerIntClear(TIMER4_BASE, TIMER_TIMA_TIMEOUT);
	pmt_switch_context();
}
#endif

void pmt_task_entry() {
	int n = pmt_data->current_task;
	/* execute task entry point */
	pmt_enable_int();
	pmt_data->tasks[n].entry_point();
	/* task terminated */
	pmt_data->tasks[n].status = PMT_STATUS_FREE;
	pmt_enable_int();
	while (true) {
	}
}

PreemptiveScheduler::PreemptiveScheduler() {
	pmt_data = NULL;
}

void PreemptiveScheduler::begin() {
	begin(PMT_STACK_SIZE);
}

void PreemptiveScheduler::begin(int stack_size) {
	data.running = false;
	data.current_task = PMT_MAIN_TASK;
	data.stack_adr = NULL;
	data.stack_size = stack_size;
	for (int i = 0; i < PMT_MAX_TASKS; i++) {
		data.tasks[i].status = PMT_STATUS_FREE;
		data.tasks[i].entry_point = NULL;
		memset(data.tasks[i].cpu_context, 0, sizeof(data.tasks[i].cpu_context));
	}
	data.tasks[PMT_MAIN_TASK].status = PMT_STATUS_CREATED;
	pmt_data = &data;
}

bool PreemptiveScheduler::create_task(void (*entry)(void)) {
	INT_PROTECT_INIT(oldLevel);
	bool ret = false;
	INT_PROTECT(oldLevel);
	for (int i = 0; i < PMT_MAX_TASKS; i++) {
		if (i == PMT_MAIN_TASK || data.tasks[i].status != PMT_STATUS_FREE)
			continue;

#if PMT_PREALLOC_STACK
		/* initialize CPU context to fork task */
		setjmp(data.tasks[i].cpu_context);
		data.tasks[i].cpu_context[PMT_CPU_REG_SP] = (int) (data.stack_adr + (data.stack_size * i));
		data.tasks[i].cpu_context[PMT_CPU_REG_PC] = (int) &pmt_task_entry;
#else
		if (data.running)
			break;
#endif

		/* initilize the task structure */
		data.tasks[i].status = PMT_STATUS_CREATED;
		data.tasks[i].entry_point = entry;
		/* if not running, stack is initilized later in run_tasks() */
		ret = true;

#if PMT_PREALLOC_STACK
		if (data.running)
		{
			data.tasks[i].status = PMT_STATUS_READY_JMP;
			fork_task(i);
		}
#endif

		break;
	}
	INT_UNPROTECT(oldLevel);
	return ret;
}

void PreemptiveScheduler::fork_task(int num_task) {
	int numThisTask = data.current_task;
	pmt_task_t * thisTask = &data.tasks[numThisTask];
	pmt_task_t * forkTask = &data.tasks[num_task];
	if (forkTask->status == PMT_STATUS_READY_JMP) {
		/* The next while is critical for task sincronization.
		 * Do not make modifications if you do not know what are you doing.
		 *  */
		pmt_disable_int();
		if (forkTask->status != PMT_STATUS_READY_JMP)
			return;
		thisTask->status = PMT_STATUS_PREPARING;
		while (true) {
			pmt_enable_int();
			pmt_disable_int();
			if (thisTask->status == PMT_STATUS_READY_INT) {
				if (forkTask->status == PMT_STATUS_READY_JMP) {
					data.current_task = num_task;
					longjmp(forkTask->cpu_context, 1);
				}
				break;
			}
		}
	}
}

void PreemptiveScheduler::fork_all() {
	int numThisTask = data.current_task;
	for (int n = 0; n < PMT_MAX_TASKS; n++) {
		if (n == numThisTask)
			continue;
		if (data.tasks[n].status == PMT_STATUS_READY_JMP) {
			fork_task(n);
			pmt_enable_int();
		}
	}
}

#ifdef __ARM_ARCH_7EM__
void PreemptiveScheduler::enable_timers() {
	ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER4);
	ROM_TimerConfigure(TIMER4_BASE, TIMER_CFG_PERIODIC);
	ROM_TimerLoadSet(TIMER4_BASE, TIMER_A, PMT_SWITCH_FREQ);
	ROM_TimerEnable(TIMER4_BASE, TIMER_A);
	ROM_IntEnable(INT_TIMER4A);
	ROM_TimerIntEnable(TIMER4_BASE, TIMER_TIMA_TIMEOUT);
	ROM_IntPrioritySet(INT_TIMER4A, 0xFF);
}

void PreemptiveScheduler::disable_timers() {
	ROM_TimerDisable(TIMER4_BASE, TIMER_A);
	ROM_TimerIntDisable(TIMER4_BASE, TIMER_TIMA_TIMEOUT);
	ROM_IntDisable(INT_TIMER4A);
	ROM_TimerIntClear(TIMER4_BASE, TIMER_TIMA_TIMEOUT);
	ROM_SysCtlPeripheralDisable(SYSCTL_PERIPH_TIMER4);
}
#else
#error "You must implement the enable_timers() and disable_timers() routines for you architecture."
#endif

bool PreemptiveScheduler::has_tasks() {
	pmt_disable_int();
	for (int i = 0; i < PMT_MAX_TASKS; i++)
		if (pmt_data->tasks[i].status >= PMT_STATUS_PREPARING) {
			pmt_enable_int();
			return true;
		}
	pmt_enable_int();
	return false;
}

void PreemptiveScheduler::run() {
	pmt_disable_int();

	/* determine number os tasks to run */
	int num_tasks = 0;

#if PMT_PREALLOC_STACK
	num_tasks = PMT_MAX_TASKS - 1;
#else
	for (int i = 0; i < PMT_MAX_TASKS; i++)
		if (data.tasks[i].status != PMT_STATUS_FREE)
			num_tasks++;
#endif

	int size = num_tasks * data.stack_size / sizeof(int);

	unsigned int temp_stack[size];

	/* aloc stack for the tasks */
	data.stack_adr = (void *) temp_stack;
	for (int i = 0; i < PMT_MAX_TASKS; i++) {
		if (data.tasks[i].status == PMT_STATUS_CREATED && i != PMT_MAIN_TASK) {
			setjmp(data.tasks[i].cpu_context);
			data.tasks[i].cpu_context[PMT_CPU_REG_SP] = (int) (data.stack_adr + (data.stack_size * i));
			data.tasks[i].cpu_context[PMT_CPU_REG_PC] = (int) &pmt_task_entry;
			data.tasks[i].status = PMT_STATUS_READY_JMP;
		}
	}

	/* set current task MAIN and execute first task */
	data.current_task = PMT_MAIN_TASK;

	pmt_data = &data;

	enable_timers();

	data.running = true;

	pmt_enable_int();

	while (true) {
		fork_all();
		data.tasks[PMT_MAIN_TASK].status = PMT_STATUS_FREE; /* pauses the main task */
		if (!has_tasks())
			break;
	}

	data.running = false;

	disable_timers();

	pmt_data = NULL;
}

