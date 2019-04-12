#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "string.h"
#include "ctype.h"
#include "stdbool.h"
#include "common.h"
#include "wc.h"
#define MAX_LEN 150
#define WC_SIZE 100000
#define HASH 5381


struct word {
	char content[MAX_LEN];
	int count;
	struct word * next;
};

struct wc {
	struct word ** table;
};

/* hash function - djb2 
 * refer to http://www.cse.yorku.ca/~oz/hash.html */
long hash_function(char* word);

/* check for duplicate words */
bool word_duplicate(struct word* head, char word[MAX_LEN]);

struct wc *
wc_init(char *word_array, long size)
{
	/* create empty hash table data structure (array of pointer) */
	struct wc * wc = (struct wc *) malloc(sizeof(struct wc));
	assert(wc);
	wc->table = (struct word **)malloc(WC_SIZE * sizeof(struct word*));
	assert(wc->table);
	for (int i = 0; i < WC_SIZE; i++)
		wc->table[i] = NULL;
	
	/* extract words from input word array & put the word into hash table */
	for (long i = 0; i < size;     ){
		/* extract a word from word array */
		char newWord[MAX_LEN] = "";
		if (!isspace(word_array[i])){
			int j = 0;
			for (j = 0; !isspace(word_array[i+j]); j++)
				newWord[j] = word_array[i+j];
			newWord[j] = '\0';
			i = i + j;
		}
		else {
			i ++;
			continue;
		}

		/* insert new word into hash table */
		long key = hash_function(newWord);
		bool duplicate = word_duplicate(wc->table[key], newWord);
		if (!duplicate && (wc->table[key] == NULL)){			// empty list
			wc->table[key] = (struct word *)malloc(sizeof(struct word));
			assert(wc->table[key]);
			strcpy(wc->table[key]->content, newWord);
			wc->table[key]->count = 1;
			wc->table[key]->next = NULL;
		}
		else if (!duplicate && (wc->table[key] != NULL)){		// non empty list
			struct word * temp = wc->table[key];
			struct word * afterTemp = temp->next;
			while (afterTemp != NULL){
				temp = temp->next;
				afterTemp = temp->next;
			}
			temp->next = (struct word *)malloc(sizeof(struct word));
			afterTemp = temp->next;
			assert(afterTemp);
			strcpy(afterTemp->content, newWord);
			afterTemp->count = 1;
			afterTemp->next = NULL;
		}
		else													// duplicate
			continue;
	}
	return wc;
}

void
wc_output(struct wc *wc)
{
	struct word * temp;
	for (long i = 0; i < WC_SIZE; i++){
		temp = wc->table[i];
		while (temp != NULL){
			printf("%s:%d\n", temp->content, temp->count);
			temp = temp->next;
		}
	}
	return;
}

void
wc_destroy(struct wc *wc)
{
	for (long i = 0; i < WC_SIZE; i++){
		struct word * head = wc->table[i];
		struct word * temp = head;
		while (head != NULL){
			temp = head;
			head = head->next;
			free(temp);
		}
	}
	free(wc->table);
	free(wc);
	return;
}

long 
hash_function(char* word)
{
	char * firstChar = &word[0];
	unsigned long hash = HASH;
	int c;
	while ((c = *firstChar++))
		hash = ((hash << 5) + hash) + c; 	// hash * 33 + c
	return hash % WC_SIZE;
}

bool 
word_duplicate(struct word * head, char word[MAX_LEN])
{
	struct word * temp = head;
	while(temp != NULL){
		if (strcmp(temp->content, word) == 0){
			if (temp == head)
				head->count ++;
			else
				temp->count ++;
			return true;
		}
		temp = temp->next;
	}
	return false;
}