#ifndef _SORTEDPOINTS_H_
#define _SORTEDPOINTS_H_
#include "point.h"

/* DO NOT CHANGE THIS FILE */

/* Forward declaration of structure for the function declarations below. */
struct sorted_points;

/* Initialize data structure, returning pointer to a new object that contains an
 * empty list. After that, new points are added to the list by calling
 * sp_add_point. The reason for allocating a structure that represents an empty
 * list is that it makes it easier to add or remove objects, especially from
 * the front of the list.*/
struct sorted_points *sp_init(void);

/* Destroy data structure. */
void sp_destroy(struct sorted_points *sp);

/* Allocate a new point and initialize it to x,y. Then add that point to the
 * struct sorted_points list. Return 1 on success and 0 on error (e.g., out of
 * memory). */

/* Note: Points are sorted by their distance from the origin (0,0). If two
 * points are the same distance form the origin, then the one with a smaller x
 * coordinate should appear before one with a larger one. If two points are the
 * same distance and have the same x coordinate, then the one with the smaller
 * y coordinate should appear first.
 *
 * e.g., the following order is legal:
 * (0,0), (0, 1), (1, 0), (-2, 0), (0, 2), (2, 0)
 */
int sp_add_point(struct sorted_points *sp, double x, double y);

/* Remove the first point from the sorted list.  Caller provides a pointer to a
 * Point (ret). This procedure stores the values of that point in *ret. Returns
 * 1 on success and 0 on failure (empty list). */
int sp_remove_first(struct sorted_points *sp, struct point *ret);

/* Remove the last point from the sorted list and store its value in
 * *ret. Returns 1 on success and 0 on failure (empty list). */
int sp_remove_last(struct sorted_points *sp, struct point *ret);

/* Remove the point that appears in position <index> on the sorted list, storing
 * its value in *ret. Returns 1 on success and 0 on failure (too short list).
 * Assume that index 0 refers to the first item in the list. Removing an element
 * from an empty list should always return a failure. */
int sp_remove_by_index(struct sorted_points *sp, int index, struct point *ret);

/* Delete any duplicate records for the sorted list. E.g., if two points on the
 * list have *identical* x and y values, then delete one of them.  Return the
 * number of records deleted. */
int sp_delete_duplicates(struct sorted_points *sp);

#endif
