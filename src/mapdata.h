
/* dist: public */

#ifndef __MAPDATA_H
#define __MAPDATA_H

/** @file
 * the mapdata module manages the contents of lvl files. other modules
 * that need information about the location of objects on the map should
 * use it.
 *
 * internally, the map file is represented as a sparse array using a
 * two-dimensional trie structure. it uses about 200k per map, which is
 * 1/5 of the space a straight bitmap would use, but retains efficient
 * access speeds.
 */

/** return codes for GetTile() */
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


/** interface id for Imapdata */
#define I_MAPDATA "mapdata-5"

/** interface struct for Imapdata
 * you should use this to figure out what's going on in the map in a
 * particular arena.
 */
typedef struct Imapdata
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	/** finds the file currently as this arena's map.
	 * you should use this function and not try to figure out the map
	 * filename yourself based on arena settings.
	 * @param arena the arena whose map we want
	 * @param buf where to put the resulting filename
	 * @param buflen how big buf is
	 * @param mapname null if you're looking for an lvl, or the name of
	 * an lvz file.
	 * @return true if it could find a lvl or lvz file, buf will contain
	 * the result. false if it failed.
	 */
	int (*GetMapFilename)(Arena *arena, char *buf, int buflen, const char *mapname);
	/* pyint: arena, string out, int buflen, zstring -> int */

	/** returns the number of flags on the map in a particular arena. */
	int (*GetFlagCount)(Arena *arena);
	/* pyint: arena -> int */

	/** returns the contents of a single tile of a map. */
	enum map_tile_t (*GetTile)(Arena *arena, int x, int y);
	/* pyint: arena, int, int -> int */

#ifdef notyet

/* draft of new region interface */

#define STRTOU32(s) (*(u32*)#s)

/* region chunk types */
#define RCT_ISBASE          STRTOU32(rBSE)
#define RCT_NOANTIWARP      STRTOU32(rNAW)
#define RCT_NOWEAPONS       STRTOU32(rNWP)
#define RCT_NONOFLAGS       STRTOU32(rNFL)

	/* functions */
	Region * FindRegionByName(Arena *arena, const char *name);
	const char * RegionName(Region *reg);
	/* puts results in *datap and *sizep, if they are not NULL. returns
	 * true if found. so you can use as a boolean, like
	 * if (GetRegionChunk(reg, RCT_NOANTIWARP, NULL, NULL)) { ... } */
	int RegionChunk(Region *reg, u32 ctype, const void **datap, int *sizep);
	/* true if the point is contained */
	int Contains(Region *reg, int x, int y);
	/* note that you can pass a list as the closure arg and LLAdd as the
	 * callback here */
	void EnumContaining(Arena *arena, int x, int y,
			void (*cb)(void *clos, Region *reg), void *clos);
	/* NULL if there are none */
	Region * GetOneContaining(Arena *arena, int x, int y);

#endif

	/* the following three functions are in this module because of
	 * efficiency concerns. */

	/** finds the tile nearest to the given tile that is appropriate for
	 ** placing a flag. */
	void (*FindEmptyTileNear)(Arena *arena, int *x, int *y);

	/** calculates the placement of a brick of a given length dropped at
	 ** a certain position. direction is in ship graphic units: 0-39 */
	int (*FindBrickEndpoints)(Arena *arena, int brickmode, int dropx, int dropy, int direction,
			int length, int *x1, int *y1, int *x2, int *y2);

	u32 (*GetChecksum)(Arena *arena, u32 key);

	void (*DoBrick)(Arena *arena, int drop, int x1, int y1, int x2, int y2);
	/* only used from game */
} Imapdata;

#endif

