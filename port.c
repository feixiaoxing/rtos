/*
     raw os - Copyright (C)  Lingjun Chen(jorya_txj).

    This file is part of raw os.

    raw os is free software; you can redistribute it it under the terms of the 
    GNU General Public License as published by the Free Software Foundation; 
    either version 3 of the License, or  (at your option) any later version.

    raw os is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; 
    without even the implied warranty of  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
    See the GNU General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program. if not, write email to jorya.txj@gmail.com
                                      ---

    A special exception to the LGPL can be applied should you wish to distribute
    a combined work that includes raw os, without being obliged to provide
    the source code for any proprietary components. See the file exception.txt
    for full details of how and when the exception can be applied.
*/


/* 	2012-4  Created by jorya_txj
  *	xxxxxx   please added here
  */

#include "os.h"

#include    <stdio.h>
#include    <string.h>
#include    <ctype.h>
#include    <stdlib.h>

#include  	<stdio.h>
#include  	<string.h>
#include  	<ctype.h>
#include  	<stdlib.h>
#include	<conio.h>
#include 	<stdarg.h>
#include	<windows.h>
#include	<mmsystem.h>
#include  	<assert.h> 




#define  WINDOWS_ASSERT(CON)    if (!(CON)) { \
									printf("If you see this error, please contact author txj, thanks\n");\
									assert(0);\
								}


static void simulated_interrupt_process( void );

void port_enter_critical();
void port_exit_critical();

/*-----------------------------------------------------------*/

/* The WIN32 simulator runs each task in a thread.  The context switching is
managed by the threads, so the task stack does not have to be managed directly,
although the task stack is still used to hold an xThreadState structure this is
the only thing it will ever hold.  The structure indirectly maps the task handle 
to a thread handle. */
typedef struct
{
	/* Handle of the thread that executes the task. */
	void *pvThread;
	HANDLE hInitEvent;
	HANDLE hSigEvent;
	void (*func)();
	void* param;
	u32 state;

} xThreadState;

#define CREATED 0x1
#define NOT_CREATED 0x2


/* An event used to inform the simulated interrupt processing thread (a high 
priority thread that simulated interrupt processing) that an interrupt is
pending. */
static void *timer_event = NULL;

/* Mutex used to protect all the simulated interrupt variables that are accessed 
by multiple threads. */
static void *cpu_global_interrupt_mask = NULL;

unsigned long port_interrupt_switch;

int port_switch_flag;

void vc_port_printf(char*   f,   ...)
{
	va_list   args;
	
	DISABLE_IE();
	
	va_start(args, f);
	vprintf(f,args);  
	va_end(args);
	
	ENABLE_IE();

}



void VCInit(void)
{
	
}

static void normal_entry(void* param) {

	Task* p_task = (Task*) param;

	xThreadState* pxThreadState = (xThreadState*) p_task-> stack_base;

	WaitForSingleObject(pxThreadState-> hSigEvent, INFINITE);

	SetEvent(pxThreadState-> hInitEvent);

	pxThreadState->func(pxThreadState-> param);

}

void  *port_stack_init(Task* p_task, u32  *p_stk_base, u32 stk_size,  void   *p_arg, void* p_func)
{
    
	xThreadState *pxThreadState = NULL;

	/* In this simulated case a stack is not initialised, but instead a thread
	is created that will execute the task being created.  The thread handles
	the context switching itself.  The xThreadState object is placed onto
	the stack that was created for the task - so the stack buffer is still
	used, just not in the conventional way.  It will not be used for anything
	other than holding this structure. */
	pxThreadState = ( xThreadState * ) (p_stk_base + stk_size - 2 - sizeof(xThreadState)/4 );

	/* Create the thread itself. */
	pxThreadState->pvThread = CreateThread( NULL, 0, ( LPTHREAD_START_ROUTINE ) normal_entry, p_task, CREATE_SUSPENDED, NULL );

	pxThreadState-> func = p_func;
	pxThreadState-> param = p_arg;
	
	pxThreadState-> hInitEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	pxThreadState-> hSigEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	pxThreadState-> state = CREATED;

	SetThreadAffinityMask( pxThreadState->pvThread, 0x1 );
	SetThreadPriority( pxThreadState->pvThread, THREAD_PRIORITY_IDLE );
	SetThreadPriorityBoost( pxThreadState->pvThread, TRUE );

	return pxThreadState;
	
}



static unsigned int vc_timer_value = 10;

void start_vc_timer(int tick_ms)
{

	vc_timer_value = tick_ms;
}



static volatile u8 done_timer_init;


static void CALLBACK os_timer_job(unsigned int a,unsigned int b,unsigned long c,unsigned long d,unsigned long e)
{	
	
	ReleaseSemaphore(timer_event, 1, 0);
				
}


static void start_internal_timer(int tick_ms) 
{	
	done_timer_init = 0;
	timeSetEvent(tick_ms, 0, os_timer_job, 0, TIME_PERIODIC);
	done_timer_init = 1;	
}


typedef  void  (*SIMULTED_INTERRUPT_TYPE)();


SIMULTED_INTERRUPT_TYPE simulated_zero_fun;
SIMULTED_INTERRUPT_TYPE simulated_interrupt_fun;

extern Task* current_task;
extern Task* sched_task;

void raw_start_first_task(void)
{
	void *pvHandle;
	xThreadState *pxThreadState;

	/*Max is assumed to 2*/
	timer_event = CreateSemaphore( NULL, 0, 2, NULL);
	
	cpu_global_interrupt_mask = CreateMutex( NULL, FALSE, NULL);
	
	if( ( cpu_global_interrupt_mask == NULL ) || ( timer_event == NULL ) ) {
	
		WINDOWS_ASSERT(0);
	}

	/* Set the priority of this thread such that it is above the priority of 
	the threads that run tasks.  This higher priority is required to ensure
	simulated interrupts take priority over tasks. */
	pvHandle = GetCurrentThread();
	
	if ( pvHandle == NULL ) {
	
		 WINDOWS_ASSERT(0);
	}
	

	
	if (SetThreadPriority( pvHandle, THREAD_PRIORITY_TIME_CRITICAL) == 0) {

		WINDOWS_ASSERT(0);
	}
	
	SetThreadPriorityBoost(pvHandle, TRUE);
	SetThreadAffinityMask( pvHandle, 0x01 );

	start_internal_timer(vc_timer_value);

	
	pxThreadState = ( xThreadState * ) *( ( unsigned long * ) current_task );

	/* Bump up the priority of the thread that is going to run, in the
	hope that this will asist in getting the Windows thread scheduler to
	behave as an embedded engineer might expect. */

	pxThreadState-> state = NOT_CREATED;
	ResumeThread( pxThreadState->pvThread );
	SignalObjectAndWait(pxThreadState-> hSigEvent, pxThreadState-> hInitEvent, INFINITE, FALSE);

	/* Handle all simulated interrupts - including yield requests and 
	simulated ticks. */
	simulated_interrupt_process();
	
}


void raw_int_switch()
{

	port_interrupt_switch = 1;
}

extern u32 idle_tick_start;
extern u32 g_irq;

static void simulated_interrupt_process( void )
{
	DWORD ret = 0xffffffff;
	BOOL end_ret = 0;
	
	xThreadState *pxThreadState;

	void* pvObjectList[2];

	pvObjectList[0] = timer_event;
	pvObjectList[1] = cpu_global_interrupt_mask;

	for(;;)
	{
		ret = WaitForMultipleObjects( sizeof(pvObjectList)/ sizeof(void*), pvObjectList, TRUE, INFINITE );
		
		if (ret == 0xffffffff) {
			
			WINDOWS_ASSERT(0);

		}

		g_irq ++;

		if (simulated_interrupt_fun) {
			simulated_interrupt_fun();

		}

		timer_isr_func();
		g_irq --;


		ReleaseMutex(cpu_global_interrupt_mask);

	}
}


void port_task_switch(void)
{
	/*global interrupt is disabled here so it is safe to change value here*/

	xThreadState* pxThreadState_cur = ( xThreadState * ) ( *( unsigned long *) current_task );

	xThreadState* pxThreadState_sched = ( xThreadState * ) ( *( unsigned long *) sched_task );

	current_task = sched_task;

	if(pxThreadState_sched-> state == CREATED) {

		pxThreadState_sched-> state = NOT_CREATED;

		ResumeThread(pxThreadState_sched-> pvThread);

		SignalObjectAndWait(pxThreadState_sched-> hSigEvent, pxThreadState_sched-> hInitEvent, INFINITE, FALSE);

	}else {

		SetEvent(pxThreadState_sched-> hSigEvent);
	}

	port_exit_critical();

	WaitForSingleObject(pxThreadState_cur-> hSigEvent, INFINITE);

	port_enter_critical();
}

extern u32 g_running;

void port_enter_critical()
{
	if (g_running) {
	
		/* The interrupt event mutex is held for the entire critical section,
		effectively disabling (simulated) interrupts. */
		WaitForSingleObject(cpu_global_interrupt_mask, INFINITE);
	}
}



void port_exit_critical()
{
	
	/* The interrupt event mutex should already be held by this thread as it was
	obtained on entry to the critical section. */

	if(!g_running) { 

		return; 
	}
	
	ReleaseMutex(cpu_global_interrupt_mask);

}

