
#include <zlib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include "asss.h"


typedef struct MapData
{
	i32 mapchecksum;
	byte *cmpmap;
	int cmpmaplen;
} MapData;


/* PROTOTYPES */

/* packet funcs */
local void PMapRequest(int, byte *, int);

/* arenaaction funcs */
local int ArenaAction(int, int);

local void CompressMap(int);

/* newstxt management */
local void RefreshNewsTxt();

i32 GetMapChecksum(int arena);
i32 GetNewsChecksum();


/* GLOBALS */

local PlayerData *players;
local ArenaData *arenas;
local Iconfig *cfg;
local Inet *net;
local Imodman *mm;
local Ilogman *log;

/* big static array */
local MapData mapdata[MAXARENA];

local char *cfg_newsfile;
local i32 newschecksum, cmpnewssize;
local byte *cmpnews;
local time_t newstime;

local Imapnewsdl _int = { GetMapChecksum, GetNewsChecksum };


/* FUNCTIONS */

int MM_mapnewsdl(int action, Imodman *mm_)
{
	static Imainloop *ml;
	if (action == MM_LOAD)
	{
		Iarenaman *aman;

		/* get interface pointers */
		mm = mm_;
		net = mm->GetInterface(I_NET);
		log = mm->GetInterface(I_LOGMAN);
		cfg = mm->GetInterface(I_CONFIG);
		ml = mm->GetInterface(I_MAINLOOP);
		aman = mm->GetInterface(I_ARENAMAN);
		players = mm->players;

		if (!net || !cfg || !log || !ml || !aman) return MM_FAIL;

		arenas = aman->data;

		/* set up callbacks */
		net->AddPacket(C2S_MAPREQUEST, PMapRequest);
		net->AddPacket(C2S_NEWSREQUEST, PMapRequest);

		/* reread news every 15 min */
		ml->SetTimer(RefreshNewsTxt, 50, 
				cfg->GetInt(GLOBAL, "General", "NewsRefreshMinutes", 15)
				* 60 * 100, NULL);

		/* cache some config data */
		cfg_newsfile = cfg->GetStr(GLOBAL, "General", "NewsFile");
		if (!cfg_newsfile) cfg_newsfile = "news.txt";
		newstime = 0; cmpnews = NULL;

		mm->RegisterInterface(I_MAPNEWSDL, &_int);
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregisterInterface(I_MAPNEWSDL, &_int);
		net->RemovePacket(C2S_MAPREQUEST, PMapRequest);
		net->RemovePacket(C2S_NEWSREQUEST, PMapRequest);
		free(cmpnews);
		ml->ClearTimer(RefreshNewsTxt);
	}
	else if (action == MM_DESCRIBE)
	{
		mm->desc = "mapnewsdl - sends maps and news.txt to players";
	}
	return MM_OK;
}


i32 GetMapChecksum(int arena)
{
	return mapdata[arena].mapchecksum;
}


i32 GetNewsChecksum()
{
	if (!cmpnews)
		RefreshNewsTxt();
	return newschecksum;
}


int ArenaAction(int action, int arena)
{
	if (action == AA_LOAD)
	{
		if (mapdata[arena].cmpmap)
			free(mapdata[arena].cmpmap);
		CompressMap(arena);
	}
	else if (action == AA_UNLOAD)
	{
		free(mapdata[arena].cmpmap);
		mapdata[arena].cmpmaplen = 0;
	}
}


void CompressMap(int arena)
{
	byte *map, *cmap;
	int i = 0, mapfd, fsize;
	struct stat st;
	char fname[64], *mapname;

	mapname = cfg->GetStr(config, "General", "Map");

	/* get map filename and open it */
	sprintf(fname,"map/%s",mapname);
	mapfd = open(fname,O_RDONLY);
	if (mapfd == -1)
	{
		log->Log(LOG_ERROR,"Map file '%s' not found in current directory", fname);
		return AA_FAIL;
	}

	/* find it's size */
	fstat(mapfd, &st);
	fsize = st.st_size;
	csize = 1.0011 * fsize + 35;

	/* mmap it */
	map = mmap(NULL, fsize, PROT_READ, MAP_SHARED, mapfd, 0);
	if (map == (void*)-1)
	{
		log->Log(LOG_ERROR,"mmap failed in CreateArena");
		return AA_FAIL;
	}

	/* calculate crc on mmap'd map */
	mapdata[arena].mapchecksum = crc32(crc32(0, Z_NULL, 0), map, fsize);

	/* allocate space for compressed version */
	cmap = amalloc(csize);

	/* set up packet header */
	cmap[0] = S2C_MAPDATA;
	strncpy(cmap+1, mapname);
	/* compress the stuff! */
	compress(cmap+17, &csize, map, fsize);

	/* shrink the allocated memory */
	mapdata[arena].cmpmap = realloc(cmap, csize+17);
	if (!me->mapdata)
	{
		log->Log(LOG_ERROR,"realloc failed in CreateArena");
		return AA_FAIL;
	}
	mapdata[arena].cmpmaplen = csize+17;

	munmap(map, fsize);
	close(mapfd);
}


void PMapRequest(int pid, byte *p, int q)
{
	int arena = players[pid].arena;
	if (p[0] == C2S_MAPREQUEST)
	{
		log->Log(LOG_DEBUG,"Sending map to %s", players[pid].name);
		if (arena < 0)
			log->Log(LOG_BADDATA, "Map request before entering arena (%s)",
					players[pid].name);
		else if (!mapdata[arena].cmpmap)
			log->Log(LOG_BADDATA, "Map request, but compressed map doesn't exist!");
		else
			net->SendToOne(pid, mapdata[arena].cmpmap,
				mapdata[arena]->cmpmaplen, NET_RELIABLE | NET_PRESIZE);
	}
	else if (p[0] == C2S_NEWSREQUEST)
	{
		log->Log(LOG_DEBUG,"Sending news to %s", players[pid].name);
		if (cmpnews)
			net->SendToOne(pid, cmpnews, cmpnewssize, NET_RELIABLE | NET_PRESIZE);
		else
			log->Log(LOG_ERROR, "News request, but compressed news doesn't exist!");
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
        log->Log(LOG_ERROR,"News file '%s' not found in current directory", cfg_newsfile);
		return;
    }

	/* find it's size */
	fstat(fd, &st);
	newtime = st.st_mtime + st.st_ctime;
	if (newtime != newstime)
	{
		newstime = newtime;
		fsize = st.st_size;
		csize = 1.0011 * fsize + 35;

		/* mmap it */
		news = mmap(NULL, fsize, PROT_READ, MAP_SHARED, fd, 0);
		if (news == (void*)-1)
		{
			log->Log(LOG_ERROR,"mmap failed in RefreshNewsTxt");
			close(fd);
			return;
		}

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
			log->Log(LOG_ERROR,"realloc failed in RefreshNewsTxt");
			close(fd);
			return;
		}
		cmpnewssize = csize+17;

		munmap(news, fsize);

		if (cmpnews) free(cmpnews);
		cmpnews = cnews;
		log->Log(LOG_USELESSINFO,"News file %s reread", cfg_newsfile);
	}
	close(fd);
	return 1;
}


