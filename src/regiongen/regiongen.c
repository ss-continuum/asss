
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>


/* bring in the sparse array code */
#include "sparse.inc"

/* the size of the map we are working with */
#define WIDTH 1024
#define HEIGHT 1024

#define TILE_FLAG 0xAA
#define TILE_SAFE 0xAB
#define TILE_GOAL 0xAC

#include "util.h"
#include "region.h"


/* structs for packet types and data */
struct tile
{
	unsigned x : 12;
	unsigned y : 12;
	unsigned type : 8;
};



/* reads a .lvl file into a sparse array of bytes */
sparse_arr read_lvl(char *name)
{
	sparse_arr arr;
	FILE *f = fopen(name,"r");
	if (!f) return NULL;

	{ /* first try to skip over bmp header */
		unsigned short bm;
		fread(&bm, sizeof(bm), 1, f);
		if (bm == 0x4D42)
		{
			unsigned long len;
			fread(&len, sizeof(len), 1, f);
			fseek(f, len, SEEK_SET);
		}
		else
			rewind(f);
	}

	{ /* now read the map */
		struct tile td;

		arr = init_sparse();

		while (fread(&td, sizeof(td), 1, f))
		{
			if (td.x < WIDTH && td.y < HEIGHT)
			{
				if (td.type < 0xD0)
					insert_sparse(arr, td.x, td.y, td.type);
				else
				{
					int size = 1, x, y;
					if (td.type == 0xD9)
						size = 2;
					else if (td.type == 0xDB)
						size = 6;
					else if (td.type == 0xDC)
						size = 5;
					for (x = 0; x < size; x++)
						for (y = 0; y < size; y++)
							insert_sparse(arr, td.x+x, td.y+y, td.type);
				}
			}
		}
	}

	return arr;
}



/* given an array and a starting coordinate, finds the corners of a
 * polygon marked by marker. */
coordlist_t find_corners(sparse_arr arr, int sx, int sy, int marker)
{
	enum direct { right = 0, down = 1, left = 2, up = 3 };
	int x = sx, y = sy, lastdir = -1;
	coordlist_t coords;

	assert(lookup_sparse(arr, x, y) == marker);

	coords = init_coordlist();

	do
	{
		int best = 3000, bestdir = -1, dir;
		int destx = -1, desty = -1;

		/* find how far away a marker is in each direction */
		for (dir = 0; dir < 4; dir++)
		{
			int tx = x, ty = y, dist = 0; /* start at the location */

			/* don't try backwards */
			if (dir == (lastdir ^ 2))
				continue;

			for (;;)
			{
				/* move one in the given direction */
				switch (dir)
				{
					case right: tx++; break;
					case down:  ty++; break;
					case left:  tx--; break;
					case up:    ty--; break;
				}
				dist++;

				/* check if we're off the edge of the map */
				if (tx < 0 || tx >= WIDTH || ty < 0 || ty >= HEIGHT)
				{
					dist = 5000;
					break;
				}

				/* check if we hit a marker */
				if (lookup_sparse(arr, tx, ty) == marker)
					break;
			}
			if (dist < best)
			{
				best = dist;
				bestdir = dir;
				destx = tx; desty = ty;
			}
		}

		/* add corner if we're not going straight */
		assert(bestdir != -1);
		if (bestdir != lastdir)
		{
			lastdir = bestdir;
			add_coord(&coords, x, y);
		}
		x = destx; y = desty;
	}
	while (x != sx || y != sy);

	add_coord(&coords, x, y);

	return coords;
}


	/* fills in the inside of a region, given an arbitrary edge in the
	 * array and a starting point. */
	static void fill_in_region(sparse_arr arr, int x, int y)
	{
		if (lookup_sparse(arr, x, y))
			return;
		insert_sparse(arr, x, y, 1);
		fill_in_region(arr, x+1, y);
		fill_in_region(arr, x-1, y);
		fill_in_region(arr, x, y+1);
		fill_in_region(arr, x, y-1);
	}

/* given a list of corners, returns an array of the points on the inside
 * of the polygon. */
sparse_arr find_inside(coordlist_t corners)
{
	coord_t topleft = { 3000, 3000 };
	sparse_arr arr = init_sparse();
	int x, y, c, dx, dy;

	x = corners.data[0].x;
	y = corners.data[0].y;

	/* fill in the edge of the region */
	/* also, find the upper-left-most point in the region */
	for (c = 1; c < corners.count; c++)
	{
		/* destination point */
		dx = corners.data[c].x;
		dy = corners.data[c].y;

		/* update topleft point */
		if ( (dx+dy) < (topleft.x+topleft.y) )
		{
			topleft.x = dx; topleft.y = dy;
		}

		/* move along the edge */
		if (x == dx)
		{
			if (y > dy)
				for ( ; y > dy; y--)
					insert_sparse(arr, x, y, 1);
			else if (y < dy)
				for ( ; y < dy; y++)
					insert_sparse(arr, x, y, 1);
			else
				assert(!"cannot have zero-length edge!");
		}
		else if (y == dy)
		{
			if (x > dx)
				for ( ; x > dx; x--)
					insert_sparse(arr, x, y, 1);
			else if (x < dx)
				for ( ; x < dx; x++)
					insert_sparse(arr, x, y, 1);
			else
				assert(!"cannot have zero-length edge!");
		}
		else
			assert(!"non-parallel edge!");

		/* just to be sure */
		x = dx; y = dy;
	}

	assert(topleft.x < WIDTH && topleft.y < HEIGHT);

	/* fill in the middle */
	fill_in_region(arr, topleft.x + 1, topleft.y + 1);

	return arr;
}


/* given an array of points on the inside of some region, returns an
 * initial list of rectangles whose union is the region. this works by
 * finding maximal vertical strips. */
rectlist_t gen_initial_rectlist(sparse_arr arr)
{
	int x;
	rectlist_t rectlist = init_rectlist();

	/* for each x, find all vertical stripts */
	for (x = 0; x < WIDTH; x++)
	{
		rect_t rect;
		int y = 0;

		rect.x = x;
		rect.w = 1;

		/* as long as there are strips left to find... */
		while (y < HEIGHT)
		{
			for ( ; y < HEIGHT; y++)
				if (lookup_sparse(arr, x, y))
					break;

			if (y >= HEIGHT)
				break;
			rect.y = y;

			for ( ; y < HEIGHT; y++)
				if (lookup_sparse(arr, x, y) == 0)
					break;

			rect.h = y - rect.y;

			add_rect(&rectlist, rect);
		}
	}

	return rectlist;
}


/* optimizes a list of rectanges that represent a region 

notes on the algorithm:

the basic idea is to take each vertical strip and extend it rightwards
as far as possible, until the strip boundaries change.

the first extension is to not stop extending the strip when the
boundaries change, but to keep extending it until it can't fit anymore.
then restart expanding at the first boundary change that was passed.

for that extension to be effective, we need a way of telling, for a
given rectangle, whether it is completely contained in another. then,
for each rectangle, we ask if it is contained in a previous one, and
omit it if it is. this will help with figures like this:

   +--+
   |  |
+--+  +--+
|        |
+--+  +--+
   |  |
   +--+

it will express them with two rectangles instead of three. it's not very
effective on much else, though.

the most promising extension seems like this:

from each strip boundary change, try extending in both directions: that
is, extend the right strip to the right and the left one to the left.
maintain all the rectangles generated. this will leave you with many
overlapping rectangles, but many will be superfluous. do an exhaustive
search to determine which subset to keep.

one strategy would be to branch on each rectangle: keep it or don't. the
search can be pruned at the first non-equivalent set.

another strategy is to branch once per drop: at each level, drop one of
the remaining rectangles. this would probably work well with a
breadth-first search.

 */
rectlist_t optimize_rectlist(rectlist_t rects)
{
	int *used, r;
	rectlist_t newrects = init_rectlist();

	/* the used array will mark rects ... */
	used = alloca(rects.count * sizeof(int));
	memset(used, 0, rects.count * sizeof(int));

	/* loop through rect list */
	for (r = 0; r < rects.count; r++)
	{
		int x, y, h, found, r2;
		rect_t rect;

		/* if this one was included in an expansion */
		if (used[r])
			continue;

		rect = rects.data[r];

		assert(rect.w == 1);

		x = rect.x + 1; /* target x */
		y = rect.y;
		h = rect.h;

		/* try to extend right */
		r2 = r;
		do
		{
			found = 0;
			for (r2++ ; r2 < rects.count; r2++)
			{
				if (rects.data[r2].y == y &&
					rects.data[r2].h == h &&
				    rects.data[r2].x == x)
				{
					found = r2;
					break;
				}
				if (rects.data[r2].x > (x+1))
				{
					/* we've gone too far.
					 * note: assumes they're ordered by x coordinate. */
					break;
				}
			}

			if (found)
			{
				/* if we found one, mark it used and increase the width
				 * of the current rect */
				used[found] = 1;
				rect.w++;
				x++; /* udpate target x value */
			}
		}
		while (found);

		add_rect(&newrects, rect);
	}

	return newrects;
}


/* locates all starting positions for regions in the array, find the
 * associated regions, and prints their coordinates. */
void find_and_print_all_regions(sparse_arr lvl, int marker, FILE *out)
{
	int x = -1, y = -1, regno = 0;

	fprintf(out, "%s\n", HEADER);

	do
	{
		int tx, ty;

		regno++;
		/* printf("Scanning map...\n"); */

		/* find a single marker */
		x = -1;
		for (ty = 0; ty < HEIGHT && x == -1; ty++)
			for (tx = 0; tx < WIDTH && x == -1; tx++)
				if (lookup_sparse(lvl, tx, ty) == marker)
				{
					x = tx; y = ty;
				}


		if (x != -1)
		{
			int x2, y2, r;
			coordlist_t corners;
			sparse_arr inside;
			rectlist_t initrects, goodrects;

			/* do the work */
			printf("Found region: "); fflush(stdout);
			corners = find_corners(lvl, x, y, marker);
			printf("<corners> "); fflush(stdout);
			inside = find_inside(corners);
			printf("<interior> "); fflush(stdout);
			initrects = gen_initial_rectlist(inside);
			printf("<rects> "); fflush(stdout);
			goodrects = optimize_rectlist(initrects);
			printf("<done>\n");

			fprintf(out,
					"\n"
					"; region %d: starts at (%d, %d) (%d rectangles)\n"
					"Name: region_%d\n"
					"; IsBase  (uncomment this line if this region is a base)\n",
					regno,
					x, y,
					goodrects.count,
					regno);
			for (r = 0; r < goodrects.count; r++)
			{
				char enc[10];
				encode_rectangle(enc, goodrects.data[r]);
				fprintf(out, "| %s\n", enc);
			}

			/* clear the things in "inside" from the lvl so they won't
			 * be found the next time around. */
			for (x2 = 0; x2 < WIDTH; x2++)
				for (y2 = 0; y2 < WIDTH; y2++)
					if (lookup_sparse(inside, x2, y2))
						insert_sparse(lvl, x2, y2, 0);

			/* free stuff */
			delete_rectlist(goodrects);
			delete_rectlist(initrects);
			delete_sparse(inside);
			delete_coordlist(corners);
		}
	}
	while (x != -1);
	printf("Done.\n");
}


int main(int argc, char *argv[])
{
	if (argc >= 2)
	{
		char *file = argv[1];
		sparse_arr lvl = read_lvl(file);
		if (!lvl)
		{
			printf("File %s not found\n", argv[1]);
			exit(2);
		}
		else
		{
			char *t = strstr(file, ".lvl");
			if (t)
			{
				FILE *out;

				t[1] = 'r'; t[2] = 'g'; t[3] = 'n';
				out = fopen(file, "w");
				if (out)
				{
					find_and_print_all_regions(lvl, TILE_FLAG, out);
					exit(0);
				}
				else
				{
					printf("Can't open %s for writing\n", file);
					exit(4);
				}
			}
			else
			{
				printf("Filename must end in .lvl\n");
				exit(3);
			}
		}
	}
	else
	{
		printf("usage: %s map.lvl\n", argv[0]);
		exit(1);
	}
}


