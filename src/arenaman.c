
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#ifdef WIN32
#include <malloc.h>
#endif

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
local Ilogman *lm;

local PlayerData *players;

/* big static arena data array */
local ArenaData arenas[MAXARENA];

local pthread_mutex_t arenastatusmtx;

local Iarenaman _int =
{
	INTERFACE_HEAD_INIT(I_ARENAMAN, "arenaman")
	SendArenaResponse, LockStatus, UnlockStatus, arenas
};




EXPORT int MM_arenaman(int action, Imodman *mm_, int arena)
{
	pthread_mutexattr_t attr;

	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		if (!pd || !net || !lm || !cfg || !ml) return MM_FAIL;

		players = pd->players;

		memset(arenas, 0, sizeof(ArenaData) * MAXARENA);

		/* init mutexes */
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&arenastatusmtx, &attr);

		mm->RegCallback(CB_MAINLOOP, ProcessArenaQueue, ALLARENAS);

		net->AddPacket(C2S_GOTOARENA, PArena);
		net->AddPacket(C2S_LEAVING, PLeaving);

		ml->SetTimer(ReapArenas, 1000, 1500, NULL);

		mm->RegInterface(&_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;
		net->RemovePacket(C2S_GOTOARENA, PArena);
		net->RemovePacket(C2S_LEAVING, PLeaving);
		mm->UnregCallback(CB_MAINLOOP, ProcessArenaQueue, ALLARENAS);
		ml->ClearTimer(ReapArenas);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(ml);
		return MM_OK;
	}
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


local void DoAttach(int arena, int action)
{
	void (*func)(const char *name, int arena);
	char *mods, *t, *_tok;
	const char *attmods;

	if (action == MM_ATTACH)
		func = mm->AttachModule;
	else if (action == MM_DETACH)
		func = mm->DetachModule;
	else
		return;

	attmods = cfg->GetStr(arenas[arena].cfg, "Modules", "AttachModules");
	if (!attmods) return;

	mods = alloca(strlen(attmods)+1);
	strcpy(mods, attmods);

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
				DO_CBS(CB_ARENAACTION, i, ArenaActionFunc, (i, AA_CREATE));

				/* don't muck with player status now, let it be done in
				 * the arena processing function */

				nextstatus = ARENA_RUNNING;
				break;

			case ARENA_DO_DESTROY_CALLBACKS:
				/* ASSERT there is nobody in here */
				for (j = 0; j < MAXPLAYERS; j++)
					if (players[j].status != S_FREE)
						assert(players[j].arena != i);
				DO_CBS(CB_ARENAACTION, i, ArenaActionFunc, (i, AA_DESTROY));
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
		lm->Log(L_WARN, "<arenaman> Cannot create a new arena: too many arenas");
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
	if (ARENA_BAD(arena))
	{
		lm->Log(L_WARN, "<arenaman> [%s] bad arena id in SendArenaResponse",
				p->name);
		return;
	}

	lm->Log(L_INFO, "<arenaman> {%s} [%s] entering arena",
				arenas[arena].name, p->name);

	/* send whoami packet */
	whoami.d1 = pid;
	net->SendToOne(pid, (byte*)&whoami, 3, NET_RELIABLE);

	/* send settings */
	{
		Iclientset *clientset = mm->GetInterface(I_CLIENTSET, arena);
		if (clientset)
			clientset->SendClientSettings(pid);
	}

	/* send player list */
	/* note: the current player's status should be S_SEND_ARENA_RESPONSE
	 * at this point */

	/* send info to himself first */
	pd->LockStatus();
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (players[i].status == S_PLAYING &&
				players[i].arena == arena &&
				i != pid )
		{
			/* send each other info */
			net->SendToOne(pid, (byte*)(players+i), 64, NET_RELIABLE);
			net->SendToOne(i, (byte*)(players+pid), 64, NET_RELIABLE);
		}
	}
	net->SendToOne(pid, (byte*)(players+pid), 64, NET_RELIABLE);
	pd->UnlockStatus();

	/* send mapfilename */
	{
		Imapnewsdl *map = mm->GetInterface(I_MAPNEWSDL, arena);
		if (map)
			map->SendMapFilename(pid);
	}

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
	int arena, type;

	/* check for bad packets */
	type = players[pid].type;
	if (type != T_VIE && type != T_CONT)
	{
		lm->Log(L_MALICIOUS,"<arenaman> [%s] Arena packet from wrong client type (%d)",
				players[pid].name, type);
		return;
	}

	if ( (type == T_VIE && l != LEN_GOARENAPACKET_VIE) ||
	          (type == T_CONT && l != LEN_GOARENAPACKET_CONT) )
	{
		lm->Log(L_MALICIOUS,"<arenaman> [%s] Bad arena packet length (%d)",
				players[pid].name, l);
		return;
	}

	if (players[pid].arena != -1)
	{
#if 0
		lm->Log(L_MALICIOUS, "<arenaman> [%s] Recvd arena request from player already in an arena", players[pid].name);
		return;
#endif
		/* stupid cont doesn't send leaving packet...
		 * fake it, and make sure not to set oldarena below, or the old
		 * stuff won't get set right. */
		PLeaving(pid, NULL, 0);
	}

	go = (struct GoArenaPacket*)p;

	if (go->shiptype < 0 || go->shiptype > SPEC)
	{
		lm->Log(L_MALICIOUS, "<arenaman> [%s] Bad shiptype in arena request", players[pid].name);
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
		lm->Log(L_MALICIOUS, "<arenaman> [%s] Bad arenatype in arena request", players[pid].name);
		return;
	}

	LOCK_STATUS();

	/* try to locate an existing arena */
	arena = FindArena(name, ARENA_DO_LOAD_CONFIG, ARENA_RUNNING);

	if (arena == -1)
	{
		lm->Log(L_INFO, "<arenaman> {%s} Creating arena", name);
		arena = CreateArena(name);
		if (arena == -1)
		{
			/* if it fails, dump in first available */
			arena = 0;
			while (arenas[arena].status != ARENA_RUNNING && arena < MAXARENA) arena++;
			if (arena == MAXARENA)
			{
				lm->Log(L_ERROR, "<arenaman> Internal error: no running arenas but cannot create new one");
				UNLOCK_STATUS();
				return;
			}
		}
	}

	/* set up player info */
	players[pid].arena = arena;
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
	lm->Log(L_INFO, "<arenaman> {%s} [%s] Player leaving arena",
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

			lm->Log(L_DRIVEL, "<arenaman> {%s} Arena being destroyed (id=%d)",
					arenas[i].name, i);
			/* set its status so that the arena processor will do
			 * appropriate things */
			arenas[i].status = ARENA_DO_DESTROY_CALLBACKS;
skip: ;
		}
	/* unlock all status info */
	pd->UnlockStatus();
	UNLOCK_STATUS();
	return 1;
}


