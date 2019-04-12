#include "request.h"
#include "server_thread.h"
#include "common.h"

/* --------------------------------------------------------------------------------------- */
/* global variables */

#define CACHE_TABLE_SIZE 3571
pthread_mutex_t B_LOCK = PTHREAD_MUTEX_INITIALIZER; // buffer lock
pthread_cond_t B_FULL = PTHREAD_COND_INITIALIZER;   // buffer full cv
pthread_cond_t B_EMPTY = PTHREAD_COND_INITIALIZER;  // buffer empty cv
int B_IN = 0;										// buffer in index
int B_OUT = 0;										// buffer out index
pthread_mutex_t C_LOCK = PTHREAD_MUTEX_INITIALIZER; // cache lock

/* --------------------------------------------------------------------------------------- */
/* request least recently used (lru) linked list structure */

typedef struct lru_node
{
	char *fileName;
	struct lru_node *next;
	struct lru_node *prev;
} lru_node;

typedef struct lru_list
{
	int listSize;
	lru_node *head;
	lru_node *tail;
} lru_list;

lru_list *lru_list_init(void);
void lru_list_destroy(lru_list *list);
lru_node *lru_list_search(lru_list *list, struct file_data *file_data);
void lru_list_insert_at_first(lru_list *list, struct file_data *file_data);
int lru_list_remove_first(lru_list *list);
int lru_list_remove_last(lru_list *list);
int lru_list_remove_one(lru_list *list, struct file_data *file_data);
int lru_list_move_to_head(lru_list *list, struct file_data *file_data);

/* --------------------------------------------------------------------------------------- */
/* cache hash table structure */

typedef struct cache_ht_entry
{
	struct file_data *fileData;
	int inUse;
	struct cache_ht_entry *next;
} cache_ht_entry;

typedef struct cache_hash_table
{
	int tableSize;
	cache_ht_entry *head[CACHE_TABLE_SIZE];
} cache_hash_table;

int djb2(char *key);
cache_hash_table *cache_ht_init(void);
void cache_ht_destroy(cache_hash_table *hashTable);
cache_ht_entry *cache_ht_insert(cache_hash_table *hashTable, struct file_data *data);
cache_ht_entry *cache_ht_search(cache_hash_table *hashTable, char *fileName);
int cache_ht_delete(cache_hash_table *hashTable, struct file_data *data);

/* --------------------------------------------------------------------------------------- */
/* cache structure */

typedef struct server_cache
{
	int maxSize;
	int maxTableSize;
	int curSize;
	cache_hash_table *hashTable;
	lru_list *lruOrder;
} server_cache;

struct server_cache *cache_init(int maxSize);
void cache_destroy(server_cache *cache);
cache_ht_entry *cache_insert(server_cache *cache, struct file_data *fileData);
cache_ht_entry *cache_lookup(server_cache *cache, char *fileName);
int cache_evict(server_cache *cache, int size);

/* --------------------------------------------------------------------------------------- */
/* server structure */

typedef struct server
{
	int exiting;
	int nr_threads;
	pthread_t **worker_thread;
	int max_requests;
	int *request_buff;
	int max_cache_size;
	server_cache *cache;
} server;

/* server and file data function declarations */
void worker_thread(server *sv);
struct server *server_init(int nr_threads, int max_requests, int max_cache_size);
void server_request(struct server *sv, int connfd);
static void do_server_request(struct server *sv, int connfd);
void server_exit(struct server *sv);
static struct file_data *file_data_init(void);
static void file_data_free(struct file_data *data);

/* --------------------------------------------------------------------------------------- */

/* initialize file data */
static struct file_data *file_data_init(void)
{
	struct file_data *data;

	data = Malloc(sizeof(struct file_data));
	data->file_name = NULL;
	data->file_buf = NULL;
	data->file_size = 0;
	return data;
}

/* free all file data */
static void file_data_free(struct file_data *data)
{
	free(data->file_name);
	free(data->file_buf);
	free(data);
}

static void do_server_request(struct server *sv, int connfd)
{
	int ret;
	struct request *rq;
	struct file_data *data = file_data_init();

	/* fill data->file_name with name of the file being requested */
	rq = request_init(connfd, data);
	if (!rq)
	{
		file_data_free(data);
		return;
	}
	if (sv->max_cache_size == 0)
	{ // no cache
		/* read file, 
		* fills data->file_buf with the file contents,
		* data->file_size with file size. */
		ret = request_readfile(rq);
		if (ret == 0)
		{ /* couldn't read file */
			goto out;
		}
		/* send file to client */
		request_sendfile(rq);
	}
	else
	{ // use cache
		cache_ht_entry *search = NULL;
		pthread_mutex_lock(&C_LOCK);
		search = cache_lookup(sv->cache, data->file_name);
		if (search != NULL)
		{ // file data exists in cache
			search->inUse++;
			data->file_size = search->fileData->file_size;
			data->file_buf = strdup(search->fileData->file_buf);
			request_set_data(rq, data);
			lru_list_move_to_head(sv->cache->lruOrder, data);
		}
		else
		{ // file data does not exist in cache
			pthread_mutex_unlock(&C_LOCK);
			ret = request_readfile(rq);
			if (ret == 0)
			{ /* couldn't read file */
				goto out;
			}
			pthread_mutex_lock(&C_LOCK);
			search = cache_insert(sv->cache, data);
			if (search != NULL)
			{
				search->inUse++;
				lru_list_move_to_head(sv->cache->lruOrder, data);
			}
		}
		pthread_mutex_unlock(&C_LOCK);
		request_sendfile(rq);
		if (search != NULL)
			search->inUse--;
	}
out:
	request_destroy(rq);
	file_data_free(data);
	return;
}

struct server *server_init(int nr_threads, int max_requests, int max_cache_size)
{
	pthread_mutex_lock(&B_LOCK);
	struct server *sv = (struct server *)Malloc(sizeof(struct server));
	sv->nr_threads = nr_threads;
	sv->max_requests = max_requests + 1;
	sv->max_cache_size = max_cache_size;
	sv->exiting = 0;
	sv->worker_thread = NULL;
	sv->request_buff = NULL;
	sv->cache = NULL;

	if (nr_threads > 0 || max_requests > 0 || max_cache_size > 0)
	{
		// create queue of max_request size, if max_requests > 0
		if (max_requests > 0)
		{
			sv->request_buff = (int *)Malloc(sizeof(int) * (max_requests + 1));
		}
		// Lab 5: init server cache and limit its size to max_cache_size
		if (max_cache_size > 0)
		{
			sv->cache = cache_init(max_cache_size);
		}
		// create worker threads, if nr_threads > 0
		if (nr_threads > 0)
		{
			sv->worker_thread = (pthread_t **)Malloc(sizeof(pthread_t *) * nr_threads);
			for (unsigned i = 0; i < nr_threads; i++)
			{
				sv->worker_thread[i] = (pthread_t *)Malloc(sizeof(pthread_t));
				pthread_create(sv->worker_thread[i], NULL, (void *)&worker_thread, sv);
			}
		}
	}

	pthread_mutex_unlock(&B_LOCK);
	return sv;
}

/* producer, add one server request */
void server_request(struct server *sv, int connfd)
{
	if (sv->nr_threads == 0)
	{ /* no worker threads */
		do_server_request(sv, connfd);
	}
	else
	{
		/*  Save the relevant info in a buffer and have one of the
		 *  worker threads do the work. */
		pthread_mutex_lock(&B_LOCK);
		while ((B_IN - B_OUT + sv->max_requests) % sv->max_requests == sv->max_requests - 1 && sv->exiting == 0)
		{
			pthread_cond_wait(&B_FULL, &B_LOCK);
		}
		sv->request_buff[B_IN] = connfd;
		if (B_IN == B_OUT)
		{
			pthread_cond_broadcast(&B_EMPTY);
		}
		B_IN = (B_IN + 1) % sv->max_requests;
		pthread_mutex_unlock(&B_LOCK);
	}
	return;
}

void server_exit(struct server *sv)
{
	/* when using one or more worker threads, use sv->exiting to indicate to
	 * these threads that the server is exiting. make sure to call
	 * pthread_join in this function so that the main server thread waits
	 * for all the worker threads to exit before exiting. */
	sv->exiting = 1;
	pthread_cond_broadcast(&B_EMPTY);
	for (unsigned i = 0; i < sv->nr_threads; i++)
	{
		pthread_join(*sv->worker_thread[i], NULL);
	}
	/* make sure to free any allocated resources */
	for (unsigned i = 0; i < sv->nr_threads; i++)
	{
		free(sv->worker_thread[i]);
	}
	cache_destroy(sv->cache);
	free(sv->request_buff);
	free(sv->worker_thread);
	free(sv);
	return;
}

/* starting routine for pthread_create */
/* consumer, handle one server request */
void worker_thread(struct server *sv)
{
	while (1)
	{
		pthread_mutex_lock(&B_LOCK);
		while (B_IN == B_OUT && sv->exiting == 0)
		{
			pthread_cond_wait(&B_EMPTY, &B_LOCK);
		}
		int connfd = sv->request_buff[B_OUT];
		if ((B_IN - B_OUT + sv->max_requests) % sv->max_requests == sv->max_requests - 1)
		{
			pthread_cond_signal(&B_FULL);
		}
		B_OUT = (B_OUT + 1) % (sv->max_requests);
		pthread_mutex_unlock(&B_LOCK);
		if (sv->exiting == 1)
		{
			pthread_exit(NULL);
		}
		do_server_request(sv, connfd);
	}
	return;
}

/* --------------------------------------------------------------------------------------- */

/* initialize server cache entry */
struct server_cache *cache_init(int maxSize)
{
	server_cache *cache = (server_cache *)Malloc(sizeof(server_cache));
	cache->maxSize = maxSize;
	cache->maxTableSize = CACHE_TABLE_SIZE;
	cache->curSize = 0;
	cache->lruOrder = lru_list_init();
	cache->hashTable = cache_ht_init();
	return cache;
}

/* free cache entry */
void cache_destroy(server_cache *cache)
{
	if (cache != NULL)
	{
		lru_list_destroy(cache->lruOrder);
		cache_ht_destroy(cache->hashTable);
		free(cache);
	}
	return;
}

cache_ht_entry *cache_insert(server_cache *cache, struct file_data *fileData)
{
	if (fileData->file_size > cache->maxSize)
		return NULL;
	cache_ht_entry *search = cache_lookup(cache, fileData->file_name);
	if (search != NULL)
		return NULL;
	int evict = cache_evict(cache, fileData->file_size);
	if (evict == 0)
		return NULL;
	cache->curSize = cache->curSize + fileData->file_size;
	lru_list_insert_at_first(cache->lruOrder, fileData);
	cache_ht_entry *ret = cache_ht_insert(cache->hashTable, fileData);
	return ret;
}

cache_ht_entry *cache_lookup(server_cache *cache, char *fileName)
{
	return cache_ht_search(cache->hashTable, fileName);
}

int cache_evict(server_cache *cache, int size)
{
	if (cache->maxSize - cache->curSize < size)
	{
		int avaliableSize = cache->maxSize - cache->curSize;
		while (avaliableSize < size)
		{ // make sure there is enough space to evict
			lru_node *tail = cache->lruOrder->tail;
			if (tail == NULL) // empty lru list
				break;
			cache_ht_entry *tailFileData = cache_lookup(cache, tail->fileName);
			if (tailFileData->inUse > 0) // file data in use
				break;
			avaliableSize = avaliableSize + tailFileData->fileData->file_size;
			cache->curSize -= tailFileData->fileData->file_size;
			lru_list_remove_last(cache->lruOrder);
			cache_ht_delete(cache->hashTable, tailFileData->fileData);
		}
	}
	return 1;
}

/* --------------------------------------------------------------------------------------- */

/* hash function - djb2 - refer to http://www.cse.yorku.ca/~oz/hash.html */
int djb2(char *key)
{
	int hash = 2 * strlen(key) + 1;
	for (int i = 0; i < strlen(key); i++)
	{
		hash = hash * 33 + (int)key[i];
	}
	return hash % CACHE_TABLE_SIZE;
}

cache_hash_table *cache_ht_init(void)
{
	cache_hash_table *hashTable = (cache_hash_table *)Malloc(sizeof(cache_hash_table));
	hashTable->tableSize = 0;
	for (int i = 0; i < CACHE_TABLE_SIZE; i++)
	{
		hashTable->head[i] = NULL;
	}
	return hashTable;
}

void cache_ht_destroy(cache_hash_table *hashTable)
{
	cache_ht_entry **head = hashTable->head;
	for (int i = 0; i < CACHE_TABLE_SIZE; i++)
	{
		while (head[i] != NULL)
		{
			cache_ht_entry *temp = head[i];
			head[i] = head[i]->next;
			free(temp);
		}
	}
	free(hashTable);
	return;
}

cache_ht_entry *cache_ht_insert(cache_hash_table *hashTable, struct file_data *data)
{
	int index = djb2(data->file_name);
	cache_ht_entry *head = hashTable->head[index];
	cache_ht_entry *temp = Malloc(sizeof(struct cache_ht_entry));
	temp->fileData = file_data_init();
	temp->fileData->file_size = data->file_size;
	temp->fileData->file_name = strdup(data->file_name);
	temp->fileData->file_buf = strdup(data->file_buf);
	temp->inUse = 0;
	temp->next = head;
	head = temp;
	hashTable->head[index] = head;
	hashTable->tableSize++;
	return temp;
}

cache_ht_entry *cache_ht_search(cache_hash_table *hashTable, char *fileName)
{
	int index = djb2(fileName);
	cache_ht_entry *temp = hashTable->head[index];
	while (temp != NULL)
	{
		if (strcmp(temp->fileData->file_name, fileName) == 0)
		{
			return temp;
		}
		temp = temp->next;
	}
	return NULL;
}

int cache_ht_delete(cache_hash_table *hashTable, struct file_data *data)
{
	int index = djb2(data->file_name);
	cache_ht_entry *head = hashTable->head[index];
	if (head == NULL)
		return 0;
	if (strcmp(head->fileData->file_name, data->file_name) == 0)
	{
		cache_ht_entry *toDelete = head;
		head = head->next;
		hashTable->head[index] = head;
		file_data_free(toDelete->fileData);
		free(toDelete);
		hashTable->tableSize--;
		return 1;
	}
	if (head->next == NULL)
		return 0;
	else
	{
		cache_ht_entry *temp = cache_ht_search(hashTable, data->file_name);
		if (temp == NULL)
			return 0;
		cache_ht_entry *toDelete = temp->next;
		temp->next = temp->next->next;
		file_data_free(toDelete->fileData);
		free(toDelete);
		hashTable->tableSize--;
		return 1;
	}
}

/* --------------------------------------------------------------------------------------- */

lru_list *lru_list_init(void)
{
	lru_list *list = (lru_list *)Malloc(sizeof(lru_list));
	list->listSize = 0;
	list->head = NULL;
	list->tail = NULL;
	return list;
}

void lru_list_destroy(lru_list *list)
{
	while (list->head != NULL)
	{
		lru_node *temp = list->head;
		list->head = list->head->next;
		free(temp);
	}
	free(list);
	return;
}

lru_node *lru_list_search(lru_list *list, struct file_data *file_data)
{
	if (list == NULL || list->head == NULL)
		return NULL;
	else
	{
		lru_node *temp = list->head;
		while (temp != NULL)
		{
			if (strcmp(temp->fileName, file_data->file_name) == 0)
				return temp;
			temp = temp->next;
		}
		return NULL;
	}
}

void lru_list_insert_at_first(lru_list *list, struct file_data *file_data)
{
	lru_node *newNode = Malloc(sizeof(struct lru_node));
	newNode->fileName = strdup(file_data->file_name);
	if (list->head == NULL)
	{ // empty list
		list->head = newNode;
		list->tail = newNode;
		newNode->next = NULL;
		newNode->prev = NULL;
		list->listSize++;
		return;
	}
	else
	{ // non-empty list
		newNode->next = list->head;
		newNode->prev = NULL;
		list->head->prev = newNode;
		list->head = newNode;
		list->listSize++;
		return;
	}
}

int lru_list_remove_first(lru_list *list)
{
	if (list == NULL || list->head == NULL)
	{ // empty list
		return 0;
	}
	if (list->head->next == NULL)
	{ // one node in list
		free(list->head->fileName);
		free(list->head);
		list->head = NULL;
		list->tail = NULL;
		list->listSize--;
		return 1;
	}
	else
	{ // more than one node in list
		lru_node *toDelete = list->head;
		list->head = list->head->next;
		list->head->prev = NULL;
		free(toDelete->fileName);
		free(toDelete);
		list->listSize--;
		return 1;
	}
}

int lru_list_remove_last(lru_list *list)
{
	if (list == NULL || list->tail == NULL)
	{ // empty list
		return 0;
	}
	if (list->tail->prev == NULL)
	{ // one node in list
		free(list->tail->fileName);
		free(list->tail);
		list->head = NULL;
		list->tail = NULL;
		list->listSize--;
		return 1;
	}
	else
	{
		lru_node *toDelete = list->tail;
		list->tail = list->tail->prev;
		list->tail->next = NULL;
		list->listSize--;
		free(toDelete->fileName);
		free(toDelete);
		return 1;
	}
}

int lru_list_remove_one(lru_list *list, struct file_data *file_data)
{
	if (list == NULL || list->head == NULL)
		return 0;
	if (strcmp(list->head->fileName, file_data->file_name) == 0) // head
		return lru_list_remove_first(list);
	if (strcmp(list->tail->fileName, file_data->file_name) == 0) // tail
		return lru_list_remove_last(list);
	else
	{
		lru_node *toDelete = lru_list_search(list, file_data);
		toDelete->next->prev = toDelete->prev;
		toDelete->prev->next = toDelete->next;
		list->listSize--;
		free(toDelete->fileName);
		free(toDelete);
		return 1;
	}
}

int lru_list_move_to_head(lru_list *list, struct file_data *file_data)
{
	lru_node *temp = lru_list_search(list, file_data);
	if (temp == NULL)
		return 0;
	int ret = lru_list_remove_one(list, file_data);
	if (ret == 0)
		return 0;
	lru_list_insert_at_first(list, file_data);
	return 1;
}