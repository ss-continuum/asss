
/* dist: public */

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "asss.h"

/* extra includes */
#include "sparse.inc"

/* region stuff, to be consistent with regiongen */
#include "region.h"

/* brick mode constants */
#include "settings/game.h"

#define MAXREGIONNAME 32

/* structs for packet types and data */
struct TileData
{
	u32 x : 12;
	u32 y : 12;
	u32 type : 8;
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
	pthread_mutex_t mtx;
};


/* prototypes */
local void ArenaAction(Arena *arena, int action);
local int read_lvl(char *name, struct MapData *md);
local HashTable * LoadRegions(char *fname);

/* interface funcs */
local int GetMapFilename(Arena *arena, char *buf, int buflen, const char *mapname);
local int GetFlagCount(Arena *arena);
local int GetTile(Arena *arena, int x, int y);
local const char *GetRegion(Arena *arena, int x, int y);
local int InRegion(Arena *arena, const char *region, int x, int y);
local void FindEmptyTileNear(Arena *arena, int *x, int *y);
local int FindBrickEndpoints(
		Arena *arena,
		int mode,
		int dropx,
		int dropy,
		int direction,
		int length,
		int *x1, int *y1, int *x2, int *y2);
local u32 GetChecksum(Arena *arena, u32 key);
local void DoBrick(Arena *arena, int drop, int x1, int y1, int x2, int y2);

/* global data */

local int mdkey;

/* cached interfaces */
local Imodman *mm;
local Iconfig *cfg;
local Iarenaman *aman;
local Ilogman *lm;
local Iprng *prng;


/* this module's interface */
local Imapdata _int =
{
	INTERFACE_HEAD_INIT(I_MAPDATA, "mapdata")
	GetMapFilename,
	GetFlagCount, GetTile,
	GetRegion, InRegion,
	FindEmptyTileNear, FindBrickEndpoints,
	GetChecksum, DoBrick
};



EXPORT int MM_mapdata(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		prng = mm->GetInterface(I_PRNG, ALLARENAS);
		if (!cfg || !aman || !lm || !prng) return MM_FAIL;

		mdkey = aman->AllocateArenaData(sizeof(struct MapData));
		if (mdkey == -1) return MM_FAIL;

		mm->RegCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);

		mm->RegInterface(&_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;

		mm->UnregCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);

		aman->FreeArenaData(mdkey);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(prng);

		return MM_OK;
	}
	return MM_FAIL;
}


int read_lvl(char *name, struct MapData *md)
{
	u16 bm;
	struct TileData td;
	int flags = 0, errors = 0;
	sparse_arr arr;

	FILE *f = fopen(name, "rb");
	if (!f) return 1;

	/* first try to skip over bmp header */
	fread(&bm, sizeof(bm), 1, f);
	if (bm == 0x4D42) /* "BM" */
	{
		u32 len;
		fread(&len, sizeof(len), 1, f);
		fseek(f, len, SEEK_SET);
	}
	else
		rewind(f);

	/* now read the map */
	arr = init_sparse();

	while (fread(&td, sizeof(td), 1, f))
	{
		if (td.x < 1024 && td.y < 1024)
		{
			if (td.type == TILE_TURF_FLAG)
				flags++;
			if (td.type < TILE_BIG_ASTEROID)
				insert_sparse(arr, td.x, td.y, td.type);
			else
			{
				int size = 1, x, y;
				if (td.type == TILE_BIG_ASTEROID)
					size = 2;
				else if (td.type == TILE_STATION)
					size = 6;
				else if (td.type == TILE_WORMHOLE)
					size = 5;
				for (x = 0; x < size; x++)
					for (y = 0; y < size; y++)
						insert_sparse(arr, td.x+x, td.y+y, td.type);
			}
		}
		else
			errors++;
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

#include "pathutil.h"

local int GetMapFilename(Arena *arena, char *buf, int buflen, const char *mapname)
{
	int islvl = 0;
	const char *t;
	struct replace_table repls[2] =
	{
		{'b', arena->basename},
		{'m', NULL},
	};

	if (!buf)
		return FALSE;
	if (!mapname)
		mapname = cfg->GetStr(arena->cfg, "General", "Map");

	if (mapname)
	{
		repls[1].with = mapname;

		t = strrchr(mapname, '.');
		if (t && strcmp(t, ".lvl") == 0)
			islvl = 1;
	}

	return find_file_on_path(
			buf,
			buflen,
			islvl ? CFG_LVL_SEARCH_PATH : CFG_LVZ_SEARCH_PATH,
			repls,
			repls[1].with ? 2 : 1) == 0;
}

void ArenaAction(Arena *arena, int action)
{
	struct MapData *md;
	
	md = P_ARENA_DATA(arena, mdkey);

	if (action == AA_CREATE)
		pthread_mutex_init(&md->mtx, NULL);
	else if (action == AA_DESTROY)
		pthread_mutex_destroy(&md->mtx);

	/* no matter what is happening, destroy old mapdata */
	if (action == AA_CREATE || action == AA_DESTROY)
	{
		if (md->arr)
		{
			delete_sparse(md->arr);
			md->arr = NULL;
		}

		if (md->regions)
		{
			HashEnum(md->regions, FreeRegion, NULL);
			HashFree(md->regions);
			md->regions = NULL;
		}
	}

	/* now, if we're creating, do it */
	if (action == AA_CREATE)
	{
		char mapname[256];
		pthread_mutex_lock(&md->mtx);
		if (GetMapFilename(arena, mapname, sizeof(mapname), NULL))
		{
			char *t;

			if (read_lvl(mapname, md))
				lm->LogA(L_ERROR, "mapdata", arena, "error processing map file '%s'",
						mapname);
			else
				lm->LogA(L_INFO, "mapdata", arena, "successfully processed map file '%s'",
						mapname);
			/* if extension == .lvl */
			t = strrchr(mapname, '.');
			if (t && strcmp(t, ".lvl") == 0)
			{
				/* change extension to rgn */
				t[1] = 'r'; t[2] = 'g'; t[3] = 'n';
				md->regions = LoadRegions(mapname);
			}
		}
		else
			lm->Log(L_ERROR, "<mapdata> {%s} can't find map file for arena",
					arena->name);
		pthread_mutex_unlock(&md->mtx);
	}
}


int GetTile(Arena *a, int x, int y)
{
	int ret;
	struct MapData *md = P_ARENA_DATA(a, mdkey);
	pthread_mutex_lock(&md->mtx);
	ret = md->arr ? lookup_sparse(md->arr, x, y) : -1;
	pthread_mutex_unlock(&md->mtx);
	return ret;
}

int GetFlagCount(Arena *a)
{
	return ((struct MapData*)P_ARENA_DATA(a, mdkey))->flags;
}


void FindEmptyTileNear(Arena *arena, int *x, int *y)
{
	/* init context. these values are funny because they are one
	 * iteration before where we really want to start from. */
	struct
	{
		enum { up, right, down, left } dir;
		int upto, remaining;
		int x, y;
	} ctx = { left, 0, 1, *x + 1, *y };
	struct MapData *md;
	sparse_arr arr;

	md = P_ARENA_DATA(arena, mdkey);
	pthread_mutex_lock(&md->mtx);

	arr = md->arr;

	if (!arr) goto failed_1;

	/* do it */
	for (;;)
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

		/* check if the tile is empty */
		if (lookup_sparse(arr, ctx.x, ctx.y))
		{
			if (ctx.upto < 35)
				continue;
			else
				goto failed_1;
		}

		/* return values */
		*x = ctx.x; *y = ctx.y;
		break;
	}

failed_1:
	pthread_mutex_unlock(&md->mtx);
}


int FindBrickEndpoints(
		Arena *arena,
		int brickmode,
		int dropx,
		int dropy,
		int direction,
		int length,
		int *x1, int *y1, int *x2, int *y2)
{
	struct MapData *md;
	sparse_arr arr;
	enum { up, right, down, left } dir;
	int bestcount, bestdir, x, y, destx = dropx, desty = dropy;

	md = P_ARENA_DATA(arena, mdkey);
	pthread_mutex_lock(&md->mtx);
	arr = md->arr;

	if (lookup_sparse(arr, dropx, dropy))
	{
		pthread_mutex_unlock(&md->mtx);
		return FALSE;
	}

	/* in the worst case, we can always just drop a single block */

	if (brickmode == BRICK_VIE)
	{
		/* find closest wall and the point next to it */
		bestcount = 3000;
		bestdir = -1;
		for (dir = 0; dir < 4; dir++)
		{
			int count = 0, oldx = dropx, oldy = dropy;
			x = dropx; y = dropy;

			while (x >= 0 && x < 1024 &&
			       y >= 0 && y < 1024 &&
			       lookup_sparse(arr, x, y) == 0 &&
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
			goto failed_2;
		}

		if (bestcount == length)
		{
			/* no closest wall */
			if (prng->Get32() & 1)
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
	}
	else if (brickmode == BRICK_AHEAD)
	{
		int count = 0, oldx = dropx, oldy = dropy;

		x = dropx; y = dropy;

		bestdir = ((direction + 5) % 40) / 10;

		while (x >= 0 && x < 1024 &&
		       y >= 0 && y < 1024 &&
		       lookup_sparse(arr, x, y) == 0 &&
		       count < length)
		{
			switch (bestdir)
			{
				case down:  oldy = y++; break;
				case right: oldx = x++; break;
				case up:    oldy = y--; break;
				case left:  oldx = x--; break;
			}
			count++;
		}

		destx = oldx; desty = oldy;
	}
	else if (brickmode == BRICK_LATERAL)
	{
		int count = 0, oldx = dropx, oldy = dropy;

		x = dropx; y = dropy;

		bestdir = ((direction + 15) % 40) / 10;

		while (x >= 0 && x < 1024 &&
		       y >= 0 && y < 1024 &&
		       lookup_sparse(arr, x, y) == 0 &&
		       count <= length / 2)
		{
			switch (bestdir)
			{
				case down:  oldy = y++; break;
				case right: oldx = x++; break;
				case up:    oldy = y--; break;
				case left:  oldx = x--; break;
			}
			count++;
		}

		destx = oldx; desty = oldy;
	}
	else if (brickmode == BRICK_CAGE)
	{
		/* generate drop coords inside map */
		int sx = dropx - (length) / 2,
			sy = dropy - (length) / 2,
			ex = dropx + (length + 1) / 2,
			ey = dropy + (length + 1) / 2;

		if (sx < 0) sx = 0;
		if (sy < 0) sy = 0;
		if (ex > 1023) ex = 1023;
		if (ey > 1023) ey = 1023;

		/* top */
		x = sx;
		y = sy;

		while (x <= ex && lookup_sparse(arr, x, y))
			++x;

		if (x > ex)
		{
			x1[0] = dropx;
			x2[0] = dropx;
			y1[0] = dropy;
			y2[0] = dropy;
		}
		else
		{
			x1[0] = x++;
			while (x <= ex && !lookup_sparse(arr, x, y))
				++x;
			x2[0] = x-1;
			y1[0] = y;
			y2[0] = y;
		}

		/* bottom */
		x = sx;
		y = ey;

		while (x <= ex && lookup_sparse(arr, x, y))
			++x;

		if (x > ex)
		{
			x1[1] = dropx;
			x2[1] = dropx;
			y1[1] = dropy;
			y2[1] = dropy;
		}
		else
		{
			x1[1] = x++;
			while (x <= ex && !lookup_sparse(arr, x, y))
				++x;
			x2[1] = x-1;
			y1[1] = y;
			y2[1] = y;
		}

		/* left */
		x = sx;
		y = sy;

		while (y <= ey && lookup_sparse(arr, x, y))
			++y;

		if (y > ey)
		{
			x1[2] = dropx;
			x2[2] = dropx;
			y1[2] = dropy;
			y2[2] = dropy;
		}
		else
		{
			y1[2] = y++;
			while (y <= ey && !lookup_sparse(arr, x, y))
				++y;
			y2[2] = y-1;
			x1[2] = x;
			x2[2] = x;
		}

		/* right */
		x = ex;
		y = sy;

		while (y <= ey && lookup_sparse(arr, x, y))
			++y;

		if (y > ey)
		{
			x1[3] = dropx;
			x2[3] = dropx;
			y1[3] = dropy;
			y2[3] = dropy;
		}
		else
		{
			y1[3] = y++;
			while (y <= ey && !lookup_sparse(arr, x, y))
				++y;
			y2[3] = y-1;
			x1[3] = x;
			x2[3] = x;
		}

		goto failed_2;
	}
	else
		goto failed_2;

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

	/* swap if necessary */
	if (*x1 > *x2)
	{
		x = *x1;
		*x1 = *x2;
		*x2 = x;
	}
	if (*y1 > *y2)
	{
		y = *y1;
		*y1 = *y2;
		*y2 = y;
	}

failed_2:
	pthread_mutex_unlock(&md->mtx);
	return TRUE;
}


void DoBrick(Arena *arena, int drop, int x1, int y1, int x2, int y2)
{
	unsigned char tile = drop ? TILE_BRICK : 0;
	sparse_arr arr;
	struct MapData *md = P_ARENA_DATA(arena, mdkey);

	pthread_mutex_lock(&md->mtx);
	arr = md->arr;

	if (x1 == x2)
	{
		int y;
		for (y = y1; y <= y2; y++)
			insert_sparse(arr, x1, y, tile);
	}
	else if (y1 == y2)
	{
		int x;
		for (x = x1; x <= x2; x++)
			insert_sparse(arr, x, y1, tile);
	}

	/* try to keep memory down */
	if (tile == 0)
		cleanup_sparse(arr);

	pthread_mutex_unlock(&md->mtx);
}


u32 GetChecksum(Arena *arena, u32 key)
{
	int x, y, savekey = (int)key;
	struct MapData *md;
	sparse_arr arr;

	md = P_ARENA_DATA(arena, mdkey);
	pthread_mutex_lock(&md->mtx);
	arr = md->arr;

	if (!arr) goto failed_3;

	for (y = savekey % 32; y < 1024; y += 32)
		for (x = savekey % 31; x < 1024; x += 31)
		{
			byte tile = lookup_sparse(arr, x, y);
			if ((tile >= TILE_START && tile <= TILE_END) || tile == TILE_SAFE)
				key += savekey ^ tile;
		}

failed_3:
	pthread_mutex_unlock(&md->mtx);
	return key;
}


/* region stuff below here */

HashTable * LoadRegions(char *fname)
{
	char buf[256];
	FILE *f = fopen(fname, "r");
	HashTable *hash = HashAlloc();
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
		fclose(f);
	}
	return hash;
}


const char *GetRegion(Arena *arena, int x, int y)
{
	return NULL;
}

int InRegion(Arena *arena, const char *region, int x, int y)
{
	struct Region *reg;
	struct MapData *md = P_ARENA_DATA(arena, mdkey);

	if (md->regions)
		if ((reg = HashGetOne(md->regions, region)))
		{
			int i;
			rect_t *r;
			for (i = 0, r = reg->rects.data; i < reg->rects.count; i++, r++)
				if (x >= r->x &&
				    (x - r->x) < r->w &&
				    y >= r->y &&
				    (y - r->y) < r->h)
					return 1;
		}

	return 0;
}


