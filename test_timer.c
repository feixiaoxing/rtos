
#include "os.h"

static Task task;;

static u8 stack[1024];

static Timer timer;

static void timer_func(void* param){

	param = param;
	
	vc_port_printf("timer\t");
	
	activate_timer(&timer);
}

static void run_task(void* param){

	param = param;
	
	create_timer(&timer, 50, timer_func, NULL);
	
	activate_timer(&timer);
	
	while(1) {
		yield();
	}
}

extern int global_test;

void test_timer() {

	if(!global_test) {

		global_test = 1;

		create_task(&task, run_task, NULL, stack, 1024);
	}

}



