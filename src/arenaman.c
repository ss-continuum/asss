
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "asss.h"


#include "packets/goarena.h"

/* MACROS */

#define LOCK_STATUS() \
	pthread_mutex_lock(&arenastatusmtx)

#define UNLOCK_STATUS() \
	pthread_mutex_unlock(&arenastatusmtx)


/* PROTOTYPES */

/* timers */
local int ReapArenas(void *);

/* main loop */
local void ProcessArenaQueue(void);

/* arena management funcs */
local int FindArena(char *, int, int);
local int CreateArena(char *);
local void LockStatus(void);
local void UnlockStatus(void);

local void PArena(int, byte *, int);
local void PLeaving(int, byte *, int);

local void SendArenaResponse(int);


/* GLOBALS */

local Imainloop *ml;
local Iplayerdata *pd;
local Iconfig *cfg;
local Inet *net;
local Imodman *mm;
local Ilogman *log;
local Imapnewsdl *map;
local Iassignfreq *afreq;
local Iclientset *clientset;

local PlayerData *players;

/* big static arena data array */
local ArenaData arenas[MAXARENA];

local pthread_mutex_t arenastatusmtx;

local Iarenaman _int =
{ SendArenaResponse, LockStatus, UnlockStatus, arenas };




int MM_arenaman(int action, Imodman *mm_, int arena)
{
	pthread_mutexattr_t attr;

	if (action == MM_LOAD)
	{
		mm = mm_;
		mm->RegInterest(I_PLAYERDATA, &pd);
		mm->RegInterest(I_NET, &net);
		mm->RegInterest(I_LOGMAN, &log);
		mm->RegInterest(I_CONFIG, &cfg);
		mm->RegInterest(I_MAINLOOP, &ml);
		mm->RegInterest(I_MAPNEWSDL, &map);
		mm->RegInterest(I_ASSIGNFREQ, &afreq);
		mm->RegInterest(I_CLIENTSET, &clientset);

		if (!net || !log || !ml) return MM_FAIL;

		players = pd->players;

		memset(arenas, 0, sizeof(ArenaData) * MAXARENA);

		/* init mutexes */
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&arenastatusmtx, &attr);

		mm->RegCallback(CALLBACK_MAINLOOP, ProcessArenaQueue, ALLARENAS);

		net->AddPacket(C2S_GOTOARENA, PArena);
		net->AddPacket(C2S_LEAVING, PLeaving);

		ml->SetTimer(ReapArenas, 1000, 1500, NULL);

		mm->RegInterface(I_ARENAMAN, &_int);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterface(I_ARENAMAN, &_int);
		net->RemovePacket(C2S_GOTOARENA, PArena);
		net->RemovePacket(C2S_LEAVING, PLeaving);
		mm->UnregCallback(CALLBACK_MAINLOOP, ProcessArenaQueue, ALLARENAS);
		ml->ClearTimer(ReapArenas);
		mm->UnregInterest(I_PLAYERDATA, &pd);
		mm->UnregInterest(I_NET, &net);
		mm->UnregInterest(I_LOGMAN, &log);
		mm->UnregInterest(I_CONFIG, &cfg);
		mm->UnregInterest(I_MAINLOOP, &ml);
		mm->UnregInterest(I_MAPNEWSDL, &map);
		mm->UnregInterest(I_ASSIGNFREQ, &afreq);
		mm->UnregInterest(I_CLIENTSET, &clientset);
		return MM_OK;
	}
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
	return MM_FAIL;
}


void LockStatus(void)
{
	LOCK_STATUS();
}

void UnlockStatus(void)
{
	UNLOCK_STATUS();
}


local void CallAA(int arena, int action)
{
	LinkedList *funcs;
	Link *l;

	funcs = mm->LookupCallback(CALLBACK_ARENAACTION, arena);

	for (l = LLGetHead(funcs); l; l = l->next)
		((ArenaActionFunc)l->data)(arena, action);

	mm->FreeLookupResult(funcs);
}

local void DoAttach(int arena, int action)
{
	void (*func)(char *name, int arena);
	char *mods, *t, *_tok;

	if (action == MM_ATTACH)
		func = mm->AttachModule;
	else if (action == MM_DETACH)
		func = mm->DetachModule;
	else
		return;

	t = cfg->GetStr(arenas[arena].cfg, "Modules", "AttachModules");
	if (!t) return;

	mods = alloca(strlen(t)+1);
	strcpy(mods, t);

#define DELIMS " \t:;,"

	t = strtok_r(mods, DELIMS, &_tok);
	while (t)
	{
		func(t, arena); /* attach or detach modules */
		t = strtok_r(NULL, DELIMS, &_tok);
	}
}


void ProcessArenaQueue(void)
{
	int i, j, nextstatus;
	ArenaData *a;

	LOCK_STATUS();
	for (i = 0, a = arenas; i < MAXARENA; i++, a++)
	{
		/* get the status */
		nextstatus = a->status;

		switch (nextstatus)
		{
			case ARENA_NONE:
			case ARENA_RUNNING:
				continue;
		}

		UNLOCK_STATUS();

		switch (nextstatus)
		{
			case ARENA_DO_LOAD_CONFIG:
				a->cfg = cfg->OpenConfigFile(a->name, NULL);
				DoAttach(i, MM_ATTACH);
				nextstatus = ARENA_DO_CREATE_CALLBACKS;
				break;

			case ARENA_DO_CREATE_CALLBACKS:
				/* do callbacks */
				CallAA(i, AA_CREATE);

				/* don't muck with player status now, let it be done in
				 * the arena processing function */

				nextstatus = ARENA_RUNNING;
				break;

			case ARENA_DO_DESTROY_CALLBACKS:
				/* ASSERT there is nobody in here */
				for (j = 0; j < MAXPLAYERS; j++)
					if (players[j].status != S_FREE)
						assert(players[j].arena != i);
				CallAA(i, AA_DESTROY);
				nextstatus = ARENA_DO_UNLOAD_CONFIG;
				break;

			case ARENA_DO_UNLOAD_CONFIG:
				DoAttach(i, MM_DETACH);
				cfg->CloseConfigFile(a->cfg);
				a->cfg = NULL;
				nextstatus = ARENA_NONE;
				break;
		}
		
		LOCK_STATUS();
		a->status = nextstatus;
	}
	UNLOCK_STATUS();
}


int CreateArena(char *name)
{
	int i = 0;

	LOCK_STATUS();
	while (arenas[i].status != ARENA_NONE && i < MAXARENA) i++;

	if (i == MAXARENA)
	{
		log->Log(L_WARN, "<arenaman> Cannot create a new arena: too many arenas");
		UNLOCK_STATUS();
		return -1;
	}

	astrncpy(arenas[i].name, name, 20);
	arenas[i].status = ARENA_DO_LOAD_CONFIG;
	UNLOCK_STATUS();

	return i;
}


void SendArenaResponse(int pid)
{
	/* LOCK: maybe should lock more in here? */
	struct SimplePacket whoami = { S2C_WHOAMI, 0 };
	int arena, i;
	PlayerData *p;

	p = players + pid;

	arena = p->arena;

	log->Log(L_INFO, "<arenaman> {%s} [%s] entering arena",
				arenas[arena].name, p->name);

	/* send whoami packet */
	whoami.d1 = pid;
	net->SendToOne(pid, (byte*)&whoami, 3, NET_RELIABLE);

	/* send settings */
	clientset->SendClientSettings(pid);

	/* send player list */
	/* note: the current player's status should be S_SEND_ARENA_RESPONSE
	 * at this point */

	/* send info to himself first */
	net->SendToOne(pid, (byte*)(players+pid), 64, NET_RELIABLE);
	pd->LockStatus();
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (	players[i].status == S_PLAYING
		     && players[i].arena  == arena
		     && i != pid )
		{
			/* send each other info */
			net->SendToOne(pid, (byte*)(players+i), 64, NET_RELIABLE);
			net->SendToOne(i, (byte*)(players+pid), 64, NET_RELIABLE);
		}
	}
	pd->UnlockStatus();

	/* send mapfilename */
	map->SendMapFilename(pid);

	/* send brick clear and finisher */
	whoami.type = S2C_BRICK;
	net->SendToOne(pid, (byte*)&whoami, 1, NET_RELIABLE);
	whoami.type = S2C_ENTERINGARENA;
	net->SendToOne(pid, (byte*)&whoami, 1, NET_RELIABLE);
}


int FindArena(char *name, int min, int max)
{
	int i;
	LOCK_STATUS();
	for (i = 0; i < MAXARENA; i++)
		if (    arenas[i].status >= min
		     && arenas[i].status <= max
		     && !strcasecmp(arenas[i].name, name) )
		{
			UNLOCK_STATUS();
			return i;
		}
	UNLOCK_STATUS();
	return -1;
}

void PArena(int pid, byte *p, int l)
{
	/* status should be S_LOGGEDIN at this point */
	struct GoArenaPacket *go;
	char *name, digit[2];
	int arena;

	/* check for bad packets */
	if (l != sizeof(struct GoArenaPacket))
	{
		log->Log(L_MALICIOUS, "<arenaman> [%s] Wrong size arena packet recvd", players[pid].name);
		return;
	}

	if (players[pid].arena != -1)
	{
		log->Log(L_MALICIOUS, "<arenaman> [%s] Recvd arena request from player already in an arena", players[pid].name);
		return;
	}

	go = (struct GoArenaPacket*)p;

	if (go->shiptype < 0 || go->shiptype > SPEC)
	{
		log->Log(L_MALICIOUS, "<arenaman> [%s] Bad shiptype in arena request", players[pid].name);
		return;
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
		log->Log(L_MALICIOUS, "<arenaman> [%s] Bad arenatype in arena request", players[pid].name);
		return;
	}

	LOCK_STATUS();

	/* try to locate an existing arena */
	arena = FindArena(name, ARENA_DO_LOAD_CONFIG, ARENA_RUNNING);

	if (arena == -1)
	{
		log->Log(L_INFO, "<arenaman> {%s} Creating arena", name);
		arena = CreateArena(name);
		if (arena == -1)
		{
			/* if it fails, dump in first available */
			arena = 0;
			while (arenas[arena].status != ARENA_RUNNING && arena < MAXARENA) arena++;
			if (arena == MAXARENA)
			{
				log->Log(L_ERROR, "<arenaman> Internal error: no running arenas but cannot create new one");
				UNLOCK_STATUS();
				return;
			}
		}
	}

	/* set up player info */
	players[pid].arena = arena;
	players[pid].oldarena = arena;
	players[pid].shiptype = go->shiptype;
	players[pid].xres = go->xres;
	players[pid].yres = go->yres;

	/* don't mess with player status yet, let him stay in S_LOGGEDIN.
	 * it will be incremented when the arena is ready. */
	UNLOCK_STATUS();
}


void PLeaving(int pid, byte *p, int q)
{
	int arena;
	struct SimplePacket pk = { S2C_PLAYERLEAVING };

	pk.d1 = pid;

	pd->LockStatus();

	arena = players[pid].arena;
	if (players[pid].status != S_PLAYING || arena == -1)
	{
		pd->UnlockStatus();
		return;
	}

	players[pid].oldarena = arena;
	/* this needs to be done for some good reason. i think it has to do
	 * with KillConnection in net. */
	players[pid].arena = -1;
	players[pid].status = S_LEAVING_ARENA;

	pd->UnlockStatus();

	net->SendToArena(arena, pid, (byte*)&pk, 3, NET_RELIABLE);
	log->Log(L_INFO, "<arenaman> {%s} [%s] Player leaving arena",
			arenas[arena].name, players[pid].name);
}


int ReapArenas(void *q)
{
	int i, j;

	/* lock all status info. remember player after arena!! */
	LOCK_STATUS();
	pd->LockStatus();

	for (i = 0; i < MAXARENA; i++)
		if (arenas[i].status == ARENA_RUNNING)
		{
			for (j = 0; j < MAXPLAYERS; j++)
				if (	players[j].status != S_FREE &&
						players[j].arena == i)
					goto skip;

			log->Log(L_DRIVEL, "<arenaman> {%s} Arena being destroyed (id=%d)",
					arenas[i].name, i);
			/* set its status so that the arena processor will do
			 * appropriate things */
			arenas[i].status = ARENA_DO_DESTROY_CALLBACKS;
skip:
		}
	/* unlock all status info */
	pd->UnlockStatus();
	UNLOCK_STATUS();
	return 1;
}


