/*
 * PreemptiveScheduler.cpp
 *
 *  Created on: 17/11/2014
 *      Author: Bazoocaze
 */

#include "Energia.h"
#include "PreemptiveScheduler.h"

#include "inc/hw_ints.h"
#include "inc/hw_timer.h"
#include "driverlib/rom.h"
#include "driverlib/sysctl.h"
#include "driverlib/timer.h"

#define INT_PROTECT_INIT(oldLevel)  int oldLevel
#define INT_PROTECT(oldLevel)       do{oldLevel=ROM_IntMasterDisable();}while(0)
#define INT_UNPROTECT(oldLevel)     do{if(!oldLevel)ROM_IntMasterEnable();}while(0)

pmt_data_t * pmt_data = 0;

void pmt_switch_context() {
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
		longjmp(pmt_data->tasks[PMT_MAIN_TASK].cpu_context, 1);
		return;
	}
	longjmp(nextTask->cpu_context, 1);
}

extern "C" void ToneIntHandler(void) {
	ROM_TimerIntClear(TIMER4_BASE, TIMER_TIMA_TIMEOUT);
	if (!pmt_data || !pmt_data->running)
		return;
	pmt_switch_context();
}

void pmt_task_entry() {
	int n = pmt_data->current_task;
	/* execute task entry point */
	ROM_IntMasterEnable();
	pmt_data->tasks[n].entry_point();
	/* task terminated */
	pmt_data->tasks[n].status = PMT_STATUS_FREE;
	ROM_IntMasterEnable();
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
		data.tasks[i].cpu_context[PMT_CPU_REG_SP] = (int) (data.stack_adr + (data.stack_size * i));
#else
		if (data.running)
			break;
#endif
		/* initilize the task structure */
		data.tasks[i].status = PMT_STATUS_CREATED;
		data.tasks[i].cpu_context[PMT_CPU_REG_PC] = (int) &pmt_task_entry;
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
		ROM_IntMasterDisable();
		if (forkTask->status != PMT_STATUS_READY_JMP)
			return;
		thisTask->status = PMT_STATUS_PREPARING;
		while (true) {
			ROM_IntMasterEnable();
			ROM_IntMasterDisable();
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
			ROM_IntMasterEnable();
		}
	}
}

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

bool PreemptiveScheduler::has_tasks() {
	ROM_IntMasterDisable();
	for (int i = 0; i < PMT_MAX_TASKS; i++)
		if (pmt_data->tasks[i].status >= PMT_STATUS_PREPARING) {
			ROM_IntMasterEnable();
			return true;
		}
	ROM_IntMasterEnable();
	return false;
}

void PreemptiveScheduler::run() {
	ROM_IntMasterDisable();

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
			data.tasks[i].cpu_context[PMT_CPU_REG_SP] = (int) (data.stack_adr + (data.stack_size * i));
			data.tasks[i].status = PMT_STATUS_READY_JMP;
		}
	}

	/* set current task MAIN and execute first task */
	data.current_task = PMT_MAIN_TASK;

	pmt_data = &data;

	enable_timers();

	data.running = true;

	ROM_IntMasterEnable();

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

