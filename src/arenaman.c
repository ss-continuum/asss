
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


/* macros */

#define RDLOCK() pthread_rwlock_rdlock(&arenalock)
#define WRLOCK() pthread_rwlock_wrlock(&arenalock)
#define UNLOCK() pthread_rwlock_unlock(&arenalock)

/* globals */

local Imainloop *ml;
local Iplayerdata *pd;
local Iconfig *cfg;
local Inet *net;
local Ichatnet *chatnet;
local Imodman *mm;
local Ilogman *lm;
local /* noinit */ Ipersist *persist;

typedef struct { short x, y; } spawnloc;
local int spawnkey;

/* the read-write lock for the global arena list */
local pthread_rwlock_t arenalock;

/* stuff to keep track of private per-arena memory */
local int perarenaspace;

/* forward declaration */
local Iarenaman myint;


local void DoAttach(Arena *a, int action)
{
	void (*func)(const char *name, Arena *arena);
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
	attmods = cfg->GetStr(a->cfg, "Modules", "AttachModules");
	if (!attmods) return;

	mods = alloca(strlen(attmods)+1);
	strcpy(mods, attmods);

#define DELIMS " \t:;,"

	t = strtok_r(mods, DELIMS, &_tok);
	while (t)
	{
		func(t, a); /* attach or detach modules */
		t = strtok_r(NULL, DELIMS, &_tok);
	}
}


local void free_arena(const void *v)
{
	const Arena *a = v;
	afree(a);
}


local void arena_conf_changed(void *v)
{
	Arena *a = v;
	/* only running arenas should recieve confchanged events */
	if (a->status == ARENA_RUNNING)
		DO_CBS(CB_ARENAACTION, a, ArenaActionFunc, (a, AA_CONFCHANGED));
}

local void syncdone1(Arena *a)
{
	RDLOCK();
	if (a)
	{
		if (a->status == ARENA_WAIT_SYNC1)
			a->status = ARENA_RUNNING;
		else
			lm->LogA(L_WARN, "arenaman", a, "syncdone1 called from wrong state");
	}
	else
		lm->Log(L_WARN, "<arenaman> syncdone1 called for bad arena");
	UNLOCK();
}


local void syncdone2(Arena *a)
{
	RDLOCK();
	if (a)
	{
		if (a->status == ARENA_WAIT_SYNC2)
			a->status = ARENA_DO_DEINIT;
		else
			lm->LogA(L_WARN, "arenaman", a, "syncdone2 called from wrong state");
	}
	else
		lm->Log(L_WARN, "<arenaman> syncdone2 called for bad arena");
	UNLOCK();
}


local int ProcessArenaStates(void *dummy)
{
	int status, oops;
	Link *link, *next;
	Arena *a;
	Player *p;

	WRLOCK();
	for (link = LLGetHead(&myint.arenalist); link; link = next)
	{
		a = link->data;
		next = link->next;

		/* get the status */
		status = a->status;

		switch (status)
		{
			case ARENA_RUNNING:
			case ARENA_WAIT_SYNC1:
			case ARENA_WAIT_SYNC2:
				continue;
		}

		switch (status)
		{
			case ARENA_DO_INIT:
				/* config file */
				a->cfg = cfg->OpenConfigFile(a->basename, NULL, arena_conf_changed, a);
				/* attach modules */
				DoAttach(a, MM_ATTACH);
				/* now callbacks */
				DO_CBS(CB_ARENAACTION, a, ArenaActionFunc, (a, AA_CREATE));
				/* finally, persistant stuff */
				if (persist)
				{
					persist->GetArena(a, syncdone1);
					a->status = ARENA_WAIT_SYNC1;
				}
				else
					a->status = ARENA_RUNNING;
				break;

			case ARENA_DO_WRITE_DATA:
				/* make sure there is nobody in here */
				oops = 0;
				pd->Lock();
				FOR_EACH_PLAYER(p)
					if (p->arena == a)
					{ oops = 1; break; }
				pd->Unlock();
				if (!oops)
				{
					if (persist)
					{
						persist->PutArena(a, syncdone2);
						a->status = ARENA_WAIT_SYNC2;
					}
					else
						a->status = ARENA_DO_DEINIT;
				}
				else
				{
					/* let's not destroy this after all... */
					a->status = ARENA_RUNNING;
				}
				break;

			case ARENA_DO_DEINIT:
				/* reverse order: callbacks, detach, close config file */
				DO_CBS(CB_ARENAACTION, a, ArenaActionFunc, (a, AA_DESTROY));
				DoAttach(a, MM_DETACH);
				cfg->CloseConfigFile(a->cfg);

				LLRemove(&myint.arenalist, a);

				free_arena(a);

				break;
		}
	}
	UNLOCK();

	return TRUE;
}


local Arena * CreateArena(const char *name)
{
	char *t;
	Arena *a;

	a = amalloc(sizeof(*a) + perarenaspace);

	astrncpy(a->name, name, 20);
	astrncpy(a->basename, name, 20);
	t = a->basename + strlen(a->basename) - 1;
	while ((t > a->basename) && isdigit(*t))
		*(t--) = 0;

	a->status = ARENA_DO_INIT;
	a->ispublic = (name[1] == '\0' && name[0] >= '0' && name[0] <= '9');
	a->cfg = NULL;

	WRLOCK();
	LLAdd(&myint.arenalist, a);
	UNLOCK();

	return a;
}


local inline void send_enter(Player *p, Player *to, int already)
{
	if (IS_STANDARD(to))
		net->SendToOne(to, (byte*)(&p->pkt), 64, NET_RELIABLE);
	else if (IS_CHAT(to))
		chatnet->SendToOne(to, "%s:%s:%d:%d",
				already ? "PLAYER" : "ENTERING",
				p->name,
				p->p_ship,
				p->p_freq);
}


local void SendArenaResponse(Player *p)
{
	/* LOCK: maybe should lock more in here? */
	struct SimplePacket whoami = { S2C_WHOAMI, 0 };
	Arena *a;
	Player *op;
	Link *link;

	a = p->arena;
	if (!a)
	{
		lm->Log(L_WARN, "<arenaman> [%s] bad arena in SendArenaResponse", p->name);
		return;
	}

	lm->Log(L_INFO, "<arenaman> {%s} [%s] entering arena", a->name, p->name);

	if (IS_STANDARD(p))
	{
		/* send whoami packet */
		whoami.d1 = p->pid;
		net->SendToOne(p, (byte*)&whoami, 3, NET_RELIABLE);

		/* send settings */
		{
			Iclientset *clientset = mm->GetInterface(I_CLIENTSET, a);
			if (clientset)
				clientset->SendClientSettings(p);
			mm->ReleaseInterface(clientset);
		}
	}
	else if (IS_CHAT(p))
	{
		chatnet->SendToOne(p, "INARENA:%s:%d", a->name, p->p_freq);
	}

	pd->Lock();
	FOR_EACH_PLAYER(op)
		if (op->status == S_PLAYING &&
		    op->arena == a &&
		    op != p )
		{
			/* send each other info */
			send_enter(op, p, 1);
			send_enter(p, op, 0);
		}
	pd->Unlock();

	if (IS_STANDARD(p))
	{
		spawnloc *sp = PPDATA(p, spawnkey);

		/* send mapfilename */
		Imapnewsdl *map = mm->GetInterface(I_MAPNEWSDL, a);

		/* send to himself */
		net->SendToOne(p, (byte*)(&p->pkt), 64, NET_RELIABLE);

		if (map) map->SendMapFilename(p);
		mm->ReleaseInterface(map);

		/* send brick clear and finisher */
		whoami.type = S2C_BRICK;
		net->SendToOne(p, (byte*)&whoami, 1, NET_RELIABLE);

		whoami.type = S2C_ENTERINGARENA;
		net->SendToOne(p, (byte*)&whoami, 1, NET_RELIABLE);

		if (sp->x > 0 && sp->y > 0)
		{
			struct SimplePacket wto = { S2C_WARPTO, sp->x, sp->y };
			net->SendToOne(p, (byte *)&wto, 5, NET_RELIABLE | NET_PRI_P3);
		}
	}
}


local void LeaveArena(Player *p)
{
	Arena *a;
	struct SimplePacket pk = { S2C_PLAYERLEAVING, p->pid };

	pd->Lock();
	a = p->arena;
	if (p->status != S_PLAYING || a == NULL)
	{
		pd->Unlock();
		return;
	}

	p->oldarena = a;
	/* this needs to be done for some good reason. i think it has to do
	 * with KillConnection in net. */
	p->arena = NULL;
	p->status = S_LEAVING_ARENA;

	pd->Unlock();

	if (net) net->SendToArena(a, p, (byte*)&pk, 3, NET_RELIABLE);
	if (chatnet) chatnet->SendToArena(a, p, "LEAVING:%s", p->name);
	lm->Log(L_INFO, "<arenaman> {%s} [%s] leaving arena",
			a->name, p->name);
}


local Arena * do_find_arena(const char *name, int min, int max)
{
	Link *link;
	Arena *a;
	RDLOCK();
	for (
			link = LLGetHead(&myint.arenalist);
			link && ((a = link->data) || 1);
			link = link->next)
		if (a->status >= min &&
		    a->status <= max &&
		    !strcmp(a->name, name) )
		{
			UNLOCK();
			return a;
		}
	UNLOCK();
	return NULL;
}


local void count_players(Arena *a, int *totalp, int *playingp)
{
	int total = 0, playing = 0;
	Player *p;
	Link *link;

	pd->Lock();
	FOR_EACH_PLAYER(p)
		if (p->status == S_PLAYING &&
		    p->arena == a)
		{
			total++;
			if (p->p_ship != SPEC)
				playing++;
		}
	pd->Unlock();

	if (totalp) *totalp = total;
	if (playingp) *playingp = playing;
}


local void complete_go(Player *p, const char *name, int ship, int xres, int yres, int gfx,
		int spawnx, int spawny)
{
	/* status should be S_LOGGEDIN or S_PLAYING at this point */
	spawnloc *sp = PPDATA(p, spawnkey);
	Arena *a;

	if (p->status != S_LOGGEDIN && p->status != S_PLAYING)
	{
		lm->LogP(L_MALICIOUS, "arenaman", p, "Sent arena request from bad status (%d)",
				p->status);
		return;
	}

	if (p->arena != NULL)
		LeaveArena(p);

	/* try to locate an existing arena */
	a = do_find_arena(name, ARENA_DO_INIT, ARENA_RUNNING);

	if (a == NULL)
	{
		lm->Log(L_INFO, "<arenaman> {%s} Creating arena", name);
		a = CreateArena(name);
		if (a == NULL)
		{
			/* if it fails, dump in first available */
			Link *l = LLGetHead(&myint.arenalist);
			if (!l)
			{
				lm->Log(L_ERROR, "<arenaman> Internal error: no running arenas but cannot create new one");
				return;
			}
			a = l->data;
		}
	}

	/* set up player info */
	p->arena = a;
	p->p_ship = ship;
	p->xres = xres;
	p->yres = yres;
	gfx ? SET_ALL_LVZ(p) : UNSET_ALL_LVZ(p);
	sp->x = spawnx;
	sp->y = spawny;

	/* don't mess with player status yet, let him stay in S_LOGGEDIN.
	 * it will be incremented when the arena is ready. */
}


local void PArena(Player *p, byte *pkt, int l)
{
	struct GoArenaPacket *go;
	char name[16];

#ifdef CFG_RELAX_LENGTH_CHECKS
	if (l != LEN_GOARENAPACKET_VIE && l != LEN_GOARENAPACKET_CONT)
#else
	int type = p->type;
	if ( (type == T_VIE && l != LEN_GOARENAPACKET_VIE) ||
	          (type == T_CONT && l != LEN_GOARENAPACKET_CONT) )
#endif
	{
		lm->Log(L_MALICIOUS,"<arenaman> [%s] Bad arena packet length (%d)",
				p->name, l);
		return;
	}

	go = (struct GoArenaPacket*)pkt;

	if (p->p_ship > SPEC)
	{
		lm->Log(L_MALICIOUS, "<arenaman> [%s] Bad p_ship in arena request", p->name);
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
			if (!ap->Place(name, sizeof(name), p))
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
		lm->Log(L_MALICIOUS, "<arenaman> [%s] Bad arenatype in arena request", p->name);
		return;
	}

	complete_go(p, name, p->p_ship, go->xres, go->yres, go->optionalgraphics, 0, 0);
}


local void MArena(Player *p, const char *line)
{
	complete_go(p, line[0] ? line : "0", SPEC, 0, 0, 0, 0, 0);
}


local void SendToArena(Player *p, const char *aname, int spawnx, int spawny)
{
	int ship = p->p_ship;
	int xres = p->xres;
	int yres = p->yres;
	int gfx = WANT_ALL_LVZ(p);

	if (p->type == T_CONT)
		complete_go(p, aname, ship, xres, yres, gfx, spawnx, spawny);
}

local void PLeaving(Player *p, byte *pkt, int q)
{
	LeaveArena(p);
}

local void MLeaving(Player *p, const char *l)
{
	LeaveArena(p);
}


local int ReapArenas(void *q)
{
	Link *link;
	Arena *a;
	Player *p;

	RDLOCK();
	pd->Lock();
	for (
			link = LLGetHead(&myint.arenalist);
			link && ((a = link->data) || 1);
			link = link->next)
		if (a->status == ARENA_RUNNING)
		{
			FOR_EACH_PLAYER(p)
				if (p->arena == a)
					goto skip;

			lm->Log(L_DRIVEL, "<arenaman> {%s} Arena being destroyed", a->name);
			/* set its status so that the arena processor will do
			 * appropriate things */
			a->status = ARENA_DO_WRITE_DATA;
skip: ;
		}
	pd->Unlock();
	UNLOCK();

	return TRUE;
}


local Arena * FindArena(const char *name, int *totalp, int *playingp)
{
	Arena *arena;

	arena = do_find_arena(name, ARENA_RUNNING, ARENA_RUNNING);

	if (arena)
		count_players(arena, totalp, playingp);

	return arena;
}


local LinkedList blocks;
struct block
{
	int start, len;
};

local int AllocateArenaData(size_t bytes)
{
	Link *l, *last = NULL;
	struct block *b, *nb;
	int current = 0;

	/* round up to next multiple of 4 */
	bytes = (bytes+3) & (~3);

	WRLOCK();
	/* first try before between two blocks (or at the beginning) */
	for (l = LLGetHead(&blocks); l; l = l->next)
	{
		b = l->data;
		if ((b->start - current) >= bytes)
		{
			nb = amalloc(sizeof(*nb));
			nb->start = current;
			nb->len = bytes;
			/* if last == NULL, this will put it in front of the list */
			LLInsertAfter(&blocks, last, nb);
			UNLOCK();
			return current;
		}
		else
			current = b->start + b->len;
		last = l;
	}

	/* if we couldn't get in between two blocks, try at the end */
	if ((perarenaspace - current) >= bytes)
	{
		nb = amalloc(sizeof(*nb));
		nb->start = current;
		nb->len = bytes;
		LLInsertAfter(&blocks, last, nb);
		UNLOCK();
		return current;
	}

	UNLOCK();
	return -1;
}


local void FreeArenaData(int key)
{
	Link *l;
	WRLOCK();
	for (l = LLGetHead(&blocks); l; l = l->next)
	{
		struct block *b = l->data;
		if (b->start == key)
		{
			LLRemove(&blocks, b);
			afree(b);
			break;
		}
	}
	UNLOCK();
}


local void Lock(void)
{
	RDLOCK();
}

local void Unlock(void)
{
	UNLOCK();
}


local Iarenaman myint =
{
	INTERFACE_HEAD_INIT(I_ARENAMAN, "arenaman")
	SendArenaResponse, LeaveArena,
	SendToArena, FindArena,
	AllocateArenaData, FreeArenaData,
	Lock, Unlock
};

EXPORT int MM_arenaman(int action, Imodman *mm_, Arena *a)
{
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

		spawnkey = pd->AllocatePlayerData(sizeof(spawnloc));
		if (spawnkey == -1) return MM_FAIL;

		LLInit(&myint.arenalist);

		pthread_rwlock_init(&arenalock, NULL);

		LLInit(&blocks);
		perarenaspace = cfg->GetInt(GLOBAL, "General", "PerArenaBytes", 10000);

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

		ml->SetTimer(ProcessArenaStates, 10, 10, NULL, NULL);
		ml->SetTimer(ReapArenas, 1000, 1500, NULL, NULL);

		mm->RegInterface(&myint, ALLARENAS);
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
		if (mm->UnregInterface(&myint, ALLARENAS))
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
		ml->ClearTimer(ProcessArenaStates, NULL);
		ml->ClearTimer(ReapArenas, NULL);
		pd->FreePlayerData(spawnkey);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(chatnet);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(ml);

		pthread_rwlock_destroy(&arenalock);
		LLEnum(&myint.arenalist, free_arena);
		LLEmpty(&myint.arenalist);
		return MM_OK;
	}
	return MM_FAIL;
}


