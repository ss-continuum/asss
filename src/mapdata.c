
#include <stdlib.h>
#include <stdio.h>

#include "asss.h"

/* extra includes */
#include "sparse.inc"


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
	char name[MAXREGIONNAME];
	int isbase;
	int x1, y1, x2, y2;
};


struct MapData
{
	sparse_arr arr;
	int flags, errors;
	LinkedList *regions;
};


/* prototypes */
local void ArenaAction(int arena, int action);
int read_lvl(char *name, struct MapData *md);

/* interface funcs */
local int GetMapFilename(int arena, char *buffer, int bufferlen);
local int GetFlagCount(int arena);
local int GetTile(int arena, int x, int y);
local char *GetRegion(int arena, int x, int y);
local int ClipToRegion(int arena, char *region, int *x, int *y);
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
{ GetMapFilename, GetFlagCount, GetTile, FindFlagTile, FindBrickEndpoints };



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
			Link *l;
			for (l = LLGetHead(mapdata[arena].regions); l; l = l->next)
				afree(l->data);
			LLFree(mapdata[arena].regions);
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
	struct SpiralContext
	{
		enum { down, right, up, left } dir;
		int upto, remaining;
		int x, y;
	} ctx;
	int good;
	sparse_arr arr = mapdata[arena].arr;

	if (!arr) return;

	/* init context. these values are funny because they are one
	 * iteration before where we really want to start from. */
	ctx.dir = left;
	ctx.upto = 0;
	ctx.remaining = 1;
	ctx.x = *x + 1;
	ctx.y = *y;
	good = 0;

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
}


/* region stuff below here */


LinkedList * LoadRegions(char *fname)
{
	LinkedList *lst;
	char mapn[PATH_MAX];

}

char *GetRegion(int arena, int x, int y)
{
	return NULL;
}

int ClipToRegion(int arena, char *region, int *x, int *y)
{
	/* returns 0 if the point was already in the region, 1 if it was
	 * clipped, and -1 if the region doesn't exist. */
	return -1;
}


