
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
	u32 mapchecksum, uncmplen;
	char mapfname[20];
	byte *cmpmap;
	int cmpmaplen;
};


/* PROTOTYPES */

local int CompressMap(int);
local int RefreshNewsTxt(void *);

local void PMapRequest(int, byte *, int);
local void ArenaAction(int, int);

local void SendMapFilename(int pid);
local u32 GetNewsChecksum(void);


/* GLOBALS */

local Imodman *mm;
local Iplayerdata *pd;
local Iconfig *cfg;
local Inet *net;
local Ilogman *lm;
local Iarenaman *aman;
local Imainloop *ml;
local Imapdata *mapdata;

local PlayerData *players;
local ArenaData *arenas;

/* big static array */
local struct MapDownloadData mapdldata[MAXARENA];

local const char *cfg_newsfile;
local u32 newschecksum, cmpnewssize;
local byte *cmpnews;
local time_t newstime;

local Imapnewsdl _int =
{
	INTERFACE_HEAD_INIT(I_MAPNEWSDL, "mapnewsdl")
	SendMapFilename, GetNewsChecksum
};


/* FUNCTIONS */

EXPORT int MM_mapnewsdl(int action, Imodman *mm_, int arena)
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
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);

		if (!net || !cfg || !lm || !ml || !aman || !pd) return MM_FAIL;

		players = pd->players;
		arenas = aman->arenas;

		/* set up callbacks */
		net->AddPacket(C2S_MAPREQUEST, PMapRequest);
		net->AddPacket(C2S_NEWSREQUEST, PMapRequest);
		mm->RegCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);

		/* reread news every 5 min */
		ml->SetTimer(RefreshNewsTxt, 50, 
				cfg->GetInt(GLOBAL, "General", "NewsRefreshMinutes", 5)
				* 60 * 100, NULL);

		/* cache some config data */
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
		ml->ClearTimer(RefreshNewsTxt);

		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(mapdata);
		return MM_OK;
	}
	return MM_FAIL;
}


u32 GetNewsChecksum(void)
{
	if (!cmpnews)
		RefreshNewsTxt(0);
	return newschecksum;
}


#include "packets/mapfname.h"

void SendMapFilename(int pid)
{
	struct MapFilename mf = { S2C_MAPFILENAME };
	int arena, len;

	arena = pd->players[pid].arena;
	strncpy(mf.filename, mapdldata[arena].mapfname, 16);
	mf.checksum = mapdldata[arena].mapchecksum;
	mf.size = mapdldata[arena].uncmplen;
	len = sizeof(mf);
	if (players[pid].type != T_CONT)
		len -= sizeof(u32);
	net->SendToOne(pid, (byte*)&mf, len, NET_RELIABLE);
}


void ArenaAction(int arena, int action)
{
	if (action == AA_CREATE)
	{
		if (mapdldata[arena].cmpmap)
			afree(mapdldata[arena].cmpmap);
		CompressMap(arena);
	}
	else if (action == AA_DESTROY)
	{
		if (mapdldata[arena].cmpmap)
		{
			afree(mapdldata[arena].cmpmap);
			mapdldata[arena].cmpmap = NULL;
			mapdldata[arena].cmpmaplen = 0;
		}
	}
}


int CompressMap(int arena)
{
	byte *map, *cmap;
	int mapfd, fsize;
	uLong csize;
	struct stat st;
	char fname[256], *mapname;
#ifdef WIN32
	HANDLE hfile, hmap;
#endif

	if (mapdata->GetMapFilename(arena, fname, 256))
		return MM_FAIL;

	/* get basename */
	mapname = strrchr(fname, '/');
	if (!mapname)
		mapname = fname;
	else
		mapname++;
	astrncpy(mapdldata[arena].mapfname, mapname, 20);

	mapfd = open(fname,O_RDONLY);
	if (mapfd == -1)
		return MM_FAIL;

	/* find it's size */
	fstat(mapfd, &st);
	fsize = st.st_size;
	csize = 1.0011 * fsize + 35;

	/* mmap it */
#ifndef WIN32
	map = mmap(NULL, fsize, PROT_READ, MAP_SHARED, mapfd, 0);
	if (map == (void*)-1)
	{
		lm->Log(L_ERROR,"<mapnewsdl> mmap failed for map '%s'", fname);
		return MM_FAIL;
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
		return MM_FAIL;
	}
	hmap = CreateFileMapping(hfile,NULL,PAGE_READONLY,0,0,0);
	if (!hmap)
	{
		fclose(hfile);
		lm->Log(L_ERROR,"<mapnewsdl> CreateFileMapping failed for map '%s', error %d",
				fname, GetLastError());
		return MM_FAIL;
	}
	map = MapViewOfFile(hmap,FILE_MAP_READ,0,0,0);
	if (!map)
	{
		CloseHandle(hmap);
		fclose(hfile);
		lm->Log(L_ERROR,"<mapnewsdl> mmap failed for map '%s'", fname);
		return MM_FAIL;
	}
#endif

	/* calculate crc on mmap'd map */
	mapdldata[arena].mapchecksum = crc32(crc32(0, Z_NULL, 0), map, fsize);
	mapdldata[arena].uncmplen = fsize;

	/* allocate space for compressed version */
	cmap = amalloc(csize);

	/* set up packet header */
	cmap[0] = S2C_MAPDATA;
	strncpy(cmap+1, mapname, 16);
	/* compress the stuff! */
	compress(cmap+17, &csize, map, fsize);
	csize += 17;

	/* shrink the allocated memory */
	mapdldata[arena].cmpmap = realloc(cmap, csize);
	if (mapdldata[arena].cmpmap == NULL)
	{
		lm->Log(L_ERROR,"<mapnewsdl> realloc failed in CompressMap");
		free(cmap);
		return MM_FAIL;
	}
	mapdldata[arena].cmpmaplen = csize;

#ifndef WIN32
	munmap(map, fsize);
#else
	UnmapViewOfFile(map);
	CloseHandle(hmap);
	CloseHandle(hfile);
#endif
	close(mapfd);

	return MM_OK;
}


void PMapRequest(int pid, byte *p, int q)
{
	int arena = players[pid].arena;
	if (p[0] == C2S_MAPREQUEST)
	{
		if (ARENA_BAD(arena))
			lm->Log(L_MALICIOUS, "<mapnewsdl> [%s] Map request before entering arena",
					players[pid].name);
		else if (!mapdldata[arena].cmpmap)
			lm->Log(L_WARN, "<mapnewsdl> {%s} Map request, but compressed map doesn't exist", arenas[arena].name);
		else
		{
			net->SendToOne(pid, mapdldata[arena].cmpmap,
					mapdldata[arena].cmpmaplen, NET_RELIABLE | NET_PRESIZE);
			lm->Log(L_DRIVEL,"<mapnewsdl> {%s} [%s] Sending compressed map",
					arenas[arena].name,
					players[pid].name);
		}
	}
	else if (p[0] == C2S_NEWSREQUEST)
	{
		if (cmpnews)
		{
			net->SendToOne(pid, cmpnews, cmpnewssize, NET_RELIABLE | NET_PRESIZE);
			lm->Log(L_DRIVEL,"<mapnewsdl> [%s] Sending news.txt", players[pid].name);
		}
		else
			lm->Log(L_WARN, "<mapnewsdl> News request, but compressed news doesn't exist");
	}
}


int RefreshNewsTxt(void *dummy)
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
		csize = 1.0011 * fsize + 35;

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


