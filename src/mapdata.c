
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "asss.h"

/* extra includes */
#include "sparse.inc"

/* region stuff, to be consistent with regiongen */
#include "region.h"


#define TILE_FLAG 0xAA
#define TILE_SAFE 0xAB
#define TILE_GOAL 0xAC

#define MAXREGIONNAME 32

/* structs for packet types and data */
struct TileData
{
	unsigned x : 12;
	unsigned y : 12;
	unsigned type : 8;
};


struct Region
{
	int isbase;
	rectlist_t rects;
};


struct MapData
{
	sparse_arr arr;
	int flags, errors;
	HashTable *regions;
};


/* prototypes */
local void ArenaAction(int arena, int action);
local int read_lvl(char *name, struct MapData *md);
local HashTable * LoadRegions(char *fname);

/* interface funcs */
local int GetMapFilename(int arena, char *buffer, int bufferlen);
local int GetFlagCount(int arena);
local int GetTile(int arena, int x, int y);
local char *GetRegion(int arena, int x, int y);
local int InRegion(int arena, char *region, int x, int y);
local void FindFlagTile(int arena, int *x, int *y);
local void FindBrickEndpoints(int arena, int dropx, int dropy, int length, int *x1, int *y1, int *x2, int *y2);

/* global data */

local struct MapData mapdata[MAXARENA];

/* cached interfaces */
local Imodman *mm;
local Iconfig *cfg;
local Iarenaman *aman;
local Ilogman *log;


/* this module's interface */
local Imapdata _int =
{
	GetMapFilename, GetFlagCount, GetTile,
	GetRegion, InRegion,
	FindFlagTile, FindBrickEndpoints
};



int MM_mapdata(int action, Imodman *_mm, int arenas)
{
	if (action == MM_LOAD)
	{
		mm = _mm;
		mm->RegInterest(I_CONFIG, &cfg);
		mm->RegInterest(I_ARENAMAN, &aman);
		mm->RegInterest(I_LOGMAN, &log);

		mm->RegCallback(CALLBACK_ARENAACTION, ArenaAction, ALLARENAS);

		mm->RegInterface(I_MAPDATA, &_int);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterface(I_MAPDATA, &_int);

		mm->UnregCallback(CALLBACK_ARENAACTION, ArenaAction, ALLARENAS);

		mm->UnregInterest(I_LOGMAN, &log);
		mm->UnregInterest(I_ARENAMAN, &aman);
		mm->UnregInterest(I_CONFIG, &cfg);
		return MM_OK;
	}
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
	return MM_FAIL;
}


int read_lvl(char *name, struct MapData *md)
{
	int flags = 0, errors = 0;
	sparse_arr arr;
	FILE *f = fopen(name,"r");
	if (!f) return 1;

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
		struct TileData td;

		arr = init_sparse();

		while (fread(&td, sizeof(td), 1, f))
		{
			if (td.x < 1024 && td.y < 1024)
			{
				if (td.type == TILE_FLAG)
					flags++;
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
			else
				errors++;
		}
	}

	fclose(f);
	md->arr = arr;
	md->flags = flags;
	md->errors = errors;
	return 0;
}


local void FreeRegion(char *k, void *v, void *d)
{
	struct Region *r = (struct Region *)v;
	delete_rectlist(r->rects);
	afree(r);
}

void ArenaAction(int arena, int action)
{
	/* no matter what is happening, destroy old mapdata */
	if (action == AA_CREATE || action == AA_DESTROY)
	{
		if (mapdata[arena].arr)
		{
			delete_sparse(mapdata[arena].arr);
			mapdata[arena].arr = NULL;
		}

		if (mapdata[arena].regions)
		{
			HashEnum(mapdata[arena].regions, FreeRegion, NULL);
			HashFree(mapdata[arena].regions);
			mapdata[arena].regions = NULL;
		}
	}

	/* now, if we're creating, do it */
	if (action == AA_CREATE)
	{
		char mapname[256];
		if (GetMapFilename(arena, mapname, 256))
			log->Log(L_ERROR, "<mapdata> {%s} Can't find map file for arena",
					aman->arenas[arena].name);
		else
		{
			char *t;

			if (read_lvl(mapname, mapdata + arena))
				log->Log(L_ERROR, "<mapdata> {%s} Error parsing map file '%s'",
						aman->arenas[arena].name, mapname);
			/* if extension == .lvl */
			t = strrchr(mapname, '.');
			if (t && t[1] == 'l' && t[2] == 'v' && t[3] == 'l' && t[4] == 0)
			{
				/* change extension to rgn */
				t[1] = 'r'; t[2] = 'g'; t[3] = 'n';
				mapdata[arena].regions = LoadRegions(mapname);
			}
		}
	}
}


#include "pathutil.h"

int GetMapFilename(int arena, char *buffer, int bufferlen)
{
	struct replace_table repls[2];
	char *map, *searchpath;

	map = cfg->GetStr(aman->arenas[arena].cfg, "General", "Map");
	if (!map) return -1;

	repls[0].repl = 'a';
	repls[0].with = aman->arenas[arena].name;
	repls[1].repl = 'm';
	repls[1].with = map;

	searchpath = cfg->GetStr(GLOBAL, "General", "MapSearchPath");
	if (!searchpath)
		searchpath = DEFAULTMAPSEARCHPATH;

	return find_file_on_path(
			buffer,
			bufferlen,
			searchpath,
			repls,
			2);
}

int GetTile(int arena, int x, int y)
{
	if (mapdata[arena].arr)
		return (int)lookup_sparse(mapdata[arena].arr, x, y);
	else
		return -1;
}

int GetFlagCount(int arena)
{
	return mapdata[arena].flags;
}


void FindFlagTile(int arena, int *x, int *y)
{
	/* init context. these values are funny because they are one
	 * iteration before where we really want to start from. */
	struct SpiralContext
	{
		enum { down, right, up, left } dir;
		int upto, remaining;
		int x, y;
	} ctx = { left, 0, 1, *x + 1, *y };
	int good = 0;
	sparse_arr arr = mapdata[arena].arr;

	if (!arr) return;

	/* do it */
	do
	{
		/* move 1 in current dir */
		switch (ctx.dir)
		{
			case down:  ctx.y++; break;
			case right: ctx.x++; break;
			case up:    ctx.y--; break;
			case left:  ctx.x--; break;
		}
		ctx.remaining--;
		/* if we're at the end of the line */
		if (ctx.remaining == 0)
		{
			ctx.dir = (ctx.dir + 1) % 4;
			if (ctx.dir == 0 || ctx.dir == 2)
				ctx.upto++;
			ctx.remaining = ctx.upto;
		}

		/* check if it's ok */
		good = 1;
		/* check if the tile is empty */
		if (lookup_sparse(arr, ctx.x, ctx.y))
			good = 0;
		/* check if it's surrounded on top and bottom */
		if (lookup_sparse(arr, ctx.x, ctx.y + 1) &&
		    lookup_sparse(arr, ctx.x, ctx.y - 1))
			good = 0;
		/* check if it's surrounded on left and right */
		if (lookup_sparse(arr, ctx.x + 1, ctx.y) &&
		    lookup_sparse(arr, ctx.x - 1, ctx.y))
			good = 0;
	}
	while (!good && ctx.upto < 35);

	if (good)
	{
		/* return values */
		*x = ctx.x; *y = ctx.y;
	}
}


void FindBrickEndpoints(int arena, int dropx, int dropy, int length, int *x1, int *y1, int *x2, int *y2)
{
	sparse_arr arr = mapdata[arena].arr;
	enum { down, right, up, left } dir;
	int bestcount, bestdir, x, y, destx, desty;

	if (lookup_sparse(arr, dropx, dropy))
	{
		/* we can't drop it on a wall! */
		*x1 = *x2 = dropx;
		*y1 = *y2 = dropy;
		return;
	}

	/* find closest wall and the point next to it */
	bestcount = 3000;
	bestdir = -1;
	for (dir = 0; dir < 4; dir++)
	{
		int count = 0, oldx = dropx, oldy = dropy;
		x = dropx; y = dropy;

		while (lookup_sparse(arr, x, y) == 0 &&
		       x >= 0 && x < 1024 &&
		       y >= 0 && y < 1024 &&
		       count < length)
		{
			switch (dir)
			{
				case down:  oldy = y++; break;
				case right: oldx = x++; break;
				case up:    oldy = y--; break;
				case left:  oldx = x--; break;
			}
			count++;
		}

		if (count < bestcount)
		{
			bestcount = count;
			bestdir = dir;
			destx = oldx; desty = oldy;
		}
	}

	if (bestdir == -1)
	{
		/* shouldn't happen */
		*x1 = *x2 = dropx;
		*y1 = *y2 = dropy;
		return;
	}

	if (bestcount == length)
	{
		/* no closest wall */
		if (rand() & 0x800)
		{
			destx = dropx - length / 2;
			desty = dropy;
			bestdir = left;
		}
		else
		{
			destx = dropx;
			desty = dropy - length / 2;
			bestdir = up;
		}
	}

	/* enter first coordinate */
	dropx = x = *x1 = destx; dropy = y = *y1 = desty;

	/* go from closest point */
	switch (bestdir)
	{
		case down:
			while (lookup_sparse(arr, x, y) == 0 &&
			       (dropy - y) < length &&
			       y >= 0)
				desty = y--;
			break;

		case right:
			while (lookup_sparse(arr, x, y) == 0 &&
			       (dropx - x) < length &&
			       x >= 0)
				destx = x--;
			break;

		case up:
			while (lookup_sparse(arr, x, y) == 0 &&
			       (y - dropy) < length &&
			       y < 1024)
				desty = y++;
			break;

		case left:
			while (lookup_sparse(arr, x, y) == 0 &&
			       (x - dropx) < length &&
			       x < 1024)
				destx = x++;
			break;
	}

	/* enter second coordinate */
	*x2 = destx; *y2 = desty;
}


/* region stuff below here */

HashTable * LoadRegions(char *fname)
{
	char buf[256];
	FILE *f = fopen(fname, "r");
	HashTable *hash = HashAlloc(17);
	struct Region *reg = NULL;

	if (f)
	{
		fgets(buf, 256, f);
		RemoveCRLF(buf);
		if (strcmp(buf, HEADER))
		{
			HashFree(hash);
			return NULL;
		}

		while (fgets(buf, 256, f))
		{
			RemoveCRLF(buf);
			if (buf[0] == ';' || buf[0] == 0)
				continue;
			else if (buf[0] == '|')
			{
				/* data coming */
				rect_t r;
				if (!reg) continue;
				r = decode_rectangle(buf+2);
				add_rect(&reg->rects, r);
			}
			else if (!strncasecmp(buf, "name", 4))
			{
				/* new region */
				char *t = buf + 4;
				while (*t == ':' || *t == ' ' || *t == '\t') t++;

				/* get new */
				reg = amalloc(sizeof(struct Region));
				reg->isbase = 0;
				reg->rects = init_rectlist();
				HashAdd(hash, t, reg);
			}
			else if (!strncasecmp(buf, "isbase", 6))
			{
				reg->isbase = 1;
			}
		}
	}
	return hash;
}


char *GetRegion(int arena, int x, int y)
{
	return NULL;
}

int InRegion(int arena, char *region, int x, int y)
{
	struct Region *reg = HashGetOne(mapdata[arena].regions, region);

	if (!reg)
		return 0;
	else
	{
		int i;
		rect_t *r;
		for (i = 0, r = reg->rects.data; i < reg->rects.count; i++, r++)
			if (x >= r->x &&
			    (x - r->x) < r->w &&
			    y >= r->y &&
			    (y - r->y) < r->h)
				return 1;
		return 0;
	}
}


