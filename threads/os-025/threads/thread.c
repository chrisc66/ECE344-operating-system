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
	EXITED = 3,
	KILLED = 4,
	SLEEP = 5
} Sid;

/* This is the thread control block */
typedef struct thread {
	Tid tid;					// thread id
	Sid sid;					// state id
	ucontext_t * context;		// CPU ucontext
	void * sp;					// stack pointer
	// thread_queue * waitQueue;	// wait queue
	struct thread * next;		// pointer to next
} thread;

/* This is the general queue structure */
typedef struct wait_queue {
	thread * head;
} thread_queue;

/* Global variables */
Sid threadArray[THREAD_MAX_THREADS];
thread * runningThread;
thread_queue readyQueue;

/* function prototypes */
void thread_yield_to_id(Tid want_tid);
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
	// data strucutre 
	readyQueue.head = NULL;
	for (int i = 0; i < THREAD_MAX_THREADS; i ++)
		threadArray[i] = EMPTY;
	// running thread
	threadArray[0] = RUNNING;
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
	// find next avaliable thread id
	Tid newTid = -1;
	for (Tid i = 0; i <= THREAD_MAX_THREADS; i++){
		if (threadArray[i] == EMPTY){
			newTid = i;
			break;
		}
	}
	if (newTid == -1){
		return THREAD_NOMORE;
	}
	// create new thread
	thread * newThread = (thread *) malloc (sizeof(thread));
	if (newThread == NULL){
		return THREAD_NOMEMORY;
	}
	newThread->sp = (void *) malloc (THREAD_MIN_STACK);
	if (newThread->sp == NULL){
		free(newThread);
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
	return newTid;
}



Tid
thread_yield(Tid want_tid)
{
	delete_exited_thread();										// clear all EXITED threads	
	// CAUTION: do NOT change the order of below if conditions	
	if (want_tid == THREAD_SELF || want_tid == thread_id()){	// yield to itself
		want_tid = thread_id();
	}
	else if (want_tid == THREAD_ANY) {							// yield to any thread
		if (readyQueue.head == NULL){
			return THREAD_NONE;
		}
		want_tid = readyQueue.head->tid;
		thread_yield_to_id(want_tid);
	}
	else if (want_tid < 0 || want_tid > THREAD_MAX_THREADS) {	// yield to an invalid thread
		return THREAD_INVALID;
	}
	else {														// yield to a specific thread
		if (threadArray[want_tid] == EMPTY){
			return THREAD_INVALID;
		}
		thread_yield_to_id(want_tid);
	}
	return want_tid;
}



Tid
thread_exit()
{
	if (readyQueue.head == NULL) {								// no other threads can run
		return THREAD_NONE;
	}
	// label thread with EXIT
	threadArray[thread_id()] = EXITED;
	runningThread->sid = EXITED;
	int ret = thread_yield(THREAD_ANY);
	printf("thread_exit() error: trying to return, ret = %d\n", ret);
	assert(0);			// should not return here
	return ret;
}



Tid
thread_kill(Tid tid)
{
	if (tid == THREAD_SELF || tid == thread_id() || 			// killing an invalid thread
		tid < 0 || tid > THREAD_MAX_THREADS ||
		(threadArray[tid] != READY && threadArray[tid] != SLEEP) ) {
		return THREAD_INVALID;
	}
	// find this thread & label with KILLED
	thread * temp = NULL;
	if (threadArray[tid] == READY)				// kill a READY thread
		temp = search_one_node(&readyQueue, tid);
	else										// kill a SLEEP thread
		assert(0);	// implement in lab 3
	threadArray[tid] = KILLED;
	temp->sid = KILLED;
	// change thread's PC to thread_exit()
	temp->context->uc_mcontext.gregs[REG_RIP] = (unsigned long) &thread_exit;
	return tid;
}



/* make sure the thread is EMPTY before calling */
/* yield to a thread with given identifier */
void thread_yield_to_id(Tid want_tid){
	volatile bool setcontextCalled = false;
	assert(!getcontext(runningThread->context));
	if (!setcontextCalled){							// context switch not called
		if (threadArray[runningThread->tid] != KILLED && threadArray[runningThread->tid] != EXITED){	// thread is not EXITED
			threadArray[runningThread->tid] = READY;
			runningThread->sid = READY;
		}
		thread * temp = runningThread;
		runningThread = remove_first(&readyQueue);
		insert_at_last(&readyQueue, temp);
		if (threadArray[runningThread->tid] != KILLED && threadArray[runningThread->tid] != EXITED){	// thread is not EXITED
			threadArray[runningThread->tid] = RUNNING;
			runningThread->sid = RUNNING;
		}
		setcontextCalled = true;
		setcontext(runningThread->context);
	}
	return;
}



/* this function delete all exited threads */
void delete_exited_thread(){
	for (int i = 0; i < THREAD_MAX_THREADS; i++){
		if (i != thread_id() && threadArray[i] == EXITED){
			// delete (free) EXITED thread
			thread * temp = remove_one_node(&readyQueue, i);
			free(temp->sp);
			free(temp->context);
			free(temp);
			// update data strucutre 
			threadArray[i] = EMPTY;
		}
	}
	return;
}



/* thread starts by calling thread_stub. The arguments to thread_stub are the
 * thread_main() function, and one argument to the thread_main() function. */
void
thread_stub(void (*thread_main)(void *), void *arg)
{
	Tid ret;

	thread_main(arg); // call thread_main() function with arg
	ret = thread_exit();
	// we should only get here if we are the last thread. 
	assert(ret == THREAD_NONE);
	// all threads are done, so process should exit
	exit(0);
}



/* insert a node at the end of a linked list */
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



/* remove the first node in the linked list */
/* return the pointer to removed thread */
thread * remove_first(thread_queue * queue){
	if (queue->head == NULL){
		return NULL;
	}
	thread * temp = queue->head;
	queue->head = queue->head->next;
	temp->next = NULL;	// temp is removed
	return temp;
}



/* remove a given node inside the linked list */
/* if found, return the pointer to removed thread */
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



/* search for a given node inside the linked list */
/* if found, return the pointer to searched thread */
thread * search_one_node(thread_queue * queue, Tid toSearch){
	thread * temp = queue->head;
	while (temp != NULL){
		if (temp->tid == toSearch){
			return temp;
		}
		temp = temp->next;
	}
	return NULL;
}



/* print queue, for debugging porposes */
void print_queue(thread * head, char * msg){
	unintr_printf("Printing queue: %s\n", msg);
	thread * temp = (thread *) head;
	int counter = 0;
	while (temp != NULL){
		unintr_printf("id = %4d, state = %d\n", temp->tid, temp->sid);
		temp = temp->next;
		++ counter;
	}
	unintr_printf("Number of threads in queue: %d\n", counter);
	return;
}



/*******************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 *******************************************************************/



/* make sure to fill the wait_queue structure defined above */
struct wait_queue *
wait_queue_create()
{
	struct wait_queue *wq;

	wq = malloc(sizeof(struct wait_queue));
	assert(wq);

	TBD();

	return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
	TBD();
	free(wq);
}

Tid
thread_sleep(struct wait_queue *queue)
{
	TBD();
	return THREAD_FAILED;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
	TBD();
	return 0;
}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid)
{
	TBD();
	return 0;
}

struct lock {
	/* ... Fill this in ... */
};

struct lock *
lock_create()
{
	struct lock *lock;

	lock = malloc(sizeof(struct lock));
	assert(lock);

	TBD();

	return lock;
}

void
lock_destroy(struct lock *lock)
{
	assert(lock != NULL);

	TBD();

	free(lock);
}

void
lock_acquire(struct lock *lock)
{
	assert(lock != NULL);

	TBD();
}

void
lock_release(struct lock *lock)
{
	assert(lock != NULL);

	TBD();
}

struct cv {
	/* ... Fill this in ... */
};

struct cv *
cv_create()
{
	struct cv *cv;

	cv = malloc(sizeof(struct cv));
	assert(cv);

	TBD();

	return cv;
}

void
cv_destroy(struct cv *cv)
{
	assert(cv != NULL);

	TBD();

	free(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}