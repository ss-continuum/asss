
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


#define I_MAPDATA "mapdata-1"

typedef struct Imapdata
{
	INTERFACE_HEAD_DECL

	int (*GetFlagCount)(int arena);
	/* arpc: int(int) */
	/* gets the number of turf flags on the map */

	int (*GetTile)(int arena, int x, int y);
	/* arpc: int(int, int, int) */
	/* returns the contents of the given tile. */


	/* the following two functions deal with the map region system. */

	const char * (*GetRegion)(int arena, int x, int y);
	/* arpc: string(int, int, int) */
	/* returns the region containing the given coordinates. only returns
	 * regions that specify IsBase to be true. returns NULL if there is
	 * no named region covering that area. */

	int (*InRegion)(int arena, const char *region, int x, int y);
	/* arpc: int(int, string, int, int) */
	/* returns true if the given point is in the given region. */


	/* the following three functions are in this module because of
	 * efficiency concerns. */

	void (*FindFlagTile)(int arena, int *x, int *y);
	/* arpc: void(int, intptr, intptr) */
	/* finds the tile nearest to the given tile that is appropriate for
	 * placing a flag (empty and accessible). */

	void (*FindBrickEndpoints)(int arena, int dropx, int dropy, int length, int *x1, int *y1, int *x2, int *y2);
	/* arpc: void(int, int, int, int, intptr, intptr, intptr, intptr) */
	/* calculates the placement of a brick of a given length dropped at
	 * a certain position. */

	u32 (*GetChecksum)(int arena, u32 key);
	/* arpc: null */
} Imapdata;

#endif

