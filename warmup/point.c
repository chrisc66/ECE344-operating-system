#include <assert.h>
#include "common.h"
#include "point.h"
#include "math.h"

void
point_translate(struct point *p, double x, double y)
{
	point_set(p, point_X(p) + x, point_Y(p) + y);
}

double
point_distance(const struct point *p1, const struct point *p2)
{
	double deltaX = point_X(p1)-point_X(p2);
	double deltaY = point_Y(p1)-point_Y(p2);
	return sqrt( pow(deltaX, 2) + pow(deltaY,2) );
}

int
point_compare(const struct point *p1, const struct point *p2)
{
	double p1Distance = sqrt( pow(point_X(p1), 2) + pow(point_Y(p1), 2));
	double p2Distance = sqrt( pow(point_X(p2), 2) + pow(point_Y(p2), 2));
	if (p1Distance == p2Distance)
		return 0;
	else if (p1Distance > p2Distance)
		return 1;
	else
		return -1;
}
