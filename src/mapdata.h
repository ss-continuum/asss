
#ifndef __MAPDATA_H
#define __MAPDATA_H

/* Imapdata
 * the mapdata module manages the contents of lvl files. other modules
 * that need information about the location of objects on the map should
 * use it.
 * internally, the map file is represented as a sparse array using a
 * two-dimensional trie structure. it uses about 200k per map, which is
 * 1/5 of the space a straight bitmap would use, but retains efficient
 * access speeds.
 */


typedef struct Imapdata
{
	int (*GetMapFilename)(int arena, char *buffer, int bufferlen);
	/* gets the filename of the map file for the given arena. returns
	 * nonzero on error. */

	int (*GetFlagCount)(int arena);
	/* gets the number of turf flags on the map */

	int (*GetTile)(int arena, int x, int );
	/* returns the contents of the given tile. */

	/* the following two functions deal with the map region system. */

	char * (*GetRegion)(int arena, int x, int y);
	/* returns the region containing the given coordinates. only returns
	 * regions that specify IsBase to be true. */

	int (*ClipToRegion)(int arena, char *region, int *x, int *y);
	/* ensures the given coordinates are within the named region.
	 * returns 0 if the point was already in the region, 1 if it was
	 * clipped, and -1 if the region doesn't exist. */

	/* the following two functions are in this module because of
	 * efficiency concerns. */

	void (*FindFlagTile)(int arena, int *x, int *y);
	/* finds the tile nearest to the given tile that is appropriate for
	 * placing a flag (empty and accessible). */

	void (*FindBrickEndpoints)(int arena, int dropx, int dropy, int length, int *x1, int *y1, int *x2, int *y2);
	/* calculates the placement of a brick of a given length dropped at
	 * a certain position. */
} Imapdata;

#endif

