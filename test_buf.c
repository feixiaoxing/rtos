
#include "os.h"

static Task task1;
static Task task2;

static u8 task1_stack[1024];
static u8 task2_stack[1024];

static Msgbuf msg_buf;
static void* pool[10];

static void run_task1(void* param){

	void* p_msg;

	param = param;
	
	while(1) {
	
		p_msg = NULL;
	
		get_msg_buf(&msg_buf, &p_msg, 1);
		
		vc_port_printf("get pool\n");

	}
}

static void run_task2(void* param){

	param = param;
	
	while(1) {
	
		put_msg_buf(&msg_buf, "pool");
		
		vc_port_printf("send pool\n");

		yield();
	}
}

extern int global_test;

void test_buf() {

	if(!global_test) {

		global_test = 1;
		
		create_msg_buf(&msg_buf, &pool, 10);

		create_task(&task1, run_task1, NULL, task1_stack, 1024);
	
		create_task(&task2, run_task2, NULL, task2_stack, 1024);

	}

}



