
#include <string.h>
#include <stdio.h>

#include "asss.h"


#include "packets/goarena.h"

/* PROTOTYPES */

/* timers */
local int ReapArenas(void *);


/* arena management funcs */
local int FindArena(char *, int);
local int CreateArena(char *, int);
local void FreeArena(int);

local void PArena(int, byte *, int);

local void SendMultipleArenaResponses(int);
local void SendOneArenaResponse(int);


/* GLOBALS */

local PlayerData *players;
local Imainloop *ml;
local Iconfig *cfg;
local Inet *net;
local Imodman *mm;
local Ilogman *log;
local Imapnewsdl *map;
local Iassignfreq *afreq;

/* big static arena data array */
local ArenaData arenas[MAXARENA];

local Iarenaman _int = { arenas };






int MM_arenaman(int action, Imodman *mm_)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		mm->RegInterest(I_NET, &net);
		mm->RegInterest(I_LOGMAN, &log);
		mm->RegInterest(I_CONFIG, &cfg);
		mm->RegInterest(I_MAINLOOP, &ml);
		mm->RegInterest(I_MAPNEWSDL, &map);
		mm->RegInterest(I_ASSIGNFREQ, &afreq);
		players = mm->players;

		if (!net || !log || !ml) return MM_FAIL;

		memset(arenas, 0, sizeof(ArenaData) * MAXARENA);

		net->AddPacket(C2S_GOTOARENA, PArena);

		ml->SetTimer(ReapArenas, 1000, 3000, NULL);

		mm->RegInterface(I_ARENAMAN, &_int);
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterface(I_ARENAMAN, &_int);
		net->RemovePacket(C2S_GOTOARENA, PArena);
		ml->ClearTimer(ReapArenas);
		mm->UnregInterest(I_NET, &net);
		mm->UnregInterest(I_LOGMAN, &log);
		mm->UnregInterest(I_CONFIG, &cfg);
		mm->UnregInterest(I_MAINLOOP, &ml);
		mm->UnregInterest(I_MAPNEWSDL, &map);
		mm->UnregInterest(I_ASSIGNFREQ, &afreq);
	}
	else if (action == MM_DESCRIBE)
	{
		mm->desc = "arenaman - handles arena creating/destruction";
	}
	return MM_OK;
}



local void CallAA(int action, int arena)
{
	LinkedList *funcs;
	Link *l;

	funcs = mm->LookupCallback(CALLBACK_ARENAACTION);

	for (l = LLGetHead(funcs); l; l = l->next)
		((ArenaActionFunc)l->data)(action, arena);

	LLFree(funcs);
}


local void DoLoadArena(int arena)
{
	char fname[64];
	ConfigHandle *config;

	arenas[arena].status = ARENA_LOADING;

	/* this should go in another thread {{{ */

	snprintf(fname, 64, "arena-%s", arenas[arena].name);
	config = cfg->OpenConfigFile(fname);
	/* if not, try default config */
    if (!config)
    {
        log->Log(LOG_USELESSINFO,"Config file '%s' not found, using default", fname);
        config = cfg->OpenConfigFile("arena-default");
    }
    if (!config)
    {   /* if not, fail */
        log->Log(LOG_ERROR,"Default config file not found");
        return;
    }
	arenas[arena].cfg = config;

	/* call module arena creating handlers */
	CallAA(AA_LOAD, arena);

	/* }}} to here */

	arenas[arena].status = ARENA_RUNNING;
	SendMultipleArenaResponses(arena);
}


local void DoFreeArena(int arena)
{
	arenas[arena].status = ARENA_UNLOADING;

	/* other thread: {{{ */
	CallAA(AA_UNLOAD, arena);
	cfg->CloseConfigFile(arenas[arena].cfg);
	/* }}} to here */

	arenas[arena].status = ARENA_NONE;
}


int CreateArena(char *name, int initialpid)
{
	int i = 0;

	if (FindArena(name, TRUE) != -1)
	{
		log->Log(LOG_ERROR,"Internal error in CreateArena: arena %s already exists!", name);
		return -1;
	}

	while (arenas[i].status != ARENA_NONE && i < MAXARENA) i++;

	if (i == MAXARENA)
	{
		log->Log(LOG_IMPORTANT, "Arena limit exceeded!");
		return -1;
	}

	astrncpy(arenas[i].name, name, 20);

	return i;
}

void FreeArena(int arena)
{
	/* eventually this will pass the message to another thread */
	DoFreeArena(arena);
}


void SendOneArenaResponse(int pid)
{
	struct SimplePacket whoami = { S2C_WHOAMI, 0 };
	struct MapFilename mapfname;
	int arena, i;
	/* Iarenaset *aset; */

	arena = players[pid].arena;

	/* send whoami packet */
	whoami.d1 = pid;
	net->SendToOne(pid, (byte*)&whoami, 3, NET_RELIABLE);

	/* figure out his freq */
	players[pid].freq = afreq->AssignFreq(pid, BADFREQ, players[pid].shiptype);

	/* send settings */
	/* aset = mm->GetInterface(I_ARENASET);
	 * net->SendToOne(pid, (byte*)aset->GetSettingData(arena),
	 *                   aset->GetSettingDataSize(), NET_RELIABLE);
	 */

	/* send player list */
	for (i = 0; i < MAXPLAYERS; i++)
		if (	players[i].status == S_CONNECTED
			 && players[i].arena  == arena )
			net->SendToOne(pid, (byte*)(players+i), 64, NET_RELIABLE);

	/* send mapfilename */
	mapfname.type = S2C_MAPFILENAME;
	mapfname.checksum = map->GetMapChecksum(arena);
	astrncpy(mapfname.filename, map->GetMapFileName(arena), 16);
	net->SendToOne(pid, (byte*)&mapfname, sizeof(struct MapFilename), NET_RELIABLE);

	/* send brick clear and finisher */
	mapfname.type = S2C_BRICK;
	net->SendToOne(pid, (byte*)&mapfname, 1, NET_RELIABLE);
	mapfname.type = S2C_ENTERINGARENA;
	net->SendToOne(pid, (byte*)&mapfname, 1, NET_RELIABLE);

	/* alert others */
	net->SendToArena(players[pid].arena, pid,
			(byte*)(players+pid), 64, NET_RELIABLE);
}


void SendMultipleArenaResponses(int arena)
{
/*
 * we want to make this one as efficient as possible. thus:
 * 
 * enumerate all the pids first
 *
 * whoamis: loop and sendtoone
 *
 * freqs: loop and figure them all out
 *
 * settings: sendtoset
 *
 * player lists: it seems like everyone will have to know about everyone
 * else, so: loop and sendtoset everyone to everyone
 *
 * mapfilenames: sendtoset
 *
 * brick clear and finisher: sendtoset both
 *
 * alert others: taken care of above
 *
 */
}


int FindArena(char *name, int acceptloading)
{
	int i;
	/* lock arena status */
	for (i = 0; i < MAXARENA; i++)
		if (	( arenas[i].status == ARENA_RUNNING ||
				  ( acceptloading && arenas[i].status == ARENA_LOADING) )
				&& !strcasecmp(arenas[i].name, name) )
			return i;
	/* unlock arena status */
	return -1;
}




void PArena(int pid, byte *p, int l)
{
	struct GoArenaPacket *go;
	char *name, digit[2];
	int arena;

	/* check for bad packets */
	if (l != sizeof(struct GoArenaPacket))
	{
		log->Log(LOG_BADDATA, "Wrong size arena packet recvd (%s)", players[pid].name);
		return;
	}

	go = (struct GoArenaPacket*)p;

	if (go->shiptype < 0 || go->shiptype > SPEC)
	{
		log->Log(LOG_BADDATA, "Bad shiptype in request (%s)", players[pid].name);
	}

	/* make a name from the request */
	if (go->arenatype == -3)
		name = go->arenaname;
	else if (go->arenatype == -2 || go->arenatype == -1)
	{
		name = digit;
		digit[0] = '0';
		digit[1] = 0;
	}
	else if (go->arenatype >= 0 && go->arenatype <= 9)
	{
		name = digit;
		digit[0] = go->arenatype + '0';
		digit[1] = 0;
	}
	else
	{
		log->Log(LOG_BADDATA, "Bad arenatype in request (%s)", players[pid].name);
		return;
	}


	/* try to locate an existing arena */
	arena = FindArena(name, TRUE);

	if (arena == -1)
	{
		arena = CreateArena(name, pid);
		if (arena == -1)
		{
			/* if it fails, dump in first available */
			arena = 0;
			while (arenas[arena].status != ARENA_RUNNING && arena < MAXARENA) arena++;
			if (arena == MAXARENA) return;
		}
		players[pid].arena = arena;
		players[pid].shiptype = go->shiptype;
		players[pid].xres = go->xres;
		players[pid].yres = go->yres;
		/* this will call SendMultipleArenaReponses, somehow */
		DoLoadArena(arena);
	}
	else
	{
		/* set this so other functions know he is in here */
		players[pid].arena = arena;
		players[pid].shiptype = go->shiptype;
		players[pid].xres = go->xres;
		players[pid].yres = go->yres;

		if (arenas[arena].status == ARENA_LOADING)
		{
			/* do nothing, entry will be send when loading is done */
		}
		else if (arenas[arena].status == ARENA_RUNNING)
		{
			SendOneArenaResponse(pid);
		}
		else
			log->Log(LOG_ERROR,"Internal error in PArena: arena %d should be either loading or running", arena);
	}
}



int ReapArenas(void *q)
{
	int i, j;

	for (i = 0; i < MAXARENA; i++)
		if (arenas[i].status == ARENA_RUNNING)
		{
			for (j = 0; j < MAXPLAYERS; j++)
				if (	players[j].status == S_CONNECTED &&
						players[j].arena == i)
					goto skip;
			
			log->Log(LOG_USELESSINFO, "Arena %s (%i) being reaped",
					arenas[i].name, i);
			FreeArena(i);
skip:
		}
	return 1;
}


#if 0

void SendArenaResponse(int arena)
{
	struct SimplePacket whoami = {S2C_WHOAMI, 0, 0, 0, 0, 0};
	int pids[MAXPLAYERS], pidc = 0, i;
	byte misc;

	for (i = 0; i < MAXPLAYERS; i++)
		if (players[i].status == S_CONNECTED && players[i].arena == arena)
			pids[pidc++] = i;
	pids[pidc++] = -1;

	/* send whoami packet */
	net->SendToOne(pid, (byte*)&whoami, 3, NET_RELIABLE);

	/* set up info */
	players[pid].shiptype = pk->shiptype;
	players[pid].xres = pk->xres;
	players[pid].yres = pk->yres;
	players[pid].arena = arena = AssignArena(pk);
	players[pid].freq = ((Iassignfreq*)mm->GetInterface(I_ASSIGNFREQ))->
		AssignFreq(pid, BADFREQ, pk->shiptype);

	/* send settings */
	net->SendToOne(pid, arenas[arena]->settings, SETTINGSIZE, NET_RELIABLE);

	/* send player list */
	for (i = 0; i < MAXPLAYERS; i++)
		if (	net->GetStatus(i) == S_CONNECTED &&
				players[i].arena == arena)
			net->SendToOne(pid, (byte*)(players+i), 64, NET_RELIABLE);

	/* send brick clear and finisher */
	net->SendToOne(pid, (byte*)&arenas[arena]->map,
			sizeof(struct MapFilename), NET_RELIABLE);
	misc = S2C_BRICK;
	net->SendToOne(pid, &misc, 1, NET_RELIABLE);
	misc = 0x02;
	net->SendToOne(pid, &misc, 1, NET_RELIABLE);

	/* alert others */
	net->SendToArena(players[pid].arena, pid,
			(byte*)(players+pid), 64, NET_RELIABLE);

	log->Log(LOG_INFO, "Player entering arena '%s' (%s)",
			arenas[arena]->name, players[pid].name);
}



int CreateArena(char *name)
{
	FILE *f;
    struct stat st;
	int i = 0, mapfd, fsize;
    byte *map, *cmap;
	ConfigHandle config;
	uLong csize;
	ArenaData *me = amalloc(sizeof(ArenaData));
	char fname[64];

	/* find a slot */
	while (arenas[i] && i < MAXARENA) i++;
	if (i == MAXARENA)
	{
		log->Log(LOG_ERROR,"Too many arenas!!! Cannot create arena %s", name);
		return -1;
	}

	/* fill in simple data */
	strcpy(me->name, name);

	/* try to open config file */
	snprintf(fname, 64, "arena-%s", name);
	config = cfg->OpenConfigFile(fname);
	/* if not, try default config */
	if (!config)
	{
		log->Log(LOG_USELESSINFO,"Config file '%s' not found, using default", fname);
		config = cfg->OpenConfigFile("arena-default");
	}
	if (!config)
	{	/* if not, fail */
		log->Log(LOG_ERROR,"Default config file not found");
		free(me); return -1;
	}
	me->config = config;

	arenas[i] = me;
	log->Log(LOG_USELESSINFO,"Arena %s (%i) created sucessfully", name, i);
	return i;
}


#endif

