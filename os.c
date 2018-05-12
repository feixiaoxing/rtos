
#include "port.h"
#include "os.h"

// function declaration

static Task* get_rdy_task();
static void timer_running_func(void* param);
static void idle_running_func(void* param);

// list function

static void list_init(ListNode* node) {

	node->prev = node;
	node->next = node;
}

static STATUS is_list_empty(ListNode* node) {

	return node->next == node;
}

static void list_insert(ListNode* head, ListNode* node) {

	node->prev = head->prev;
	node->next = head;

	head->prev->next = node;
	head->prev = node;
}

static  void list_delete(ListNode* node) {

	node->prev->next = node->next;
	node->next->prev = node->prev;
}

// global variable

#undef EXTERN
#define EXTERN

EXTERN u32 g_irq;

static u32 g_sched_lock;
EXTERN u32 g_running;

EXTERN Task* current_task;
EXTERN Task* sched_task;

static ListNode g_run_queue;

static u64 g_tick;
static Sem timer_sem;
static ListNode g_timer;
static Task timer_task;
static u8 timer_stack[1024];

static u64 g_idle;
static Task idle_task;
static u8 idle_stack[1024];

// os init

void os_init() {

	// about irq

	g_irq = 0;

	// about schedule variable

	g_sched_lock = 0;
	g_running = 0;
	current_task = NULL;
	sched_task = NULL;

	// about ready queue

	list_init(&g_run_queue);

	// about timer task

	g_tick = 0;
	list_init(&g_timer);
	create_sem(&timer_sem, 0);
	create_task(&timer_task, timer_running_func, NULL, timer_stack, 1024);

	// about idle task

	g_idle = 0;
	create_task(&idle_task, idle_running_func, NULL, idle_stack, 1024);

}

// os start

void os_start() {

	if(!g_running) {

		g_running = 1;
		current_task = get_rdy_task();
		current_task-> state = RUNNING;

		START_FIRST_TASK();
	}
}


// os lock and unlock function

void sched_lock() {

	DISABLE_IE();
	g_sched_lock ++;
	ENABLE_IE();

}

void sched_unlock() {

	DISABLE_IE();
	g_sched_lock --;
	ENABLE_IE();
}

static STATUS is_sched_lock() {

	STATUS result;

	DISABLE_IE();
	result = g_sched_lock > 0;
	ENABLE_IE();

	return result;
}


// about rdy queue

static void add_to_rdy_queue(Task* p_task) {

	list_insert(&g_run_queue, &p_task-> rdy);
}

static void remove_from_rdy_queue(Task* p_task) {

	list_delete(&p_task-> rdy);
}

static Task* get_rdy_task(){

	return get_list_entry(g_run_queue.next, Task, rdy);
}

// about blk queue

static void add_to_blk_queue(ListNode* head, Task* p_task){

	list_insert(head, &p_task-> blk);
}


static void remove_from_blk_queue(Task* p_task) {

	list_delete(&p_task-> blk);
}


// dispatch function

static STATUS dispatch() {

	if(is_in_irq()) {

		return IN_IRQ;
	}

	if(is_sched_lock()) {

		return OS_SCHED_LOCKED;
	}

	sched_task = get_rdy_task();
	if(sched_task != current_task) {

		sched_task-> state = RUNNING;

		CONTEXT_SWITCH();
	}

	return SUCCESS;

}

// yield function 

void yield() {

	if(is_in_irq()) {

		return;
	}

	if(is_sched_lock()) {

		return;
	}

	DISABLE_IE();
	
	remove_from_rdy_queue(current_task);
	add_to_rdy_queue(current_task);

	sched_task = get_rdy_task();
	if(sched_task != current_task) {

		current_task-> state = READY;
		sched_task-> state = RUNNING;

		CONTEXT_SWITCH();
	}

	ENABLE_IE();
}


// create task
 
STATUS create_task(Task* p_task, void* entry, void* param, void* p_stack, u32 stack_size) {

	if(NULL == p_task) {

		return PARAM_ERROR;
	}

	if(NULL == entry) {

		return PARAM_ERROR;
	}

	if(NULL == p_stack) {

		return PARAM_ERROR;
	}

	if(0 == stack_size) {

		return PARAM_ERROR;
	}

	p_task-> entry = entry;
	p_task-> param = param;
	p_task-> stack_size = stack_size;

	p_task-> msg = NULL;

	p_task-> buf_msg = NULL;

	p_task-> event_opt = 0;
	p_task-> event_val = 0;
	p_task-> event_data = 0;

	list_init(&p_task-> blk);
	list_init(&p_task-> rdy);

	p_task-> stack_base = (void*) INIT_STACK_DATA(p_task, p_stack, stack_size, entry, param);

	DISABLE_IE();
	add_to_rdy_queue(p_task);

	p_task-> state = READY;

	ENABLE_IE();

	return SUCCESS;

}

// shutdown task

STATUS shutdown_task(Task* p_task){

	if(NULL == p_task){

		return PARAM_ERROR;
	}

	DISABLE_IE();

	if(current_task == p_task){

		ENABLE_IE();

		return SELF_KILL_FORBID;
	}

	if (READY == p_task-> state) {

		list_delete(&p_task-> rdy);

	}else if(BLOCKED == p_task-> state){

		list_delete(&p_task-> blk);
	}

	p_task-> state = DIE;

	ENABLE_IE();

	return SUCCESS;

}

// resume task

STATUS resume_task(Task* p_task){

	if(NULL == p_task) {

		return PARAM_ERROR;
	}

	DISABLE_IE();

	if(READY == p_task-> state || BLOCKED == p_task-> state){

		ENABLE_IE();

		return SUCCESS;
	}

	p_task-> state = READY;
	add_to_rdy_queue(p_task);

	ENABLE_IE();

	return SUCCESS;
}

// create semaphore

STATUS create_sem(Sem* p_sem, u32 count) {

	if(NULL == p_sem) {

		return PARAM_ERROR;
	}

	p_sem-> blk_type = SEM_TYPE;
	list_init(&p_sem-> head);
	p_sem-> count = count;

	return SUCCESS;

}

// get semaphore

STATUS get_sem(Sem* p_sem, u8 wait) {

	STATUS result;

	if(is_in_irq()) {

		return IN_IRQ;
	}

	if(NULL == p_sem) {

		return PARAM_ERROR;
	}

	if(SEM_TYPE != p_sem-> blk_type) {

		return WRONG_BLOCK_TYPE;
	}

	DISABLE_IE();
	if(p_sem-> count) {

		p_sem-> count -= 1;
		ENABLE_IE();

		return SUCCESS;
	}

	if(is_sched_lock()) {

		ENABLE_IE();

		return OS_SCHED_LOCKED;
	}	

	if(!wait) {

		ENABLE_IE();

		return NOT_WAIT;
	}
	
	remove_from_rdy_queue(current_task);
	add_to_blk_queue(&p_sem-> head, current_task);

	current_task-> state = BLOCKED;
	
	result = dispatch();

	ENABLE_IE();

	return result;
}

// put semaphore

STATUS put_sem(Sem* p_sem) {

	Task* p_task;

	if(NULL == p_sem) {

		return PARAM_ERROR;
	}

	if(SEM_TYPE != p_sem-> blk_type) {

		return WRONG_BLOCK_TYPE;
	}
	
	DISABLE_IE();

	if(is_list_empty(&p_sem->head)) {

		p_sem->count += 1;
	
		ENABLE_IE();

		return SUCCESS;
	}

	p_task = get_list_entry(p_sem->head.next, Task, blk);

	remove_from_blk_queue(p_task);
	add_to_rdy_queue(p_task);

	p_task-> state = READY;
	
	ENABLE_IE();

	return SUCCESS;
}

// create mutex

STATUS create_mutex(Mutex* p_mutex) {

	if(NULL == p_mutex) {

		return PARAM_ERROR;
	}

	p_mutex-> blk_type = MUT_TYPE;
	list_init(&p_mutex-> head);
	p_mutex-> count = 1;
	p_mutex-> owner = NULL;

	return SUCCESS;
}

// get mutex

STATUS get_mutex(Mutex* p_mutex, u8 wait) {

	STATUS result;

	if(is_in_irq()) {

		return IN_IRQ;
	}

	if(NULL == p_mutex) {

		return PARAM_ERROR;
	}

	if(MUT_TYPE != p_mutex-> blk_type) {

		return WRONG_BLOCK_TYPE;
	}

	DISABLE_IE();

	if(p_mutex-> count) {

		p_mutex-> count = 0;
		p_mutex-> owner = current_task;

		ENABLE_IE();

		return SUCCESS;
	}

	if(is_sched_lock()) {

		ENABLE_IE();

		return OS_SCHED_LOCKED;
	} 

	if(!wait) {

		ENABLE_IE();

		return NOT_WAIT;
	}

	remove_from_rdy_queue(current_task);
	add_to_blk_queue(&p_mutex-> head, current_task);

	current_task-> state = BLOCKED;
	
	result = dispatch();

	ENABLE_IE();

	return result;
}

// put mutex

STATUS put_mutex(Mutex* p_mutex) {

	Task* p_task;

	if(NULL == p_mutex) {

		return PARAM_ERROR;
	}

	if(MUT_TYPE != p_mutex-> blk_type) {

		return WRONG_BLOCK_TYPE;
	}

	DISABLE_IE();

	if(current_task != p_mutex-> owner) {

		ENABLE_IE();

		return NOT_MUTEX_OWNER;
	}

	if(is_list_empty(&p_mutex->head)) {

		p_mutex-> count = 1;
		p_mutex-> owner = NULL;

		ENABLE_IE();

		return SUCCESS;
	}

	p_task = get_list_entry(p_mutex->head.next, Task, blk);

	remove_from_blk_queue(p_task);
	add_to_rdy_queue(p_task);

	p_mutex-> owner = p_task;

	p_task-> state = READY;

	ENABLE_IE();

	return SUCCESS;	
}

// create mail

STATUS create_mail(Mailbox* p_box, void* msg) {

	if(NULL == p_box) {

		return PARAM_ERROR;
	}

	p_box-> blk_type = MAIL_TYPE;
	list_init(&p_box-> head);
	p_box-> msg = msg;

	return SUCCESS;
}


// get mail

STATUS get_mail(Mailbox* p_box, void** pp_msg, u8 wait) {

	STATUS result;

	if(is_in_irq()) {

		return IN_IRQ;
	}

	if(NULL == p_box) {

		return PARAM_ERROR;
	}

	if(NULL == pp_msg) {

		return PARAM_ERROR;
	}

	if(MAIL_TYPE != p_box-> blk_type) {

		return WRONG_BLOCK_TYPE;
	}

	DISABLE_IE();
	if(p_box-> msg) {

		*pp_msg = p_box-> msg;
		p_box-> msg = NULL;
		ENABLE_IE();

		return SUCCESS;
	}

	if(is_sched_lock()) {

		ENABLE_IE();

		return OS_SCHED_LOCKED;
	}

	if(!wait) {

		ENABLE_IE();

		return NOT_WAIT;
	}

	remove_from_rdy_queue(current_task);
	add_to_blk_queue(&p_box-> head, current_task);

	current_task-> state = BLOCKED;
	result = dispatch();
	ENABLE_IE();

	if(SUCCESS != result) {

		return result;
	}

	DISABLE_IE();

	*pp_msg = current_task-> msg;

	ENABLE_IE();

	return SUCCESS;
}


// put mail

STATUS put_mail(Mailbox* p_box, void* msg) {

	Task* p_task;

	if(NULL == p_box) {

		return PARAM_ERROR;
	}

	if(NULL == msg) {

		return PARAM_ERROR;
	}

	if(MAIL_TYPE != p_box-> blk_type) {

		return WRONG_BLOCK_TYPE;
	}

	DISABLE_IE();

	if(p_box-> msg) {

		ENABLE_IE();

		return MSG_EXIST;
	}

	if(is_list_empty(&p_box->head)){

		p_box->msg = msg;
		ENABLE_IE();

		return SUCCESS;
	}

	p_task = get_list_entry(p_box->head.next, Task, blk);

	remove_from_blk_queue(p_task);
	add_to_rdy_queue(p_task);

	p_task-> msg = msg;

	p_task-> state = READY;

	ENABLE_IE();

	return SUCCESS;
}

// create message buffer

STATUS create_msg_buf(Msgbuf* p_msg_buf, void** pp_msg, u32 size) {

	if(NULL == p_msg_buf) {

		return PARAM_ERROR;
	}

	if(NULL == pp_msg) {

		return PARAM_ERROR;
	}

	if(!size) {

		return PARAM_ERROR;
	}

	p_msg_buf-> blk_type = BUF_TYPE;
	list_init(&p_msg_buf->head);
	p_msg_buf-> pp_msg = pp_msg;
	p_msg_buf-> size = size;

	p_msg_buf-> count = 0;
	p_msg_buf-> start = 0;
	p_msg_buf-> end   = 0;

	return SUCCESS;
}

// get message from buffer

STATUS get_msg_buf(Msgbuf* p_msg_buf, void** pp_msg, u8 wait) {

	STATUS result;

	if(is_in_irq()) {

		return IN_IRQ;
	}

	if(NULL == p_msg_buf) {

		return PARAM_ERROR;
	}

	if(NULL == pp_msg){

		return PARAM_ERROR;
	}

	if(BUF_TYPE != p_msg_buf-> blk_type){

		return WRONG_BLOCK_TYPE;
	}

	DISABLE_IE();

	if(p_msg_buf-> count) {

		*pp_msg = p_msg_buf->pp_msg[p_msg_buf-> end];

		p_msg_buf-> end ++;

		if(p_msg_buf-> end == p_msg_buf-> size){

			p_msg_buf-> end = 0;
		}

		p_msg_buf-> count --;

		ENABLE_IE();

		return SUCCESS;
	}

	if(is_sched_lock()) {

		ENABLE_IE();

		return OS_SCHED_LOCKED;
	}

	if(!wait) {

		ENABLE_IE();

		return NOT_WAIT;
	}

	remove_from_rdy_queue(current_task);
	add_to_blk_queue(&p_msg_buf-> head, current_task);

	current_task-> state = BLOCKED;
	result = dispatch();
	ENABLE_IE();

	if(SUCCESS != result) {

		return result;
	}

	DISABLE_IE();
	*pp_msg = current_task-> buf_msg;
	ENABLE_IE();

	return SUCCESS;
}

// put message buffer

STATUS put_msg_buf(Msgbuf* p_msg_buf, void* p_msg){

	Task* p_task;

	if(NULL == p_msg_buf){

		return PARAM_ERROR;
	}

	if(NULL == p_msg) {

		return PARAM_ERROR;
	}

	if(BUF_TYPE != p_msg_buf-> blk_type){

		return WRONG_BLOCK_TYPE;
	}

	DISABLE_IE();

	if(is_list_empty(&p_msg_buf-> head)){

		if(p_msg_buf-> size == p_msg_buf-> count){

			ENABLE_IE();

			return MSG_FULL;
		}

		p_msg_buf-> pp_msg[p_msg_buf-> start] = p_msg;

		p_msg_buf-> start ++;

		if(p_msg_buf-> start == p_msg_buf-> size) {

			p_msg_buf-> start = 0;
		}

		p_msg_buf-> count ++;

		ENABLE_IE();

		return SUCCESS;
	}

	p_task = get_list_entry(p_msg_buf->head.next, Task, blk);

	remove_from_blk_queue(p_task);
	add_to_rdy_queue(p_task);

	p_task-> buf_msg = p_msg;

	p_task-> state = READY;

	ENABLE_IE();

	return SUCCESS;
}


// create event

STATUS create_event(Event* p_event, u32 val){

	if(NULL == p_event) {

		return PARAM_ERROR;
	}

	p_event-> blk_type = EVENT_TYPE;
	list_init(&p_event-> head);
	p_event-> val = val;

	return SUCCESS;
}

// get event

STATUS get_event(Event* p_event, u32 option, u32 val, u32* p_data, u8 wait){

	STATUS result;

	if(is_in_irq()){

		return IN_IRQ;
	}

	if(NULL == p_event) {

		return PARAM_ERROR;
	}

	if(EVENT_TYPE != p_event-> blk_type){

		return WRONG_BLOCK_TYPE;
	}

	if((AND_OPTION != option) && (OR_OPTION != option)){

		return PARAM_ERROR;
	}

	if(NULL == p_data) {

		return PARAM_ERROR;
	}

	DISABLE_IE();

	if(AND_OPTION == option) {

		if(val == (p_event-> val & val)) {

			*p_data = val;
			p_event-> val &= ~val;

			ENABLE_IE();

			return SUCCESS;
		}

	}else {

		if(p_event-> val & val) {

			*p_data = p_event-> val & val;
			p_event-> val &= ~(*p_data);

			ENABLE_IE();

			return SUCCESS;
		}
	}

	if(is_sched_lock()) {

		ENABLE_IE();

		return OS_SCHED_LOCKED;
	}

	if(!wait) {

		ENABLE_IE();

		return NOT_WAIT;
	}

	remove_from_rdy_queue(current_task);
	add_to_blk_queue(&p_event-> head, current_task);

	current_task-> event_opt = option;
	current_task-> event_val = val;
	current_task-> state = BLOCKED;

	result = dispatch();
	ENABLE_IE();

	if(SUCCESS != result) {

		return result;
	}

	DISABLE_IE();
	*p_data = current_task-> event_data;
	ENABLE_IE();

	return SUCCESS;
}

// put event

STATUS put_event(Event* p_event, u32 val) {

	Task* p_task;
	ListNode* p_node;

	if(NULL == p_event) {

		return PARAM_ERROR;
	}

	if(EVENT_TYPE != p_event-> blk_type) {

		return WRONG_BLOCK_TYPE;
	}

	DISABLE_IE();

	p_event-> val |= val;

	if(is_list_empty(&p_event->head)){

		ENABLE_IE();

		return SUCCESS;
	}

	p_node = p_event->head.next;

	while(p_node != &p_event->head) {

		p_task = get_list_entry(p_node, Task, blk);

		if(AND_OPTION == p_task-> event_opt) {

			if(p_task-> event_val == (p_event-> val & p_task-> event_val)){

				p_task-> event_data = p_task-> event_val;
				p_event-> val &= ~p_task-> event_val;

				p_node = p_node->next;

				remove_from_blk_queue(p_task);
				add_to_rdy_queue(p_task);

				p_task-> state = READY;

				continue;
			}

		}else {

			if(p_event-> val & p_task-> event_val) {

				p_task-> event_data = p_event-> val & p_task-> event_val;
				p_event-> val &= ~p_task-> event_data;

				p_node = p_node->next;

				remove_from_blk_queue(p_task);
				add_to_rdy_queue(p_task);

				p_task-> state = READY;

				continue;
			}
		}

		p_node = p_node->next;
	}

	ENABLE_IE();

	return SUCCESS;
}

// create timer

STATUS create_timer(Timer* p_timer, u32 val, void(*func)(void*), void* param){

	if(NULL == p_timer) {

		return PARAM_ERROR;
	}

	if(!val) {

		return PARAM_ERROR;
	}

	if(NULL == func) {

		return PARAM_ERROR;
	}

	list_init(&p_timer-> list);
	p_timer-> val = val;
	p_timer-> func = func;
	p_timer-> param = param;

	return SUCCESS;
}

// activate timer

STATUS activate_timer(Timer* p_timer) {

	ListNode* p_node;

	if(is_in_irq()) {

		return IN_IRQ;
	}

	if(NULL == p_timer) {

		return PARAM_ERROR;
	}

	sched_lock();

	p_node = g_timer.next;
	p_timer-> second = g_tick + p_timer-> val;

	while(p_node != &g_timer) {

		if(get_list_entry(p_node, Timer, list)-> second > p_timer-> second){

			break;
		}

		p_node = p_node->next;
	}

	list_insert(p_node, &p_timer-> list);
	sched_unlock();

	return SUCCESS;

}

// deactivate timer

STATUS deactivate_timer(Timer* p_timer){

	if(is_in_irq()) {

		return IN_IRQ;
	}

	if(NULL == p_timer) {

		return PARAM_ERROR;
	}

	sched_lock();

	if(is_list_empty(&p_timer-> list)){

		sched_unlock();

		return TIMER_NOT_RUN;
	}

	list_delete(&p_timer-> list);

	sched_unlock();

	return SUCCESS;

}

// timer task function

static void timer_running_func(void* param) {

	ListNode* p_node;
	Timer* p_timer;

	param = param;

	while(1) {

		get_sem(&timer_sem, 1);

		sched_lock();

		p_node = g_timer.next;

		while(p_node != &g_timer) {

			p_timer = get_list_entry(p_node, Timer, list);
			if(p_timer-> second > g_tick){
				break;
			}

			p_node = p_node->next;
			list_delete(&p_timer-> list);
			p_timer->func(p_timer-> param);
		}

		sched_unlock();
	}
}

// idle task function

static void idle_running_func(void* param) {

	param = param;

	while(1) {

		DISABLE_IE();
		g_idle ++;
		ENABLE_IE();

		yield();
	}

}

// function called by timer isr

void timer_isr_func() {

	g_tick ++;

	put_sem(&timer_sem);
}

// for test variable

EXTERN int global_test;

int main(int argc, char* argv[]) {

	os_init();

	global_test = 0;

	//test_task();

	//test_sem();

	//test_mutex();

	//test_mail();

	//test_buf();

	//test_event();

	test_timer();

	os_start();

	return 0;
}

