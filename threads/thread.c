#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include <stdbool.h>
#include <string.h>
#include "thread.h"
#include "interrupt.h"



/* define a list of thead states / state id */
typedef enum{
	EMPTY = 0,
	RUNNING = 1,
	READY = 2,
	KILLED = 3,
	EXITED = 4,
	SLEEP = 5
} Sid;

/* This is the thread control block */
typedef struct thread {
	Tid tid;						// thread id
	Sid sid;						// state id
	ucontext_t * context;			// CPU ucontext
	void * sp;						// stack pointer
	struct thread * next;			// pointer to next
} thread;

/* This is the general queue structure */
typedef struct wait_queue {
	thread * head;
} thread_queue;

/* Global variables */
Sid threadArray[THREAD_MAX_THREADS];
thread * runningThread;
thread_queue readyQueue;
thread_queue * waitQueueArray[THREAD_MAX_THREADS];

/* function prototypes */
void thread_yield_to_id(Tid want_tid, thread_queue * queue, int STATE);
void delete_exited_thread();
void thread_stub(void (*thread_main)(void *), void *arg);
void insert_at_last(thread_queue * queue, thread* newThread);
thread * remove_first(thread_queue * queue);
thread * remove_one_node(thread_queue * queue, Tid toRemove);
thread * search_one_node(thread_queue * queue, Tid toSearch);
void print_queue(thread * head, char * msg);



void
thread_init(void)
{
	// initialize data strucutre 
	readyQueue.head = NULL;
	for (int i = 0; i < THREAD_MAX_THREADS; i ++){
		threadArray[i] = EMPTY;
		waitQueueArray[i] = NULL;
	}
	
	// create running thread structure
	threadArray[0] = RUNNING;
	waitQueueArray[0] = wait_queue_create();
	runningThread = (thread *) malloc (sizeof(thread));
	runningThread->tid = 0;
	runningThread->sid = RUNNING;
	runningThread->context = (ucontext_t  *) malloc (sizeof(ucontext_t));
	runningThread->sp = NULL;
	runningThread->next = NULL;
	return;
}



Tid
thread_id()
{
	return runningThread->tid;
}



Tid
thread_create(void (*fn) (void *), void *parg)
{
	volatile int oldEnable = interrupts_off();
	// find next avaliable thread id
	Tid newTid = -1;
	for (Tid i = 0; i <= THREAD_MAX_THREADS; i++){
		if (threadArray[i] == EMPTY){
			newTid = i;
			break;
		}
	}
	if (newTid == -1){
		interrupts_set(oldEnable);
		return THREAD_NOMORE;
	}
	// create new thread structure
	thread * newThread = (thread *) malloc (sizeof(thread));
	newThread->sp = (void *) malloc (THREAD_MIN_STACK);
	if (newThread->sp == NULL){
		free(newThread);
		interrupts_set(oldEnable);
		return THREAD_NOMEMORY;
	}
	memset(newThread->sp, 0, THREAD_MIN_STACK);
	newThread->tid = newTid;
	newThread->sid = READY;
	newThread->next = NULL;
	newThread->context = (ucontext_t *) malloc (sizeof(ucontext_t));
	assert(!getcontext(newThread->context));
	unsigned long stackEnd = (unsigned long)newThread->sp + THREAD_MIN_STACK;
	stackEnd = stackEnd - stackEnd%16 - 8;
	newThread->context->uc_stack.ss_flags = 0;
	newThread->context->uc_stack.ss_size = THREAD_MIN_STACK;
	newThread->context->uc_stack.ss_sp = (void*) newThread->sp;
	newThread->context->uc_mcontext.gregs[REG_RSP] = (unsigned long) stackEnd;
	newThread->context->uc_mcontext.gregs[REG_RIP] = (unsigned long) &thread_stub;
	newThread->context->uc_mcontext.gregs[REG_RDI] = (unsigned long) fn;
	newThread->context->uc_mcontext.gregs[REG_RSI] = (unsigned long) parg;
	// update data structure
	threadArray[newTid] = READY;
	insert_at_last(&readyQueue, newThread);
	waitQueueArray[newTid] = wait_queue_create();
	interrupts_set(oldEnable);
	return newTid;
}



Tid
thread_yield(Tid want_tid)
{
	volatile int oldEnable = interrupts_off();
	delete_exited_thread();										// clear all EXITED threads	
	// CAUTION: do NOT change the order of below if conditions	
	if (want_tid == THREAD_SELF || want_tid == thread_id()){	// yield to itself
		want_tid = thread_id();
	}
	else if (want_tid == THREAD_ANY) {							// yield to any thread
		if (readyQueue.head == NULL){
			interrupts_set(oldEnable);
			return THREAD_NONE;
		}
		want_tid = readyQueue.head->tid;
		thread_yield_to_id(want_tid, &readyQueue, READY);
	}
	else if (want_tid < 0 || want_tid > THREAD_MAX_THREADS) {	// yield to an invalid thread
		interrupts_set(oldEnable);
		return THREAD_INVALID;
	}
	else {														// yield to a specific thread
		if (threadArray[want_tid] == EMPTY){
			interrupts_set(oldEnable);
			return THREAD_INVALID;
		}
		thread_yield_to_id(want_tid, &readyQueue, READY);
	}
	interrupts_set(oldEnable);
	return want_tid;
}



Tid
thread_exit()
{
	volatile int oldEnable = interrupts_off();
	if (readyQueue.head == NULL) {								// no other threads can run
		interrupts_set(oldEnable);
		return THREAD_NONE;
	}
	// label thread with EXITED
	threadArray[thread_id()] = EXITED;
	runningThread->sid = EXITED;
	int ret = thread_yield(THREAD_ANY);
	// error checking, for debugging purposes
	printf("thread_exit() error: trying to return, ret = %d\n", ret);
	assert(0);			// should not return here
	interrupts_set(oldEnable);
	return ret;
}



Tid
thread_kill(Tid tid)
{
	volatile int oldEnable = interrupts_off();
	if (tid == THREAD_SELF || tid == thread_id() || 		// killing an invalid thread
		tid < 0 || tid > THREAD_MAX_THREADS ||
		(threadArray[tid] != READY && threadArray[tid] != SLEEP) ) {
		interrupts_set(oldEnable);
		return THREAD_INVALID;
	}
	// find this thread & label with KILLED
	thread * temp = NULL;
	if (threadArray[tid] == READY){							// kill a READY thread
		temp = search_one_node(&readyQueue, tid);
	}
	else{													// kill a SLEEP thread
		for (int i = 0; i < THREAD_MAX_THREADS; i++){
			temp = search_one_node(waitQueueArray[i], tid);
			if (temp != NULL)
				break;
		}
	}
	threadArray[tid] = KILLED;
	temp->sid = KILLED;
	// change thread's PC to thread_exit()
	temp->context->uc_mcontext.gregs[REG_RIP] = (unsigned long) &thread_exit;
	interrupts_set(oldEnable);
	return tid;
}



/* make sure the thread is EMPTY before calling */
/* yield to a thread with given identifier */
void thread_yield_to_id(Tid want_tid, thread_queue * queue, int STATE){
	volatile int oldEnable = interrupts_off();
	volatile bool setcontextCalled = false;
	assert(!getcontext(runningThread->context));
	if (!setcontextCalled){							// context switch not called
		if (threadArray[thread_id()] != KILLED && threadArray[thread_id()] != EXITED){	// thread is not KILLED or EXITED
			threadArray[thread_id()] = STATE;
			runningThread->sid = STATE;
		}
		thread * temp = runningThread;
		runningThread = remove_one_node(&readyQueue, want_tid);
		insert_at_last(queue, temp);
		if (threadArray[thread_id()] != KILLED && threadArray[thread_id()] != EXITED){	// thread is not KILLED or EXITED
			threadArray[thread_id()] = RUNNING;
			runningThread->sid = RUNNING;
		}
		setcontextCalled = true;
		setcontext(runningThread->context);
	}
	interrupts_set(oldEnable);
	return;
}



/* this function delete all exited threads */
void delete_exited_thread(){
	volatile int oldEnable = interrupts_off();
	for (int i = 0; i < THREAD_MAX_THREADS; i++){
		if (i != thread_id() && threadArray[i] == EXITED){
			// delete (free) EXITED thread
			thread * temp = remove_one_node(&readyQueue, i);
			free(temp->sp);
			free(temp->context);
			free(temp);
			// update data strucutre: thread array, ready queue, wait queue
			threadArray[i] = EMPTY;
			wait_queue_destroy(waitQueueArray[i]);
			waitQueueArray[i] = NULL;
		}
	}
	interrupts_set(oldEnable);
	return;
}



/* thread starts by calling thread_stub. The arguments to thread_stub are the
 * thread_main() function, and one argument to the thread_main() function. */
void
thread_stub(void (*thread_main)(void *), void *arg)
{
	interrupts_on();
	Tid ret;

	thread_main(arg); // call thread_main() function with arg
	ret = thread_exit();
	// we should only get here if we are the last thread. 
	assert(ret == THREAD_NONE);
	// all threads are done, so process should exit
	exit(0);
}



/* insert a node at the end of a linked list */
/* this function is called inside critical section, i.e. during interrupt off */
void insert_at_last(thread_queue * queue, thread * newThread){
	if (queue->head == NULL)
		queue->head = newThread;
	else{
		thread * temp = queue->head;
		while(temp->next != NULL)
			temp = temp->next;
		temp->next = newThread;
	}
	return;
}



/* remove the first node in the linked list, */
/* return the pointer to removed thread */
/* this function is called inside critical section, i.e. during interrupt off */
thread * remove_first(thread_queue * queue){
	if (queue->head == NULL){
		return NULL;
	}
	thread * temp = queue->head;
	queue->head = queue->head->next;
	temp->next = NULL;	// temp is removed
	return temp;
}



/* remove a given node inside the linked list, */
/* if found, return the pointer to removed thread */
/* this function is called inside critical section, i.e. during interrupt off */
thread * remove_one_node(thread_queue * queue, Tid toRemove){
	if (queue->head == NULL){
		return NULL;
	}
	else if (queue->head->tid == toRemove){
		return remove_first(queue);
	}
	else{
		thread * temp = queue->head;
		thread * after = temp->next;
		while (after->tid != toRemove){
			temp = temp->next;
			after = temp->next;
		}
		temp->next = after->next;
		after->next = NULL;	// after is now removed
		return after;
	}
}



/* search for a given node inside the linked list, */
/* if found, return the pointer to searched thread */
/* this function is called inside critical section, i.e. during interrupt off */
thread * search_one_node(thread_queue * queue, Tid toSearch){
	if (queue == NULL)
		return NULL;
	thread * temp = queue->head;
	while (temp != NULL){
		if (temp->tid == toSearch){
			return temp;
		}
		temp = temp->next;
	}
	return NULL;
}



/* print queue function, for debugging porposes */
/* this function is called inside critical section, i.e. during interrupt off */
void print_queue(thread * head, char * msg){
	unintr_printf("Printing queue: %s\n", msg);
	thread * temp = (thread *) head;
	int counter = 0;
	while (temp != NULL){
		unintr_printf("%4d ->", temp->tid);
		temp = temp->next;
		++ counter;
	}
	unintr_printf("\nNumber of threads in queue: %d\n\n", counter);
	return;
}



/*******************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 *******************************************************************/



/* make sure to fill the wait_queue structure defined above */
struct wait_queue *
wait_queue_create()
{
	thread_queue * wq = malloc(sizeof(struct wait_queue));
	wq->head = NULL;
	return wq;
}



void
wait_queue_destroy(struct wait_queue *wq)
{
	thread_wakeup(wq, 1);
	free(wq);
	return;
}



Tid
thread_sleep(struct wait_queue *queue)
{
	volatile int oldEnable = interrupts_off();
	// printf("sleep: running: %3d\n", thread_id());///////////////// testing ////////////////
	// printf("thread_sleep: calling thread %d start waiting on thread %d\n", thread_id(), tid);///////////////// testing ////////////////
	if (queue == NULL){
		// printf("sleep: return INVALID\n");///////////////// testing ////////////////
		interrupts_set(oldEnable);
		return THREAD_INVALID;
	}
	if (readyQueue.head == NULL){		// emtpy ready queue, THREAD_NONE
		// printf("sleep: return NONE\n");///////////////// testing ////////////////
		interrupts_set(oldEnable);
		return THREAD_NONE;
	}
	else{								// yield to THREAD_ANY
		// int oldRunning = thread_id();///////////////// testing ////////////////
		// thread_wakeup(waitQueueArray[thread_id()], 1);
		Tid want_tid = readyQueue.head->tid;
		thread_yield_to_id(want_tid, queue, SLEEP);
		// print_queue(readyQueue.head, "ready queue");
		// printf("sleep: return SUCCESS on %d\n", oldRunning);///////////////// testing ////////////////
		interrupts_set(oldEnable);
		return want_tid;
	}
}



/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
	volatile volatile int oldEnable = interrupts_off();
	if (queue == NULL || queue->head == NULL){
		interrupts_set(oldEnable);
		return 0;
	}
	else if (all == 1){
		int count = 0;
		thread * temp = queue->head;
		while (temp != NULL){
			count ++;
			temp->sid = READY;
			threadArray[temp->tid] = READY;
			temp = temp->next;
		}
		insert_at_last(&readyQueue, queue->head);
		queue->head = NULL;
		interrupts_set(oldEnable);
		return count;
	}
	else{
		thread * wakeupThread = remove_first(queue);
		insert_at_last(&readyQueue, wakeupThread);
		wakeupThread->sid = READY;
		threadArray[wakeupThread->tid] = READY;
		interrupts_set(oldEnable);
		return 1;
	}
}



/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid)
{
	// printf("thread_wait: here\n");///////////////// testing ////////////////
	spin(75500);///////////////// testing ////////////////
	volatile int oldEnable = interrupts_off();
	// printf("wait: running: %3d, wait: %3d\n", thread_id(), tid);///////////////// testing ////////////////
	// printf("thread_wait: calling thread %d start waiting on thread %d\n", thread_id(), tid);///////////////// testing ////////////////
	if (tid < 0 || tid > THREAD_MAX_THREADS || tid == thread_id() 
		|| threadArray[tid] == EMPTY || threadArray[tid] == EXITED){
		// printf("wait: return INVALID\n");///////////////// testing ////////////////
		interrupts_set(oldEnable);
		return THREAD_INVALID;
	}
	// printf("thread_wait: calling thread %d in the process of waiting on thread %d\n", thread_id(), tid);///////////////// testing ////////////////
	// thread_wakeup(waitQueueArray[thread_id()], 1);
	// spin(500);
	thread_sleep(waitQueueArray[tid]);
	// printf("wait: return SUCCESS on %d\n", tid);///////////////// testing ////////////////
	// print_queue(readyQueue.head, "ready queue");
	// printf("thread_wait: calling thread %d finish waiting on thread %d, with return value %d\n", thread_id(), tid, ret);///////////////// testing ////////////////
	interrupts_set(oldEnable);
	return tid;
}



//////////////////////////////////////////////////////////////////////////////////////////////////////////////



struct lock {
	bool acquired;
	Tid lockTid;
	thread_queue * waitQueue;
};



struct lock *
lock_create()
{
	volatile int oldEnable = interrupts_off();
	struct lock * lock = malloc(sizeof(struct lock));
	assert(lock);
	lock->acquired = false;
	lock->lockTid = THREAD_NONE;
	lock->waitQueue = wait_queue_create();
	assert(lock->waitQueue);
	interrupts_set(oldEnable);
	return lock;
}



void
lock_destroy(struct lock *lock)
{
	volatile int oldEnable = interrupts_off();
	assert(lock != NULL);
	wait_queue_destroy(lock->waitQueue);
	free(lock);
	interrupts_set(oldEnable);
	return;
}



void
lock_acquire(struct lock *lock)
{
	volatile int oldEnable = interrupts_off();
	assert(lock != NULL);
	/*	http://www.rowleydownload.co.uk/arm/documentation/gnu/gcc/_005f_005fsync-Builtins.html
		type __sync_val_compare_and_swap ( type *ptr, type oldval, type newval, ...)
		These built-in functions perform an atomic compare and swap. 
		That is, if the current value of * ptr is oldval , then write newval into * ptr .
		The “val” version returns the contents of * ptr before the operation. 	*/
	while(__sync_val_compare_and_swap(&(lock->lockTid), THREAD_NONE, thread_id()) != THREAD_NONE){	// sleep all threads into lock's wait queue
		lock->acquired = true;
		lock->lockTid = thread_id();
		thread_sleep(lock->waitQueue);
	}
	interrupts_set(oldEnable);
	return;
}



void
lock_release(struct lock *lock)
{
	volatile int oldEnable = interrupts_off();
	assert(lock != NULL);
	thread_wakeup(lock->waitQueue, 1);	// wake up all threads
	lock->acquired = false;
	lock->lockTid = THREAD_NONE;
	interrupts_set(oldEnable);
	return;
}



//////////////////////////////////////////////////////////////////////////////////////////////////////////////



struct cv {
	thread_queue * queue;
};



struct cv *
cv_create()
{
	volatile int oldEnable = interrupts_off();
	struct cv * cv = malloc(sizeof(struct cv));
	assert(cv);
	cv->queue = wait_queue_create();
	interrupts_set(oldEnable);
	return cv;
}



void
cv_destroy(struct cv *cv)
{
	volatile int oldEnable = interrupts_off();
	assert(cv != NULL);
	wait_queue_destroy(cv->queue);
	free(cv);
	interrupts_set(oldEnable);
	return;
}



void
cv_wait(struct cv *cv, struct lock *lock)
{
	volatile int oldEnable = interrupts_off();
	assert(cv != NULL);
	assert(lock != NULL);
	lock_release(lock);
	thread_sleep(cv->queue);
	lock_acquire(lock);
	interrupts_set(oldEnable);
	return;
}



void
cv_signal(struct cv *cv, struct lock *lock)
{
	volatile int oldEnable = interrupts_off();
	assert(cv != NULL);
	assert(lock != NULL);
	thread_wakeup(cv->queue, 0);	// wakeup one thread on cv_signal
	interrupts_set(oldEnable);
	return;
}



void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	volatile int oldEnable = interrupts_off();
	assert(cv != NULL);
	assert(lock != NULL);
	thread_wakeup(cv->queue, 1);	// wakeup all threads on cv_boardcast
	interrupts_set(oldEnable);
	return;
}