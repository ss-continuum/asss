
/* dist: public */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#ifndef WIN32
#include <unistd.h>
#include <sys/mman.h>
#else
#include <io.h>
#endif

#include "zlib.h"

#include "asss.h"


struct MapDownloadData
{
	u32 checksum, uncmplen, cmplen;
	int optional;
	byte *cmpmap;
	char filename[20];
};

struct data_locator
{
	Arena *arena;
	int lvznum, wantopt;
	u32 len;
};


/* GLOBALS */

local Imodman *mm;
local Iplayerdata *pd;
local Iconfig *cfg;
local Inet *net;
local Ilogman *lm;
local Iarenaman *aman;
local Imainloop *ml;

local int dlkey;

local const char *cfg_newsfile;
local u32 newschecksum, cmpnewssize;
local byte *cmpnews;
local time_t newstime;


/* functions */

local int RefreshNewsTxt(void *dummy)
{
	int fd, fsize;
	time_t newtime;
	uLong csize;
	byte *news, *cnews;
	struct stat st;

	fd = open(cfg_newsfile, O_RDONLY);
	if (fd == -1)
	{
		lm->Log(L_WARN,"<mapnewsdl> News file '%s' not found in current directory", cfg_newsfile);
		return 1; /* let's get called again in case the file's been replaced */
	}

	/* find it's size */
	fstat(fd, &st);
	newtime = st.st_mtime + st.st_ctime;
	if (newtime != newstime)
	{
#ifdef WIN32
		HANDLE hfile, hnews;
#endif
		newstime = newtime;
		fsize = st.st_size;
		csize = (uLong)(1.0011 * fsize + 35);

		/* mmap it */
#ifndef WIN32
		news = mmap(NULL, fsize, PROT_READ, MAP_SHARED, fd, 0);
		if (news == (void*)-1)
		{
			lm->Log(L_ERROR,"<mapnewsdl> mmap failed in RefreshNewsTxt");
			close(fd);
			return 1;
		}
#else
		hfile = CreateFile(cfg_newsfile,
				GENERIC_READ,
				FILE_SHARE_READ,
				NULL,
				OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
				0);
		if (hfile == INVALID_HANDLE_VALUE)
		{
			lm->Log(L_ERROR,"<mapnewsdl> CreateFile failed in RefreshNewsTxt, error %d",
					GetLastError());
			return 1;
		}
		hnews = CreateFileMapping(hfile,NULL,PAGE_READONLY,0,0,0);
		if (!hnews)
		{
			CloseHandle(hfile);
			lm->Log(L_ERROR,"<mapnewsdl> CreateFileMapping failed in RefreshNewsTxt, error %d",
					GetLastError());
			return 1;
		}
		news = MapViewOfFile(hnews,FILE_MAP_READ,0,0,0);
		if (!news)
		{
			CloseHandle(hnews);
			CloseHandle(hfile);
			lm->Log(L_ERROR,"<mapnewsdl> mapviewoffile failed in RefreshNewsTxt, error %d", GetLastError());
			return 1;
		}
#endif

		/* calculate crc on mmap'd map */
		newschecksum = crc32(crc32(0, Z_NULL, 0), news, fsize);

		/* allocate space for compressed version */
		cnews = amalloc(csize);

		/* set up packet header */
		cnews[0] = S2C_INCOMINGFILE;
		/* 16 bytes of zero for the name */

		/* compress the stuff! */
		compress(cnews+17, &csize, news, fsize);

		/* shrink the allocated memory */
		cnews = realloc(cnews, csize+17);
		if (!cnews)
		{
			lm->Log(L_ERROR,"<mapnewsdl> realloc failed in RefreshNewsTxt");
			close(fd);
			return 1;
		}
		cmpnewssize = csize+17;

#ifndef WIN32
		munmap(news, fsize);
#else
		UnmapViewOfFile(news);
		CloseHandle(hnews);
		CloseHandle(hfile);
#endif

		if (cmpnews) afree(cmpnews);
		cmpnews = cnews;
		lm->Log(L_DRIVEL,"<mapnewsdl> News file '%s' reread", cfg_newsfile);
	}
	close(fd);
	return 1;
}


local u32 GetNewsChecksum(void)
{
	if (!cmpnews)
		RefreshNewsTxt(0);
	return newschecksum;
}


#include "packets/mapfname.h"

local void SendMapFilename(Player *p)
{
	struct MapFilename *mf;
	LinkedList *dls;
	struct MapDownloadData *data;
	int len, wantopt = WANT_ALL_LVZ(p);
	Arena *arena;

	arena = p->arena;
	if (!arena) return;

	dls = P_ARENA_DATA(arena, dlkey);
	if (LLIsEmpty(dls))
	{
		lm->LogA(L_WARN, "<mapnewsdl>", arena, "Missing map data");
		return;
	}

	if (p->type != T_CONT)
	{
		data = LLGetHead(dls)->data;
		mf = alloca(21);

		strncpy(mf->files[0].filename, data->filename, 16);
		mf->files[0].checksum = data->checksum;
		len = 21;
	}
	else
	{
		int idx = 0;
		Link *l;

		/* allocate for the maximum possible */
		mf = alloca(sizeof(mf->files[0]) * LLCount(dls));

		for (l = LLGetHead(dls); l; l = l->next)
		{
			data = l->data;
			if (!data->optional || wantopt)
			{
				strncpy(mf->files[idx].filename, data->filename, 16);
				mf->files[idx].checksum = data->checksum;
				mf->files[idx].size = data->uncmplen;
				idx++;
			}
		}
		len = 1 + sizeof(mf->files[0]) * idx;
	}

	mf->type = S2C_MAPFILENAME;
	net->SendToOne(p, (byte*)mf, len, NET_RELIABLE);
}



local struct MapDownloadData * compress_map(const char *fname, int docomp)
{
	byte *map, *cmap;
	int mapfd, fsize;
	uLong csize;
	struct stat st;
	const char *mapname;
#ifdef WIN32
	HANDLE hfile, hmap;
#endif
	struct MapDownloadData *data;

	data = amalloc(sizeof(*data));

	/* get basename */
	mapname = strrchr(fname, '/');
	if (!mapname)
		mapname = fname;
	else
		mapname++;
	astrncpy(data->filename, mapname, 20);

	mapfd = open(fname, O_RDONLY);
	if (mapfd == -1)
		goto fail1;

	/* find it's size */
	fstat(mapfd, &st);
	fsize = st.st_size;
	if (docomp)
		csize = (uLong)(1.0011 * fsize + 35);
	else
		csize = fsize + 17;

	/* mmap it */
#ifndef WIN32
	map = mmap(NULL, fsize, PROT_READ, MAP_SHARED, mapfd, 0);
	if (map == (void*)-1)
	{
		lm->Log(L_ERROR,"<mapnewsdl> mmap failed for map '%s'", fname);
		close(mapfd);
		goto fail1;
	}
#else
	hfile = CreateFile(fname,
			GENERIC_READ,
			FILE_SHARE_READ,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
			0);
	if (hfile == INVALID_HANDLE_VALUE)
	{
		lm->Log(L_ERROR,"<mapnewsdl> CreateFile failed for map '%s', error %d",
				fname, GetLastError());
		goto fail1;
	}
	hmap = CreateFileMapping(hfile,NULL,PAGE_READONLY,0,0,0);
	if (!hmap)
	{
		CloseHandle(hfile);
		lm->Log(L_ERROR,"<mapnewsdl> CreateFileMapping failed for map '%s', error %d",
				fname, GetLastError());
		goto fail1;
	}
	map = MapViewOfFile(hmap,FILE_MAP_READ,0,0,0);
	if (!map)
	{
		CloseHandle(hmap);
		CloseHandle(hfile);
		lm->Log(L_ERROR,"<mapnewsdl> MapViewOfFile failed for map '%s'", fname);
		goto fail1;
	}
#endif

	/* calculate crc on mmap'd map */
	data->checksum = crc32(crc32(0, Z_NULL, 0), map, fsize);
	data->uncmplen = fsize;

	/* allocate space for compressed version */
	cmap = amalloc(csize);

	/* set up packet header */
	cmap[0] = S2C_MAPDATA;
	strncpy(cmap+1, mapname, 16);

	if (docomp)
	{
		/* compress the stuff! */
		compress(cmap+17, &csize, map, fsize);
		csize += 17;

		/* shrink the allocated memory */
		data->cmpmap = realloc(cmap, csize);
		if (data->cmpmap == NULL)
		{
			lm->Log(L_ERROR,"<mapnewsdl> realloc failed in compress_map");
			free(cmap);
			goto fail1;
		}
	}
	else
	{
		/* just copy */
		memcpy(cmap+17, map, fsize);
		data->cmpmap = cmap;
	}

	data->cmplen = csize;

#ifndef WIN32
	munmap(map, fsize);
#else
	UnmapViewOfFile(map);
	CloseHandle(hmap);
	CloseHandle(hfile);
#endif
	close(mapfd);

	return data;

fail1:
	afree(data);
	return NULL;
}


/* call with arena data lock */
local void free_maps(Arena *arena)
{
	LinkedList *dls;
	Link *l;

	dls = P_ARENA_DATA(arena, dlkey);
	for (l = LLGetHead(dls); l; l = l->next)
	{
		struct MapDownloadData *data = l->data;
		afree(data->cmpmap);
		afree(data);
	}
	LLEmpty(dls);
}


#include "pathutil.h"

local int real_get_filename(Arena *arena, const char *map, char *buffer, int bufferlen)
{
	struct replace_table repls[2] =
	{
		{'a', arena->basename},
		{'m', map}
	};

	if (!map) return -1;

	return find_file_on_path(
			buffer,
			bufferlen,
			CFG_MAP_SEARCH_PATH,
			repls,
			2);
}

local void ArenaAction(Arena *arena, int action)
{
	LinkedList *dls = P_ARENA_DATA(arena, dlkey);

	/* clear any old maps lying around */
	if (action == AA_CREATE || action == AA_DESTROY)
		free_maps(arena);

	if (action == AA_CREATE)
	{
		struct MapDownloadData *data;
		char lvzname[256], fname[256];
		const char *lvzs, *tmp = NULL;

		/* first add the map itself */
		/* cfghelp: General:Map, arena, string
		 * The name of the level file for this arena. */
		if (real_get_filename(
					arena,
					cfg->GetStr(arena->cfg, "General", "Map"),
					fname,
					256) != -1)
		{
			data = compress_map(fname, 1);
			if (data)
				LLAdd(dls, data);
		}

		/* now look for lvzs */
		/* cfghelp: General:LevelFiles, arena, string
		 * A list of extra files to send to the client for downloading.
		 * A '+' before any file means it's marked as optional. */
		lvzs = cfg->GetStr(arena->cfg, "General", "LevelFiles");
		if (!lvzs) lvzs = cfg->GetStr(arena->cfg, "Misc", "LevelFiles");
		while (strsplit(lvzs, ",: ", lvzname, 256, &tmp))
		{
			char *real = lvzname[0] == '+' ? lvzname+1 : lvzname;
			if (real_get_filename(arena, real, fname, 256) != -1)
			{
				data = compress_map(fname, 0);
				if (data)
				{
					if (lvzname[0] == '+')
						data->optional = 1;
					LLAdd(dls, data);
				}
			}
		}
	}
}



local struct MapDownloadData *get_map(Arena *arena, int lvznum, int wantopt)
{
	LinkedList *dls = P_ARENA_DATA(arena, dlkey);
	Link *l;
	int idx;

	/* find the right spot */
	for (idx = lvznum, l = LLGetHead(dls); idx && l; l = l->next)
		if (!((struct MapDownloadData*)(l->data))->optional || wantopt)
			idx--;

	return l ? l->data : NULL;
}


local void get_data(void *clos, int offset, byte *buf, int needed)
{
	struct MapDownloadData *data;
	struct data_locator *dl = (struct data_locator*)clos;

	if (needed == 0)
	{
		lm->Log(L_DRIVEL, "<mapnewsdl> finished map/news download (transfer %p)", dl);
		afree(dl);
	}
	else if (dl->arena == NULL && dl->len == cmpnewssize)
		memcpy(buf, cmpnews + offset, needed);
	else if ((data = get_map(dl->arena, dl->lvznum, dl->wantopt)) &&
	         dl->len == data->cmplen)
		memcpy(buf, data->cmpmap + offset, needed);
	else if (buf)
		memset(buf, 0, needed);
}


local void PMapRequest(Player *p, byte *pkt, int len)
{
	struct data_locator *dl;
	Arena *arena = p->arena;
	int wantopt = WANT_ALL_LVZ(p);

	if (pkt[0] == C2S_MAPREQUEST)
	{
		struct MapDownloadData *data;
		unsigned short lvznum = (len == 3) ? pkt[1] | pkt[2]<<8 : 0;

		if (!arena)
		{
			lm->Log(L_MALICIOUS, "<mapnewsdl> [%s] Map request before entering arena",
					p->name);
			return;
		}

		data = get_map(arena, lvznum, wantopt);

		if (!data)
		{
			lm->LogP(L_WARN, "mapnewsdl", p, "Can't find lvl/lvz %d", lvznum);
			return;
		}

		dl = amalloc(sizeof(*dl));

		dl->arena = arena;
		dl->lvznum = lvznum;
		dl->wantopt = wantopt;
		dl->len = data->cmplen;

		net->SendSized(p, dl, data->cmplen, get_data);
		lm->LogP(L_DRIVEL, "mapnewsdl", p, "Sending map/lvz %d (%d bytes) (transfer %p)",
				lvznum, data->cmplen, dl);
	}
	else if (pkt[0] == C2S_NEWSREQUEST)
	{
		if (cmpnews)
		{
			dl = amalloc(sizeof(*dl));
			dl->arena = NULL;
			dl->len = cmpnewssize;
			net->SendSized(p, dl, cmpnewssize, get_data);
			lm->Log(L_DRIVEL,"<mapnewsdl> [%s] Sending news.txt (transfer %p)", p->name, dl);
		}
		else
			lm->Log(L_WARN, "<mapnewsdl> News request, but compressed news doesn't exist");
	}
}



/* interface */

local Imapnewsdl _int =
{
	INTERFACE_HEAD_INIT(I_MAPNEWSDL, "mapnewsdl")
	SendMapFilename, GetNewsChecksum
};


EXPORT int MM_mapnewsdl(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		/* get interface pointers */
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);

		if (!net || !cfg || !lm || !ml || !aman || !pd) return MM_FAIL;

		dlkey = aman->AllocateArenaData(sizeof(LinkedList));
		if (dlkey == -1) return MM_FAIL;

		/* set up callbacks */
		net->AddPacket(C2S_MAPREQUEST, PMapRequest);
		net->AddPacket(C2S_NEWSREQUEST, PMapRequest);
		mm->RegCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);

		/* reread news every 5 min */
		/* cfghelp: General:NewsRefreshMinutes, global, int, def: 5
		 * How often to check for an updated news.txt. */
		ml->SetTimer(RefreshNewsTxt, 50,
				cfg->GetInt(GLOBAL, "General", "NewsRefreshMinutes", 5)
				* 60 * 100, NULL, NULL);

		/* cache some config data */
		/* cfghelp: General:NewsFile, global, string, def: news.txt
		 * The filename of the news file. */
		cfg_newsfile = cfg->GetStr(GLOBAL, "General", "NewsFile");
		if (!cfg_newsfile) cfg_newsfile = "news.txt";
		newstime = 0; cmpnews = NULL;

		mm->RegInterface(&_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;
		net->RemovePacket(C2S_MAPREQUEST, PMapRequest);
		net->RemovePacket(C2S_NEWSREQUEST, PMapRequest);
		mm->UnregCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);

		afree(cmpnews);
		ml->ClearTimer(RefreshNewsTxt, NULL);

		aman->FreeArenaData(dlkey);

		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(aman);
		return MM_OK;
	}
	return MM_FAIL;
}

