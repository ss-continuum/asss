
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


#include "packets/mapfname.h"

typedef struct MapData
{
	struct MapFilename mapnamepkt;
	byte *cmpmap;
	int cmpmaplen;
} MapData;


/* PROTOTYPES */

/* packet funcs */
local void PMapRequest(int, byte *, int);

/* arenaaction funcs */
local int ArenaAction(int, int);

/* newstxt management */
local void RefreshNewsTxt();



/* GLOBALS */

local PlayerData *players;
local Imainloop *ml;
local Iconfig *cfg;
local Inet *net;
local Imodman *mm;
local Ilogman *log;
local Iarenaman *aman;

/* big static array */
local MapData mapdata[MAXARENA];

local char *cfg_newsfile;
local i32 newschecksum, cmpnewssize;
local byte *cmpnews;
local time_t newstime;


/* FUNCTIONS */

int MM_mapnewsdl(int action, Imodman *mm_)
{
	if (action == MM_LOAD)
	{
		/* get interface pointers */
		mm = mm_;
		net = mm->GetInterface(I_NET);
		log = mm->GetInterface(I_LOGMAN);
		cfg = mm->GetInterface(I_CONFIG);
		ml = mm->GetInterface(I_MAINLOOP);
		aman = mm->GetInterface(I_ARENAMAN);
		players = mm->players;

		if (!net || !cfg || !log || !ml || !aman) return MM_FAIL;

		/* set up callbacks */
		net->AddPacket(C2S_MAPREQUEST, PMapRequest);

		/* cache some config data */
		cfg_newsfile = cfg->GetStr(GLOBAL, "General", "NewsFile");
		if (!cfg_newsfile) cfg_newsfile = "news.txt";
		newstime = 0; cmpnews = NULL;
	}
	else if (action == MM_UNLOAD)
	{
		net->RemovePacket(C2S_NEWSREQUEST, PMapRequest);
		free(cmpnews); cmpnews = NULL;
	}
	else if (action == MM_DESCRIBE)
	{
		mm->desc = "mapnewsdl - sends maps and news.txt to players";
	}
	return MM_OK;
}


int ArenaAction(int action, int arena)
{
	if (action == AA_LOAD)
	{
		byte *map, *cmap;
		int i = 0, mapfd, fsize;
		struct stat st;
		char fname[64];

		/* get map filename and open it */
		me->map.type = S2C_MAPFILENAME;
		sprintf(fname,"map/%s",cfg->GetStr(config, "General", "Map"));
		astrncpy(me->map.filename, cfg->GetStr(config, "General", "Map"), 16);
		mapfd = open(fname,O_RDONLY);
		if (mapfd == -1)
		{
			log->Log(LOG_ERROR,"Map file '%s' not found in current directory", fname);
			free(me); return -1;
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
			free(me); return -1;
		}

		/* calculate crc on mmap'd map */
		me->map.checksum = crc32(crc32(0, Z_NULL, 0), map, fsize);

		/* allocate space for compressed version */
		cmap = amalloc(csize);

		/* set up packet header */
		cmap[0] = S2C_MAPDATA;
		strncpy(cmap+1, cfg->GetStr(config, "General", "Map"), 16);
		/* compress the stuff! */
		compress(cmap+17, &csize, map, fsize);

		/* shrink the allocated memory */
		me->mapdata = realloc(cmap, csize+17);
		if (!me->mapdata)
		{
			log->Log(LOG_ERROR,"realloc failed in CreateArena");
			return -1;
		}
		me->mapdatalen = csize+17;

		munmap(map, fsize);
		close(mapfd);
	}
	else if (action == AA_UNLOAD)
	{
	}
}



void PMapRequest(int pid, byte *p, int q)
{
	int arena = players[pid].arena;
	if (p[0] == C2S_MAPREQUEST)
	{
		log->Log(LOG_DEBUG,"Sending map to %s", players[pid].name);
		if (arena < 0)
		{
			log->Log(LOG_BADDATA, "Map request before entering arena (%s)",
					players[pid].name);
			return;
		}
		net->SendToOne(pid, mapdata[arena]cmpmap,
			mapdata[arena]->cmpmaplen, NET_RELIABLE | NET_PRESIZE);
	}
	else if (p[0] == C2S_NEWSREQUEST)
	{
		log->Log(LOG_DEBUG,"Sending news to %s", players[pid].name);
		net->SendToOne(pid, cmpnews, cmpnewssize, NET_RELIABLE | NET_PRESIZE);
	}
}


void RefreshNewsTxt()
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
}


