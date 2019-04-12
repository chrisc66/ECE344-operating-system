#include "request.h"
#include "server_thread.h"
#include "common.h"
#include <stdbool.h>



/* global variable and structure */

pthread_mutex_t B_LOCK = PTHREAD_MUTEX_INITIALIZER;		// buffer lock
pthread_cond_t B_FULL = PTHREAD_COND_INITIALIZER;		// buffer full cv
pthread_cond_t B_EMPTY = PTHREAD_COND_INITIALIZER;		// buffer empty cv
int B_IN = 0;	// buffer in index
int B_OUT = 0;	// buffer out index

struct server {
	int nr_threads;
	int max_requests;
	int max_cache_size;
	int exiting;
	/* add any other parameters you need */
	pthread_t **worker_thread;
	int *request_buff;
};



/* function declarations */
void worker_thread (struct server *sv);



/* static functions */

/* initialize file data */
static struct file_data *
file_data_init(void)
{
	struct file_data *data;

	data = Malloc(sizeof(struct file_data));
	data->file_name = NULL;
	data->file_buf = NULL;
	data->file_size = 0;
	return data;
}



/* free all file data */
static void
file_data_free(struct file_data *data)
{
	free(data->file_name);
	free(data->file_buf);
	free(data);
}



static void
do_server_request(struct server *sv, int connfd)
{
	int ret;
	struct request *rq;
	struct file_data * data = file_data_init();

	/* fill data->file_name with name of the file being requested */
	rq = request_init(connfd, data);
	if (!rq) {
		file_data_free(data);
		return;
	}
	/* read file, 
	* fills data->file_buf with the file contents,
	* data->file_size with file size. */
	ret = request_readfile(rq);
	if (ret == 0) { /* couldn't read file */
		goto out;
	}
	/* send file to client */
	request_sendfile(rq);
out:
	request_destroy(rq);
	file_data_free(data);
	return;
}



/* entry point functions */

struct server *
server_init(int nr_threads, int max_requests, int max_cache_size)
{
	pthread_mutex_lock(&B_LOCK);
	// critical region start	
	struct server * sv = (struct server *)Malloc(sizeof(struct server));
	sv->nr_threads = nr_threads;
	sv->max_requests = max_requests + 1;
	sv->max_cache_size = max_cache_size;
	sv->exiting = 0;
	sv->worker_thread = NULL;
	sv->request_buff= NULL;

	if (nr_threads > 0 || max_requests > 0 || max_cache_size > 0) {
		// create queue of max_request size, if max_requests > 0
		if (max_requests > 0) {
			sv->request_buff = (int *) Malloc (sizeof(int) * (max_requests + 1));
		}		
		/* Lab 5: init server cache and limit its size to max_cache_size */
		/*
		if (max_cache_size > 0) {
			// init cache for this server
		}
		*/
		// create worker threads, if nr_threads > 0
		if (nr_threads > 0) {
			sv->worker_thread = (pthread_t **) Malloc (sizeof(pthread_t*) * nr_threads);
			for (unsigned i = 0; i < nr_threads; i++){
				sv->worker_thread[i] = (pthread_t *) Malloc (sizeof(pthread_t));
				pthread_create(sv->worker_thread[i], NULL, (void *) &worker_thread, sv);
			}
		}
	}
	// critical region end
	pthread_mutex_unlock(&B_LOCK);
	return sv;
}



/* producer, add one server request */
void
server_request(struct server *sv, int connfd)
{
	if (sv->nr_threads == 0) { /* no worker threads */
		do_server_request(sv, connfd);
	} 
	else {
		/*  Save the relevant info in a buffer and have one of the
		 *  worker threads do the work. */
		pthread_mutex_lock(&B_LOCK);
		while ((B_IN - B_OUT + sv->max_requests) % sv->max_requests == sv->max_requests - 1 && sv->exiting == 0){
			pthread_cond_wait(&B_FULL, &B_LOCK);
		}
		sv->request_buff[B_IN] = connfd;
		if (B_IN == B_OUT){
			pthread_cond_broadcast(&B_EMPTY);
		}
		B_IN = (B_IN + 1) % sv->max_requests;
		pthread_mutex_unlock(&B_LOCK);
	}
	return;
}



void
server_exit(struct server *sv)
{
	/* when using one or more worker threads, use sv->exiting to indicate to
	 * these threads that the server is exiting. make sure to call
	 * pthread_join in this function so that the main server thread waits
	 * for all the worker threads to exit before exiting. */
	sv->exiting = 1;
	pthread_cond_broadcast(&B_EMPTY);
	for(unsigned i = 0; i < sv->nr_threads; i++){
		pthread_join(*sv->worker_thread[i],NULL);
	}
	/* make sure to free any allocated resources */
	for (unsigned i = 0; i < sv->nr_threads; i++){
		free(sv->worker_thread[i]);
	}
	free(sv->request_buff);
	free(sv->worker_thread);
	free(sv);
	return;
}



/* starting routine for pthread_create */
/* consumer, handle one server request */
void worker_thread (struct server *sv)
{
	while(true){
		// critical region start
		pthread_mutex_lock(&B_LOCK);
		while (B_IN == B_OUT && sv->exiting == 0){
			pthread_cond_wait(&B_EMPTY, &B_LOCK);
		}
		int connfd = sv->request_buff[B_OUT];
		if ((B_IN - B_OUT + sv->max_requests) % sv->max_requests == sv->max_requests - 1){
			pthread_cond_signal(&B_FULL);
		}
		B_OUT = (B_OUT + 1) % (sv->max_requests);
		pthread_mutex_unlock(&B_LOCK);
		// critical region end
		if (sv->exiting == 1){
			pthread_exit(NULL);
		}
		do_server_request(sv, connfd);
	}
	return;
}