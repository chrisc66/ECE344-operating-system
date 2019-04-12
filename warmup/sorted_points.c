#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#include "point.h"
#include "sorted_points.h"
#include "stdbool.h"

/* this structure stores each node inside the sorted list*/
struct node {
	struct point * point;
	struct node * next;
};

/* this structure should store all the points in a list in sorted order. */
struct sorted_points {
	struct node * head;
};

/* think about where you are going to store a pointer to the next element of the
 * linked list? if needed, you may define other structures. */

struct sorted_points *
sp_init()
{
	struct sorted_points *sp;

	sp = (struct sorted_points *)malloc(sizeof(struct sorted_points));
	assert(sp);

	sp->head = NULL;

	return sp;
}

void
sp_destroy(struct sorted_points *sp)
{
	if (sp != NULL) {
		while (sp->head != NULL) {
			struct node * temp = sp->head;
			sp->head = sp->head->next;
			free(temp->point);
			free(temp);
		}
		free(sp);
		sp = NULL;
	}
	return; 
}

int
sp_add_point(struct sorted_points *sp, double x, double y)
{
	// create new sorted point
	struct node * newNode;
	newNode = (struct node *)malloc(sizeof(struct node));
	assert(newNode);
	struct point * newPoint;
	newPoint = (struct point *)malloc(sizeof(struct point));
	assert(newPoint);
	point_set(newPoint, x, y);
	newNode->point = newPoint;
	newNode->next = NULL;

	if (sp == NULL || newNode == NULL || newPoint == NULL){	// failure
		return 0;
	}

	else if (sp->head == NULL){								// empty list
		sp->head = newNode;
		newNode->next = NULL;
		return 1;
	}

	else if (point_compare(newNode->point, sp->head->point) == -1){	// non empty list
		// new - ori head
		struct node * temp = sp->head;
		sp->head = newNode;
		newNode->next = temp;
		return 1;
	}

	else {													// non empty list
		struct node * temp = sp->head;
		struct node * after = sp->head->next;
		while (after != NULL && point_compare(newNode->point, after->point) == 1){
			temp = temp->next;
			after = after->next;
		}
		if (after == NULL){
			// temp - new - after(NULL)
			temp->next = newNode;
			newNode->next = NULL;
		}
		else if (point_compare(newNode->point, after->point) == 0){
			if (newNode->point->x < after->point->x){
				// temp - new - after
				temp->next = newNode;
				newNode->next = after;
			}
			else {
				// temp - after - new
				struct node * aaaa = after->next;
				after->next = newNode;
				newNode->next = aaaa;
			}
		}
		else {
			// temp - new - after
			temp->next = newNode;
			newNode->next = after;
		}
		return 1;
	}

}

int
sp_remove_first(struct sorted_points *sp, struct point *ret)
{
	if (sp == NULL || sp->head == NULL){	// empty list
		ret = NULL;
		return 0;
	}
	else {									// non empty list
		struct node * temp = sp->head;
		sp->head = sp->head->next;
		point_set(ret, temp->point->x, temp->point->y);
		free(temp->point);
		free(temp);
		return 1;	
	}
}

int
sp_remove_last(struct sorted_points *sp, struct point *ret)
{
	if (sp == NULL || sp->head == NULL){					// empty list
		ret = NULL;
		return 0;
	}
	else if (sp->head != NULL && sp->head->next == NULL) {	// non empty list, one node
		struct node * temp = sp->head;
		point_set(ret, temp->point->x, temp->point->y);
		free(sp->head->point);
		free(sp->head);
		sp->head = NULL;
	}
	else {													// non empty list, more than one node
		struct node * temp = sp->head;
		struct node * after = temp->next;
		while (after->next != NULL){
			temp = temp->next;
			after = temp->next;
		}
		temp->next = NULL;
		point_set(ret, after->point->x, after->point->y);
		free(after->point);
		free(after);
	}
	return 1; 
}

int
sp_remove_by_index(struct sorted_points *sp, int index, struct point *ret)
{
	if (sp == NULL || sp->head == NULL)
		return 0;

	if (index == 0)
		return sp_remove_first(sp, ret);

	struct node * temp = sp->head;
	struct node * after = sp->head->next;
	for (int i = 1; i < index; i ++){
		if (after->next == NULL || temp->next == NULL)
			return 0;
		temp = temp->next;
		after = after->next;
	}
	temp->next = after->next;
	point_set(ret, after->point->x, after->point->y);
	free(after->point);
	free(after);
	return 1;
}

int
sp_delete_duplicates(struct sorted_points *sp)
{
	if (sp == NULL || sp->head == NULL || sp->head->next == NULL)
		return 0;
	
	int count = 0;
	struct node * temp = sp->head;
	struct node * after = sp->head->next;
	while (after != NULL){
		if (temp->point->x == after->point->x && temp->point->y == after->point->y){
			temp->next = after->next;
			free(after->point);
			free(after);
			after = temp->next;
			count ++;
			continue;
		}
		temp = after;
		after = after->next;
	}
	return count;
}

// void print_sorted_points (struct sorted_points *sp){
// 	printf("Printing Linked List \n");
// 	struct node * temp = sp->head;
// 	int index = 0;
// 	while (temp != NULL){
// 		printf("%d, x = %lf, y = %lf \n",index, point_X(temp->point), point_Y(temp->point));
// 		index ++;
// 		temp = temp->next;
// 	}
// 	return;
// }