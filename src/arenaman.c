
/* dist: public */

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef WIN32
#include <malloc.h>
#endif

#include "asss.h"
#include "clientset.h"


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
local void LockStatus(void);
local void UnlockStatus(void);

local void PArena(int, byte *, int);
local void MArena(int, const char *);
local void PLeaving(int, byte *, int);
local void MLeaving(int, const char *);

local void LeaveArena(int);
local void SendArenaResponse(int);
local int FindArena(const char *name, int *totalcount, int *playing);

/* globals */

local Imainloop *ml;
local Iplayerdata *pd;
local Iconfig *cfg;
local Inet *net;
local Ichatnet *chatnet;
local Imodman *mm;
local Ilogman *lm;
local /* noinit */ Ipersist *persist;

local PlayerData *players;

/* big static arena data array */
local ArenaData arenas[MAXARENA];

local pthread_mutex_t arenastatusmtx;

local Iarenaman _int =
{
	INTERFACE_HEAD_INIT(I_ARENAMAN, "arenaman")
	SendArenaResponse, LeaveArena,
	FindArena,
	LockStatus, UnlockStatus, arenas
};




EXPORT int MM_arenaman(int action, Imodman *mm_, int arena)
{
	pthread_mutexattr_t attr;

	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		chatnet = mm->GetInterface(I_CHATNET, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		if (!pd || !lm || !cfg || !ml) return MM_FAIL;

		players = pd->players;

		memset(arenas, 0, sizeof(ArenaData) * MAXARENA);

		/* init mutexes */
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&arenastatusmtx, &attr);
		pthread_mutexattr_destroy(&attr);

		mm->RegCallback(CB_MAINLOOP, ProcessArenaQueue, ALLARENAS);

		if (net)
		{
			net->AddPacket(C2S_GOTOARENA, PArena);
			net->AddPacket(C2S_LEAVING, PLeaving);
		}
		if (chatnet)
		{
			chatnet->AddHandler("GO", MArena);
			chatnet->AddHandler("LEAVE", MLeaving);
		}


		ml->SetTimer(ReapArenas, 1000, 1500, NULL, -1);

		mm->RegInterface(&_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_POSTLOAD)
	{
		persist = mm->GetInterface(I_PERSIST, ALLARENAS);
	}
	else if (action == MM_PREUNLOAD)
	{
		mm->ReleaseInterface(persist);
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;
		if (net)
		{
			net->RemovePacket(C2S_GOTOARENA, PArena);
			net->RemovePacket(C2S_LEAVING, PLeaving);
		}
		if (chatnet)
		{
			chatnet->RemoveHandler("GO", MArena);
			chatnet->RemoveHandler("LEAVE", MLeaving);
		}
		mm->UnregCallback(CB_MAINLOOP, ProcessArenaQueue, ALLARENAS);
		ml->ClearTimer(ReapArenas, -1);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(chatnet);
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

	/* cfghelp: Modules:AttachModules, arena, string
	 * This is a list of modules that you want to take effect in this
	 * arena. Not all modules need to be attached to arenas to function,
	 * but some do. */
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


local void arena_conf_changed(void *aptr)
{
	int arena = ((ArenaData*)aptr) - arenas;
	/* only running arenas should recieve confchanged events */
	if (arenas[arena].status == ARENA_RUNNING)
		DO_CBS(CB_ARENAACTION, arena, ArenaActionFunc, (arena, AA_CONFCHANGED));
}

local void syncdone1(int arena)
{
	LOCK_STATUS();
	if (arenas[arena].status == ARENA_WAIT_SYNC1)
		arenas[arena].status = ARENA_RUNNING;
	else
		lm->LogA(L_WARN, "arenaman", arena, "syncdone1 called from wrong state");
	UNLOCK_STATUS();
}


local void syncdone2(int arena)
{
	LOCK_STATUS();
	if (arenas[arena].status == ARENA_WAIT_SYNC2)
		arenas[arena].status = ARENA_DO_DEINIT;
	else
		lm->LogA(L_WARN, "arenaman", arena, "syncdone2 called from wrong state");
	UNLOCK_STATUS();
}


void ProcessArenaQueue(void)
{
	int i, j, nextstatus, oops;
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
			case ARENA_WAIT_SYNC1:
			case ARENA_WAIT_SYNC2:
				continue;
		}

		switch (nextstatus)
		{
			case ARENA_DO_INIT:
				/* config file */
				a->cfg = cfg->OpenConfigFile(a->basename, NULL, arena_conf_changed, a);
				/* attach modules */
				DoAttach(i, MM_ATTACH);
				/* now callbacks */
				DO_CBS(CB_ARENAACTION, i, ArenaActionFunc, (i, AA_CREATE));
				/* finally, persistant stuff */
				if (persist)
				{
					persist->GetArena(i, syncdone1);
					nextstatus = ARENA_WAIT_SYNC1;
				}
				else
					nextstatus = ARENA_RUNNING;
				break;

			case ARENA_DO_WRITE_DATA:
				/* make sure there is nobody in here */
				oops = 0;
				for (j = 0; j < MAXPLAYERS; j++)
					if (players[j].status != S_FREE)
						if (players[j].arena == i)
							oops = 1;
				if (!oops)
				{
					if (persist)
					{
						persist->PutArena(i, syncdone2);
						nextstatus = ARENA_WAIT_SYNC2;
					}
					else
						nextstatus = ARENA_DO_DEINIT;
				}
				else
				{
					/* let's not destroy this after all... */
					nextstatus = ARENA_RUNNING;
				}
				break;

			case ARENA_DO_DEINIT:
				/* reverse order: callbacks, detach, close config file */
				DO_CBS(CB_ARENAACTION, i, ArenaActionFunc, (i, AA_DESTROY));
				DoAttach(i, MM_DETACH);
				cfg->CloseConfigFile(a->cfg);
				a->cfg = NULL;
				nextstatus = ARENA_NONE;
				break;
		}

		a->status = nextstatus;
	}
	UNLOCK_STATUS();
}


local int CreateArena(const char *name)
{
	int i = 0;
	char *t;
	ArenaData *a;

	LOCK_STATUS();
	while (arenas[i].status != ARENA_NONE && i < MAXARENA) i++;

	if (i == MAXARENA)
	{
		lm->Log(L_WARN, "<arenaman> Cannot create a new arena: too many arenas");
		UNLOCK_STATUS();
		return -1;
	}

	a = arenas + i;

	astrncpy(a->name, name, 20);

	astrncpy(a->basename, name, 20);
	t = a->basename + strlen(a->basename) - 1;
	while (t > a->basename && isdigit(*t))
		*(t--) = 0;

	a->status = ARENA_DO_INIT;
	a->ispublic = (name[1] == '\0' && name[0] >= '0' && name[0] <= '9');

	UNLOCK_STATUS();

	return i;
}


local inline void send_enter(int pid, int to, int already)
{
	if (IS_STANDARD(to))
		net->SendToOne(to, (byte*)(players+pid), 64, NET_RELIABLE);
	else if (IS_CHAT(to))
		chatnet->SendToOne(to, "%s:%s:%d:%d",
				already ? "PLAYER" : "ENTERING",
				players[pid].name,
				players[pid].shiptype,
				players[pid].freq);
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

	if (IS_STANDARD(pid))
	{
		/* send whoami packet */
		whoami.d1 = pid;
		net->SendToOne(pid, (byte*)&whoami, 3, NET_RELIABLE);

		/* send settings */
		{
			Iclientset *clientset = mm->GetInterface(I_CLIENTSET, arena);
			if (clientset)
				clientset->SendClientSettings(pid);
			mm->ReleaseInterface(clientset);
		}
	}
	else if (IS_CHAT(pid))
	{
		chatnet->SendToOne(pid, "INARENA:%s:%d",
				arenas[arena].name,
				players[pid].freq);
	}

	pd->LockStatus();
	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (players[i].status == S_PLAYING &&
		    players[i].arena == arena &&
		    i != pid )
		{
			/* send each other info */
			send_enter(i, pid, 1);
			send_enter(pid, i, 0);
		}
	}
	pd->UnlockStatus();

	if (IS_STANDARD(pid))
	{
		/* send mapfilename */
		Imapnewsdl *map = mm->GetInterface(I_MAPNEWSDL, arena);

		/* send to himself */
		net->SendToOne(pid, (byte*)(players+pid), 64, NET_RELIABLE);

		if (map) map->SendMapFilename(pid);
		mm->ReleaseInterface(map);

		/* send brick clear and finisher */
		whoami.type = S2C_BRICK;
		net->SendToOne(pid, (byte*)&whoami, 1, NET_RELIABLE);
		whoami.type = S2C_ENTERINGARENA;
		net->SendToOne(pid, (byte*)&whoami, 1, NET_RELIABLE);
	}
}


local int do_find_arena(const char *name, int min, int max)
{
	int i;
	LOCK_STATUS();
	for (i = 0; i < MAXARENA; i++)
		if (arenas[i].status >= min &&
		    arenas[i].status <= max &&
		    !strcmp(arenas[i].name, name) )
		{
			UNLOCK_STATUS();
			return i;
		}
	UNLOCK_STATUS();
	return -1;
}


local void count_players(int arena, int *totalp, int *playingp)
{
	int total = 0, playing = 0, pid;

	pd->LockStatus();
	for (pid = 0; pid < MAXPLAYERS; pid++)
	{
		if (pd->players[pid].status == S_PLAYING &&
				pd->players[pid].arena == arena)
		{
			total++;
			if (pd->players[pid].shiptype != SPEC)
				playing++;
		}
	}
	pd->UnlockStatus();

	if (totalp) *totalp = total;
	if (playingp) *playingp = playing;
}


local void complete_go(int pid, const char *name, int ship, int xres, int yres, int gfx)
{
	/* status should be S_LOGGEDIN or S_PLAYING at this point */
	int arena;

	if (players[pid].status != S_LOGGEDIN && players[pid].status != S_PLAYING)
	{
		lm->Log(L_MALICIOUS, "<arenaman> [pid=%d] Sent arena request from bad status (%d)",
				pid, players[pid].status);
		return;
	}

	if (players[pid].arena != -1)
		LeaveArena(pid);

	LOCK_STATUS();

	/* try to locate an existing arena */
	arena = do_find_arena(name, ARENA_DO_INIT, ARENA_RUNNING);

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
	players[pid].shiptype = ship;
	players[pid].xres = xres;
	players[pid].yres = yres;
	gfx ? SET_ALL_LVZ(pid) : UNSET_ALL_LVZ(pid);

	/* don't mess with player status yet, let him stay in S_LOGGEDIN.
	 * it will be incremented when the arena is ready. */
	UNLOCK_STATUS();
}


void PArena(int pid, byte *p, int l)
{
	struct GoArenaPacket *go;
	char name[16];

#ifdef CFG_RELAX_LENGTH_CHECKS
	if (l != LEN_GOARENAPACKET_VIE && l != LEN_GOARENAPACKET_CONT)
#else
	int type = players[pid].type;
	if ( (type == T_VIE && l != LEN_GOARENAPACKET_VIE) ||
	          (type == T_CONT && l != LEN_GOARENAPACKET_CONT) )
#endif
	{
		lm->Log(L_MALICIOUS,"<arenaman> [%s] Bad arena packet length (%d)",
				players[pid].name, l);
		return;
	}

	go = (struct GoArenaPacket*)p;

	if (go->shiptype < 0 || go->shiptype > SPEC)
	{
		lm->Log(L_MALICIOUS, "<arenaman> [%s] Bad shiptype in arena request", players[pid].name);
		return;
	}

	/* make a name from the request */
	if (go->arenatype == -3)
	{
		char *t;
		astrncpy(name, go->arenaname, 16);
		/* set all illegal characters to underscores, and lowercase name */
		for (t = name; *t; t++)
			if (!isalnum(*t) && !strchr("-_#@", *t))
				*t = '_';
			else if (isupper(*t))
				*t = tolower(*t);
	}
	else if (go->arenatype == -2 || go->arenatype == -1)
	{
		Iarenaplace *ap = mm->GetInterface(I_ARENAPLACE, ALLARENAS);
		if (ap)
		{
			if (!ap->Place(name, sizeof(name), pid))
				strcpy(name, "0");
			mm->ReleaseInterface(ap);
		}
		else
			strcpy(name, "0");
	}
	else if (go->arenatype >= 0 && go->arenatype <= 9)
	{
		name[0] = go->arenatype + '0';
		name[1] = 0;
	}
	else
	{
		lm->Log(L_MALICIOUS, "<arenaman> [%s] Bad arenatype in arena request", players[pid].name);
		return;
	}

	complete_go(pid, name, go->shiptype, go->xres, go->yres, go->optionalgraphics);
}


void MArena(int pid, const char *line)
{
	complete_go(pid, line[0] ? line : "0", SPEC, 0, 0, 0);
}


void LeaveArena(int pid)
{
	int arena;
	struct SimplePacket pk = { S2C_PLAYERLEAVING, pid };

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

	if (net) net->SendToArena(arena, pid, (byte*)&pk, 3, NET_RELIABLE);
	if (chatnet) chatnet->SendToArena(arena, pid, "LEAVING:%s", players[pid].name);
	lm->Log(L_INFO, "<arenaman> {%s} [%s] Player leaving arena",
			arenas[arena].name, players[pid].name);
}


void PLeaving(int pid, byte *p, int q)
{
	LeaveArena(pid);
}

void MLeaving(int pid, const char *l)
{
	LeaveArena(pid);
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
				if (players[j].status != S_FREE &&
				    players[j].arena == i)
					goto skip;

			lm->Log(L_DRIVEL, "<arenaman> {%s} Arena being destroyed (id=%d)",
					arenas[i].name, i);
			/* set its status so that the arena processor will do
			 * appropriate things */
			arenas[i].status = ARENA_DO_WRITE_DATA;
skip: ;
		}
	/* unlock all status info */
	pd->UnlockStatus();
	UNLOCK_STATUS();
	return 1;
}


int FindArena(const char *name, int *totalp, int *playingp)
{
	int arena;

	arena = do_find_arena(name, ARENA_RUNNING, ARENA_RUNNING);

	if (ARENA_OK(arena))
		count_players(arena, totalp, playingp);

	return arena;
}


