
/* dist: public */

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "asss.h"
#include "clientset.h"
#include "persist.h"

#include "packets/goarena.h"


/* macros */

#define RDLOCK() pthread_rwlock_rdlock(&arenalock)
#define WRLOCK() pthread_rwlock_wrlock(&arenalock)
#define UNLOCK() pthread_rwlock_unlock(&arenalock)

/* let us use arenaman.h macros normally */
#define aman (&myint)

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

typedef struct { byte resurrect; } adata;
local int adkey;

/* forward declaration */
local Iarenaman myint;


local void do_attach(Arena *a, int action)
{
	char mod[32];
	const char *attmods, *tmp = NULL;
	void (*func)(const char *name, Arena *arena);
	
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

	while (strsplit(attmods, " \t:;,", mod, sizeof(mod), &tmp))
		func(mod, a);
}


local void arena_conf_changed(void *v)
{
	Arena *a = v;

	/* only running arenas should recieve confchanged events */
	if (a->status == ARENA_RUNNING)
		DO_CBS(CB_ARENAACTION, a, ArenaActionFunc, (a, AA_CONFCHANGED));
}


local void arena_sync_done(Arena *a)
{
	WRLOCK();
	if (a->status == ARENA_WAIT_SYNC1)
		a->status = ARENA_RUNNING;
	else if (a->status == ARENA_WAIT_SYNC2)
		a->status = ARENA_DO_DEINIT;
	else
		lm->LogA(L_WARN, "arenaman", a, "arena_sync_done called from wrong state");
	UNLOCK();
}


local int ProcessArenaStates(void *dummy)
{
	int status, oops;
	Link *link;
	Arena *a;
	adata *ad;
	Player *p;

	WRLOCK();
	FOR_EACH_ARENA_P(a, ad, adkey)
	{
		/* get the status */
		status = a->status;

		switch (status)
		{
			case ARENA_RUNNING:
			case ARENA_CLOSING:
			case ARENA_WAIT_SYNC1:
			case ARENA_WAIT_SYNC2:
				continue;
		}

		switch (status)
		{
			case ARENA_DO_INIT:
				/* config file */
				a->cfg = cfg->OpenConfigFile(a->basename, NULL, arena_conf_changed, a);
				/* cfghelp: Team:SpectatorFrequency, arena, int, range: 0-9999, def: 8025
				 * The frequency that spectators are assigned to, by default. */
				a->specfreq = cfg->GetInt(a->cfg, "Team", "SpectatorFrequency", CFG_DEF_SPEC_FREQ);
				/* attach modules */
				do_attach(a, MM_ATTACH);
				/* now callbacks */
				DO_CBS(CB_ARENAACTION, a, ArenaActionFunc, (a, AA_CREATE));
				/* finally, persistant stuff */
				if (persist)
				{
					persist->GetArena(a, arena_sync_done);
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
						persist->PutArena(a, arena_sync_done);
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
				do_attach(a, MM_DETACH);
				cfg->CloseConfigFile(a->cfg);
				a->cfg = NULL;

				if (ad->resurrect)
				{
					/* clear all private data on recycle, so it looks to
					 * modules like it was just created. */
					memset(a->arenaextradata, 0, perarenaspace);
					ad->resurrect = FALSE;
					a->status = ARENA_DO_INIT;
				}
				else
				{
					LLRemove(&aman->arenalist, a);
					afree(a);
				}

				break;
		}
	}
	UNLOCK();

	return TRUE;
}


/* call with write lock held */
local Arena * create_arena(const char *name)
{
	char *t;
	Arena *a;
	adata *ad;

	a = amalloc(sizeof(*a) + perarenaspace);
	ad = P_ARENA_DATA(a, adkey);

	astrncpy(a->name, name, 20);
	astrncpy(a->basename, name, 20);
	t = a->basename + strlen(a->basename) - 1;
	while ((t >= a->basename) && isdigit(*t))
		*(t--) = 0;
	if (t < a->basename)
		astrncpy(a->basename, AG_PUBLIC, 20);

	a->status = ARENA_DO_INIT;
	a->cfg = NULL;
	ad->resurrect = FALSE;

	LLAdd(&aman->arenalist, a);

	lm->Log(L_INFO, "<arenaman> {%s} created arena", name);

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
		if ((op->status == S_PLAYING || op->status == S_DO_ARENA_CALLBACKS) &&
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
		Imapnewsdl *map = mm->GetInterface(I_MAPNEWSDL, a);

		/* send to himself */
		net->SendToOne(p, (byte*)(&p->pkt), 64, NET_RELIABLE);

		if (map)
		{
			map->SendMapFilename(p);
			mm->ReleaseInterface(map);
		}

		/* send brick clear and finisher */
		whoami.type = S2C_BRICK;
		net->SendToOne(p, (byte*)&whoami, 1, NET_RELIABLE);

		whoami.type = S2C_ENTERINGARENA;
		net->SendToOne(p, (byte*)&whoami, 1, NET_RELIABLE);

		if (sp->x > 0 && sp->y > 0 && sp->x < 1024 && sp->y < 1024)
		{
			struct SimplePacket wto = { S2C_WARPTO, sp->x, sp->y };
			net->SendToOne(p, (byte *)&wto, 5, NET_RELIABLE);
		}
	}
}


local int initiate_leave_arena(Player *p)
{
	int notify = FALSE;

	/* this messy logic attempts to deal with players who haven't fully
	 * entered an arena yet. it will try to insert them at the proper
	 * stage of the arena leaving process so things that have been done
	 * get undone, and things that haven't been done _don't_ get undone. */
	switch (p->status)
	{
		case S_LOGGEDIN:
		case S_DO_FREQ_AND_ARENA_SYNC:
			/* for these 2, nothing much has been done. just go back to
			 * loggedin. */
			p->status = S_LOGGEDIN;
			break;
		case S_WAIT_ARENA_SYNC1:
			/* this is slightly tricky: we want to wait until persist is
			 * done loading the scores before changing the state, or
			 * things will get screwed up. so mark it here and let core
			 * take care of it. this is really messy and it would be
			 * nice to find a better way to handle it. */
			p->flags.leave_arena_when_done_waiting = 1;
			break;
		case S_SEND_ARENA_RESPONSE:
			/* in these, stuff has come out of the database. put it back
			 * in. */
			p->status = S_DO_ARENA_SYNC2;
			break;
		case S_DO_ARENA_CALLBACKS:
			/* put stuff back in database and also notify other players
			 * of the leaving player. */
			p->status = S_DO_ARENA_SYNC2;
			notify = TRUE;
			break;
		case S_PLAYING:
			/* do all of the above, plus call leaving callbacks. */
			p->status = S_LEAVING_ARENA;
			notify = TRUE;
			break;

		case S_LEAVING_ARENA:
		case S_DO_ARENA_SYNC2:
		case S_WAIT_ARENA_SYNC2:
			/* no problem, player is already on the way out */
			break;

		default:
			/* something's wrong here */
			lm->LogP(L_ERROR, "arenaman", p, "player has an arena, but in bad state (%d)", p->status);
			notify = TRUE;
			break;
	}

	return notify;
}


local void LeaveArena(Player *p)
{
	int notify;
	Arena *a;

	pd->WriteLock();

	a = p->arena;
	if (!a)
	{
		pd->WriteUnlock();
		return;
	}

	notify = initiate_leave_arena(p);

	pd->WriteUnlock();

	if (notify)
	{
		struct SimplePacket pk = { S2C_PLAYERLEAVING, p->pid };
		if (net) net->SendToArena(a, p, (byte*)&pk, 3, NET_RELIABLE);
		if (chatnet) chatnet->SendToArena(a, p, "LEAVING:%s", p->name);
		lm->Log(L_INFO, "<arenaman> {%s} [%s] leaving arena", a->name, p->name);
	}
}


local int RecycleArena(Arena *a)
{
	Link *link;
	Player *p;
	adata *ad = P_ARENA_DATA(a, adkey);

	WRLOCK();

	if (a->status != ARENA_RUNNING)
	{
		UNLOCK();
		return MM_FAIL;
	}

	pd->WriteLock();

	FOR_EACH_PLAYER(p)
		if (p->arena == a &&
		    !IS_STANDARD(p) &&
		    !IS_CHAT(p))
		{
			pd->WriteUnlock();
			UNLOCK();
			lm->LogA(L_WARN, "arenaman", a, "can't recycle arena with fake players");
			return MM_FAIL;
		}

	/* first move playing players elsewhere */
	FOR_EACH_PLAYER(p)
		if (p->arena == a)
		{
			struct
			{
				u8 type;
				i16 pid;
			} pkt = { S2C_WHOAMI, p->pid };

			/* send whoami packet so the clients leave the arena */
			if (IS_STANDARD(p))
				net->SendToOne(p, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);
			else if (IS_CHAT(p))
				chatnet->SendToOne(p, "INARENA:%s:%d", a->name, p->p_freq);

			/* actually initiate the client leaving arena on our side */
			initiate_leave_arena(p);

			/* and mark the same arena as his desired arena to enter */
			p->newarena = a;
		}

	pd->WriteUnlock();

	/* then tell it to die and then resurrect itself */
	a->status = ARENA_CLOSING;
	ad->resurrect = TRUE;

	UNLOCK();

	return MM_OK;
}


/* call with read or write lock held */
local Arena * do_find_arena(const char *name, int min, int max)
{
	Link *link;
	Arena *a;
	FOR_EACH_ARENA(a)
		if (a->status >= min &&
		    a->status <= max &&
		    !strcmp(a->name, name) )
			return a;
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
		    p->arena == a &&
		    p->type != T_FAKE)
		{
			total++;
			if (p->p_ship != SHIP_SPEC)
				playing++;
		}
	pd->Unlock();

	if (totalp) *totalp = total;
	if (playingp) *playingp = playing;
}


local void complete_go(Player *p, const char *reqname, int ship,
		int xres, int yres, int gfx, int voices, int spawnx, int spawny)
{
	char name[16], *t;

	/* status should be S_LOGGEDIN or S_PLAYING at this point */
	spawnloc *sp = PPDATA(p, spawnkey);
	Arena *a;

	if (p->status != S_LOGGEDIN && p->status != S_PLAYING && p->status != S_LEAVING_ARENA)
	{
		lm->LogP(L_MALICIOUS, "arenaman", p, "sent arena request from bad status (%d)",
				p->status);
		return;
	}

	/* remove all illegal characters, and lowercase name */
	astrncpy(name, reqname, sizeof(name));
	for (t = name; *t; t++)
		if (*t == '#' && t == name)
			/* initial pound sign allowed */;
		else if (!isalnum(*t))
			*t = 'x';
		else if (isupper(*t))
			*t = tolower(*t);
	if (name[0] == '\0')
		strcpy(name, "x");

	if (p->arena != NULL)
		LeaveArena(p);

	/* try to locate an existing arena */
	WRLOCK();
	a = do_find_arena(name, ARENA_DO_INIT, ARENA_DO_DEINIT);

	if (a == NULL)
	{
		a = create_arena(name);
		if (a == NULL)
		{
			/* if it fails, dump in first available */
			Link *l = LLGetHead(&aman->arenalist);
			if (!l)
			{
				lm->Log(L_ERROR,
						"<arenaman> internal error: no running arenas but cannot create new one");
				UNLOCK();
				return;
			}
			a = l->data;
		}
	}
	else if (a->status > ARENA_RUNNING)
	{
		/* if we caught an arena on the way out, no problem, just make
		 * sure it cycles back into existence. */
		adata *ad = P_ARENA_DATA(a, adkey);
		ad->resurrect = TRUE;
	}

	/* set up player info */
	pd->WriteLock();
	p->newarena = a;
	pd->WriteUnlock();

	p->p_ship = ship;
	p->xres = xres;
	p->yres = yres;
	p->flags.want_all_lvz = gfx;
	p->pkt.acceptaudio = voices;
	sp->x = spawnx;
	sp->y = spawny;

	UNLOCK();

	/* don't mess with player status yet, let him stay in S_LOGGEDIN.
	 * it will be incremented when the arena is ready. */
}


local int has_cap_go(Player *p)
{
	/* check capability to ?go anywhere */
	Icapman *capman = mm->GetInterface(I_CAPMAN, ALLARENAS);
	int has = capman ? capman->HasCapability(p, "cmd_go") : TRUE;
	mm->ReleaseInterface(capman);
	return has;
}


local void PArena(Player *p, byte *pkt, int len)
{
	struct GoArenaPacket *go = (struct GoArenaPacket*)pkt;
	char name[16];
	int spx = 0, spy = 0;

	if (len != LEN_GOARENAPACKET_VIE && len != LEN_GOARENAPACKET_CONT)
	{
		lm->LogP(L_MALICIOUS, "arenaman", p, " bad arena packet len=%d", len);
		return;
	}

	if (go->shiptype > SHIP_SPEC)
	{
		lm->Log(L_MALICIOUS, "<arenaman> [%s] bad shiptype in arena request", p->name);
		return;
	}

	/* make a name from the request */
	if (go->arenatype == -3)
	{
		if (!has_cap_go(p)) return;
		astrncpy(name, go->arenaname, 16);
	}
	else if (go->arenatype == -2 || go->arenatype == -1)
	{
		Iarenaplace *ap = mm->GetInterface(I_ARENAPLACE, ALLARENAS);
		if (ap)
		{
			if (!ap->Place(name, sizeof(name), &spx, &spy, p))
				strcpy(name, "0");
			mm->ReleaseInterface(ap);
		}
		else
			strcpy(name, "0");
	}
	else if (go->arenatype >= 0 /* && go->arenatype <= 0x7fff */)
	{
		if (!has_cap_go(p)) return;
		snprintf(name, sizeof(name), "%d", go->arenatype);
	}
	else
	{
		lm->Log(L_MALICIOUS, "<arenaman> [%s] bad arenatype in arena request", p->name);
		return;
	}

	/* only use extra byte if it's there */
	complete_go(p, name, go->shiptype, go->xres, go->yres,
			(len >= LEN_GOARENAPACKET_CONT) ? go->optionalgraphics : 0,
			go->wavmsg, spx, spy);
}


local void MArena(Player *p, const char *line)
{
	complete_go(p, line[0] ? line : "0", SHIP_SPEC, 0, 0, 0, 0, 0, 0);
}


local void SendToArena(Player *p, const char *aname, int spawnx, int spawny)
{
	if (p->type == T_CONT)
		complete_go(p, aname, p->p_ship, p->xres, p->yres, p->flags.want_all_lvz, p->pkt.acceptaudio, spawnx, spawny);
}

local void PLeaving(Player *p, byte *pkt, int len)
{
#ifndef CFG_RELAX_LENGTH_CHECKS
	if (len != 1)
	{
		lm->LogP(L_MALICIOUS, "arenaman", p, "bad arena leaving packet len=%d", len);
		return;
	}
#endif

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
	FOR_EACH_ARENA(a)
		if (a->status == ARENA_RUNNING || a->status == ARENA_CLOSING)
		{
			Link *link;
			FOR_EACH_PLAYER(p)
				/* if any player is currently using this arena, it can't
				 * be reaped. also, if anyone is entering a running
				 * arena, don't reap that. */
				if (p->arena == a ||
				    (p->newarena == a && a->status == ARENA_RUNNING))
					goto skip;
				/* we do allow reaping of a closing arena that someone
				 * wants to re-enter, but we first make sure that it
				 * will reappear. */
				else if (p->newarena == a)
				{
					adata *ad = P_ARENA_DATA(a, adkey);
					ad->resurrect = TRUE;
				}

			lm->Log(L_DRIVEL, "<arenaman> {%s} arena being %s", a->name,
					a->status == ARENA_RUNNING ? "destroyed" : "recycled");

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

	RDLOCK();
	arena = do_find_arena(name, ARENA_RUNNING, ARENA_RUNNING);
	UNLOCK();

	if (arena && (totalp || playingp))
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
	Arena *a;
	void *data;
	Link *link, *last = NULL;
	struct block *b, *nb;
	int current = 0;

	/* round up to next multiple of word size */
	bytes = (bytes+(sizeof(int)-1)) & (~(sizeof(int)-1));

	WRLOCK();

	/* first try before between two blocks (or at the beginning) */
	for (link = LLGetHead(&blocks); link; link = link->next)
	{
		b = link->data;
		if ((size_t)(b->start - current) >= bytes)
			goto found;
		else
			current = b->start + b->len;
		last = link;
	}

	/* if we couldn't get in between two blocks, try at the end */
	if ((size_t)(perarenaspace - current) >= bytes)
		goto found;

	UNLOCK();
	return -1;

found:
	nb = amalloc(sizeof(*nb));
	nb->start = current;
	nb->len = bytes;
	/* if last == NULL, this will put it in front of the list */
	LLInsertAfter(&blocks, last, nb);

	/* clear all newly allocated space */
	FOR_EACH_ARENA_P(a, data, current)
		memset(data, 0, bytes);

	UNLOCK();

	return current;
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


local void GetPopulationSummary(int *totalp, int *playingp)
{
	/* arena lock held (shared) here */
	Link *link;
	Arena *a;
	Player *p;
	int total = 0, playing = 0;

	FOR_EACH_ARENA(a)
		a->playing = a->total = 0;

	pd->Lock();
	FOR_EACH_PLAYER(p)
		if (p->status == S_PLAYING &&
		    p->type != T_FAKE &&
		    p->arena)
		{
			total++;
			p->arena->total++;
			if (p->p_ship != SHIP_SPEC)
			{
				playing++;
				p->arena->playing++;
			}
		}
	pd->Unlock();

	if (totalp) *totalp = total;
	if (playingp) *playingp = playing;
}



local Iarenaman myint =
{
	INTERFACE_HEAD_INIT(I_ARENAMAN, "arenaman")
	SendArenaResponse, LeaveArena,
	RecycleArena,
	SendToArena, FindArena,
	GetPopulationSummary,
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

		LLInit(&aman->arenalist);

		pthread_rwlock_init(&arenalock, NULL);

		LLInit(&blocks);
		perarenaspace = cfg->GetInt(GLOBAL, "General", "PerArenaBytes", 10000);

		adkey = AllocateArenaData(sizeof(adata));
		if (adkey == -1) return MM_FAIL;

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
		ml->SetTimer(ReapArenas, 170, 170, NULL, NULL);

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

		FreeArenaData(adkey);

		pthread_rwlock_destroy(&arenalock);
		LLEnum(&aman->arenalist, afree);
		LLEmpty(&aman->arenalist);
		return MM_OK;
	}
	return MM_FAIL;
}


