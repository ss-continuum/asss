
/* dist: public */

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

/* Return codes for GetTile() */
enum map_tile_t
{
	/* standard tile types */
	TILE_NONE           = 0,

	TILE_START          = 1,
	/* map borders are not included in the .lvl files */
	TILE_BORDER         = 20,
	TILE_END            = 161,
	/* tiles up to this point are part of security checksum */

	TILE_V_DOOR_START   = 162,
	TILE_V_DOOR_END     = 165,

	TILE_H_DOOR_START   = 166,
	TILE_H_DOOR_END     = 169,

	TILE_TURF_FLAG      = 170,

	/* only other tile included in security checksum */
	TILE_SAFE           = 171,

	TILE_GOAL           = 172,

	/* fly-over */
	TILE_OVER_START     = 173,
	TILE_OVER_END       = 175,

	/* fly-under */
	TILE_UNDER_START    = 176,
	TILE_UNDER_END      = 190,

	TILE_TINY_ASTEROID  = 216,
	TILE_TINY_ASTEROID2 = 217,
	TILE_BIG_ASTEROID   = 218,

	TILE_STATION        = 219,

	TILE_WORMHOLE       = 220,

	/* internal tile types */
	TILE_BRICK          = 250,
};


#define I_MAPDATA "mapdata-4"

typedef struct Imapdata
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	int (*GetMapFilename)(Arena *arena, char *buf, int buflen, const char *mapname);
	/* returns true if it could find a map and put the filename in buf.
	 * false if it couldn't find a map or buf wasn't big enough. mapname
	 * should be null unless you're looking for lvzs or something. */
	/* pyint: arena, string out, int buflen, string -> int */

	int (*GetFlagCount)(Arena *arena);
	/* gets the number of turf flags on the map */
	/* pyint: arena -> int */

	int (*GetTile)(Arena *arena, int x, int y);
	/* returns the contents of the given tile. */
	/* pyint: arena, int, int -> int */


	/* the following two functions deal with the map region system. */

	const char * (*GetRegion)(Arena *arena, int x, int y);
	/* returns the region containing the given coordinates. only returns
	 * regions that specify IsBase to be true. returns NULL if there is
	 * no named region covering that area. */

	int (*InRegion)(Arena *arena, const char *region, int x, int y);
	/* returns true if the given point is in the given region. */


	/* the following three functions are in this module because of
	 * efficiency concerns. */

	void (*FindEmptyTileNear)(Arena *arena, int *x, int *y);
	/* finds the tile nearest to the given tile that is appropriate for
	 * placing a flag. */

	int (*FindBrickEndpoints)(Arena *arena, int brickmode, int dropx, int dropy, int direction,
			int length, int *x1, int *y1, int *x2, int *y2);
	/* calculates the placement of a brick of a given length dropped at
	 * a certain position. direction is in ship graphic units: 0-39 */

	u32 (*GetChecksum)(Arena *arena, u32 key);

	void (*DoBrick)(Arena *arena, int drop, int x1, int y1, int x2, int y2);
	/* only used from game */
} Imapdata;

#endif

