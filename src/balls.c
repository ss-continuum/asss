
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "asss.h"

/* extra includes */
#include "packets/balls.h"


/* defines */
#define LOCK_STATUS(arena) \
	pthread_mutex_lock(ballmtx + arena)
#define UNLOCK_STATUS(arena) \
	pthread_mutex_unlock(ballmtx + arena)


/* internal structs */
struct MyBallData
{
	int sendtime, lastsent;
	/* these are in centiseconds. the timer event runs with a resolution
	 * of 100 centiseconds, though, so that's the best resolution you're
	 * going to get. */
	int spawnx, spawny, spawnr;
	int goaldelay;
	/* this is the delay between a goal and the ball respawning. */
};

/* prototypes */
local void SpawnBall(int arena, int bid);
local void AABall(int arena, int action);
local void PABall(int pid, int action, int arena);
local void ShipChange(int, int, int);
local void FreqChange(int, int);
local void BallKill(int, int, int, int, int);

/* timers */
local int BasicBallTimer(void *);

/* packet funcs */
local void PPickupBall(int, byte *, int);
local void PFireBall(int, byte *, int);
local void PGoal(int, byte *, int);

/* interface funcs */
local void SetBallCount(int arena, int ballcount);
local void PlaceBall(int arena, int bid, struct BallData *newpos);
local void EndGame(int arena);
local void LockBallStatus(int arena);
local void UnlockBallStatus(int arena);


/* local data */
local Imodman *mm;
local Inet *net;
local Iconfig *cfg;
local Ilogman *logm;
local Iplayerdata *pd;
local Iarenaman *aman;
local Imainloop *ml;
local Imapdata *mapdata;

/* the big balldata array */
local struct ArenaBallData balldata[MAXARENA];
local struct MyBallData pballdata[MAXARENA];
local pthread_mutex_t ballmtx[MAXARENA];

local Iballs _myint =
{
	INTERFACE_HEAD_INIT(I_BALLS, "ball-core")
	SetBallCount, PlaceBall, EndGame,
	LockBallStatus, UnlockBallStatus, balldata
};



EXPORT int MM_balls(int action, Imodman *_mm, int arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;
		net = mm->GetInterface(I_NET, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		logm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);

		mm->RegCallback(CB_ARENAACTION, AABall, ALLARENAS);
		mm->RegCallback(CB_PLAYERACTION, PABall, ALLARENAS);
		mm->RegCallback(CB_SHIPCHANGE, ShipChange, ALLARENAS);
		mm->RegCallback(CB_FREQCHANGE, FreqChange, ALLARENAS);
		mm->RegCallback(CB_KILL, BallKill, ALLARENAS);

		net->AddPacket(C2S_PICKUPBALL, PPickupBall);
		net->AddPacket(C2S_SHOOTBALL, PFireBall);
		net->AddPacket(C2S_GOAL, PGoal);

		{ /* init data */
			int i;
			for (i = 0; i < MAXARENA; i++)
				balldata[i].ballcount = 0;
		}

		{ /* init mutexes */
			int i;
			pthread_mutexattr_t attr;

			pthread_mutexattr_init(&attr);
			pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
			for (i = 0; i < MAXARENA; i++)
				pthread_mutex_init(ballmtx + i, &attr);
			pthread_mutexattr_destroy(&attr);
		}

		/* timers */
		ml->SetTimer(BasicBallTimer, 300, 100, NULL);

		mm->RegInterface(&_myint, ALLARENAS);

		/* seed random number generator */
		srand(GTC());
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_myint, ALLARENAS))
			return MM_FAIL;
		ml->ClearTimer(BasicBallTimer);
		net->RemovePacket(C2S_GOAL, PGoal);
		net->RemovePacket(C2S_SHOOTBALL, PFireBall);
		net->RemovePacket(C2S_PICKUPBALL, PPickupBall);
		mm->UnregCallback(CB_KILL, BallKill, ALLARENAS);
		mm->UnregCallback(CB_FREQCHANGE, FreqChange, ALLARENAS);
		mm->UnregCallback(CB_SHIPCHANGE, ShipChange, ALLARENAS);
		mm->UnregCallback(CB_PLAYERACTION, PABall, ALLARENAS);
		mm->UnregCallback(CB_ARENAACTION, AABall, ALLARENAS);
		mm->ReleaseInterface(mapdata);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(logm);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(net);
		return MM_OK;
	}
	return MM_FAIL;
}



local void SendBallPacket(int arena, int bid, int rel)
{
	struct BallPacket bp;
	struct BallData *bd = balldata[arena].balls + bid;

	LOCK_STATUS(arena);
	bp.type = S2C_BALL;
	bp.ballid = bid;
	bp.x = bd->x;
	bp.y = bd->y;
	bp.xspeed = bd->xspeed;
	bp.yspeed = bd->yspeed;
	bp.player = bd->carrier;
	if (bd->state == BALL_CARRIED)
		bp.time = 0;
	else if (bd->state == BALL_ONMAP)
		bp.time = bd->time;
	else
	{
		UNLOCK_STATUS(arena);
		return;
	}
	UNLOCK_STATUS(arena);

	net->SendToArena(arena, -1, (byte*)&bp, sizeof(bp), rel);
}


local void PhaseBall(int arena, int bid, int relflags)
{
	struct BallData *bd = balldata[arena].balls + bid;

	LOCK_STATUS(arena);
	bd->state = BALL_ONMAP;
	bd->x = bd->y = 30000;
	bd->xspeed = bd->yspeed = 0;
	bd->time = 0xFFFFFFFF; /* this is the key for making it phased */
	bd->carrier = -1;
	SendBallPacket(arena, bid, relflags);
	UNLOCK_STATUS(arena);
}


void SpawnBall(int arena, int bid)
{
	int cx, cy, rad, x, y;
	struct BallData d;

	d.state = BALL_ONMAP;
	d.xspeed = d.yspeed = 0;
	d.carrier = -1;
	d.time = GTC();

	cx = pballdata[arena].spawnx;
	cy = pballdata[arena].spawny;
	rad = pballdata[arena].spawnr;
	{
		float rndrad, rndang;
		/* pick random point */
		rndrad = (float)rand()/(RAND_MAX+1.0) * (float)rad;
		rndang = (float)rand()/(RAND_MAX+1.0) * M_PI * 2.0;
		x = cx + (rndrad * cos(rndang));
		y = cy + (rndrad * sin(rndang));
		/* wrap around, don't clip, so radii of 2048 from a corner
		 * work properly. */
		while (x < 0) x += 1024;
		while (x > 1023) x -= 1024;
		while (y < 0) y += 1024;
		while (y > 1023) y -= 1024;

		/* ask mapdata to move it to nearest empty tile */
		mapdata->FindFlagTile(arena, &x, &y);
	}

	/* whew, finally place the thing */
	d.x = x<<4; d.y = y<<4;
	PlaceBall(arena, bid, &d);
}


void SetBallCount(int arena, int ballcount)
{
	struct BallData *newbd;
	int oldc, i;

	if (ballcount < 0 || ballcount > 255)
		return;

	LOCK_STATUS(arena);

	oldc = balldata[arena].ballcount;

	if (ballcount < oldc)
	{
		/* we have to remove some balls. there is no clean way to do
		 * this (as of now). what we do is "phase" the ball so it can't
		 * be picked up by clients by setting it's last-updated time to
		 * be the highest possible time. then we send it's position to
		 * currently-connected clients. then we forget about the ball
		 * entirely so that updates are never sent again. to new
		 * players, it will look like the ball doesn't exist. */
		/* send it reliably, because clients are never going to see this
		 * ball ever again. */
		for (i = ballcount; i < oldc; i++)
			PhaseBall(arena, i, NET_RELIABLE);
	}

	/* do the realloc here so that if we have to phase, we do it before
	 * cutting down the memory, and if we have to grow, we spawn the
	 * balls into new memory. */
	newbd = realloc(balldata[arena].balls, ballcount * sizeof(struct BallData));
	if (!newbd && ballcount > 0)
		Error(ERROR_MEMORY, "realloc failed!");
	balldata[arena].ballcount = ballcount;
	balldata[arena].balls = newbd;

	if (ballcount > oldc)
	{
		for (i = oldc; i < ballcount; i++)
			SpawnBall(arena, i);
	}

	UNLOCK_STATUS(arena);
}


void PlaceBall(int arena, int bid, struct BallData *newpos)
{
	LOCK_STATUS(arena);
	if (bid >= 0 && bid < balldata[arena].ballcount)
	{
		balldata[arena].balls[bid] = *newpos;
		SendBallPacket(arena, bid, NET_UNRELIABLE);
	}
	UNLOCK_STATUS(arena);

	logm->Log(L_DRIVEL, "<balls> {%s} Ball %d is at (%d, %d)",
			aman->arenas[arena].name, bid, newpos->x, newpos->y);
}


void EndGame(int arena)
{
	int i, gtc = GTC(), newgame;
	ConfigHandle c = aman->arenas[arena].cfg;

	LOCK_STATUS(arena);

	for (i = 0; i < balldata[arena].ballcount; i++)
	{
		PhaseBall(arena, i, NET_RELIABLE);
		balldata[arena].balls[i].state = BALL_WAITING;
		balldata[arena].balls[i].carrier = -1;
	}

	newgame = cfg->GetInt(c, "Soccer", "NewGameDelay", -3000);
	if (newgame < 0)
		newgame = rand()%(newgame*-1);

	for (i = 0; i < balldata[arena].ballcount; i++)
		balldata[arena].balls[i].time = gtc + newgame;

	UNLOCK_STATUS(arena);
}


void LockBallStatus(int arena)
{
	LOCK_STATUS(arena);
}

void UnlockBallStatus(int arena)
{
	UNLOCK_STATUS(arena);
}


local void LoadBallSettings(int arena, int spawnballs)
{
	struct MyBallData *d = pballdata + arena;
	ConfigHandle c = aman->arenas[arena].cfg;
	int bc, i;

	/* get ball game type */
	bc = cfg->GetInt(c, "Soccer", "BallCount", 0);

	/* and initialize settings for that type */
	if (bc)
	{
		LOCK_STATUS(arena);
		d->spawnx = cfg->GetInt(c, "Soccer", "SpawnX", 512);
		d->spawny = cfg->GetInt(c, "Soccer", "SpawnY", 512);
		d->spawnr = cfg->GetInt(c, "Soccer", "SpawnRadius", 20);
		d->sendtime = cfg->GetInt(c, "Soccer", "SendTime", 1000);
		d->goaldelay = cfg->GetInt(c, "Soccer", "GoalDelay", 0);

		if (spawnballs)
		{
			d->lastsent = GTC();
			balldata[arena].ballcount = bc;

			/* allocate array for public ball data */
			balldata[arena].balls = amalloc(bc * sizeof(struct BallData));

			for (i = 0; i < bc; i++)
				SpawnBall(arena, i);

			logm->Log(L_INFO, "<balls> {%s} Arena has %d balls",
					aman->arenas[arena].name,
					bc);
		}

		UNLOCK_STATUS(arena);
	}
}


void AABall(int arena, int action)
{
	LOCK_STATUS(arena);
	if (action == AA_CREATE || action == AA_DESTROY)
	{
		/* clean up old ball data */
		if (balldata[arena].balls)
		{
			afree(balldata[arena].balls);
			balldata[arena].balls = NULL;
		}
		balldata[arena].ballcount = 0;
	}
	if (action == AA_CREATE)
	{
		/* only if we're creating, load the data */
		LoadBallSettings(arena, 1);
	}
	else if (action == AA_CONFCHANGED)
	{
		/* reload only settings, don't reset balls */
		LoadBallSettings(arena, 0);
	}
	UNLOCK_STATUS(arena);
}


local void CleanupAfter(int arena, int pid)
{
	/* make sure that if someone leaves, his ball drops */
	int i;
	struct BallData *f = balldata[arena].balls;

	LOCK_STATUS(arena);
	for (i = 0; i < balldata[arena].ballcount; i++, f++)
		if (f->state == BALL_CARRIED &&
			f->carrier == pid)
		{
			f->state = BALL_ONMAP;
			f->x = pd->players[pid].position.x;
			f->y = pd->players[pid].position.y;
			f->xspeed = f->yspeed = 0;
			f->time = GTC();
			SendBallPacket(arena, i, NET_UNRELIABLE);
		}
	UNLOCK_STATUS(arena);
}

void PABall(int pid, int action, int arena)
{
	/* if he's entering arena, the timer event will send him the ball
	 * info. */
	if (action == PA_LEAVEARENA)
		CleanupAfter(arena, pid);
}

void ShipChange(int pid, int ship, int newfreq)
{
	CleanupAfter(pd->players[pid].arena, pid);
}

void FreqChange(int pid, int newfreq)
{
	CleanupAfter(pd->players[pid].arena, pid);
}

void BallKill(int arena, int killer, int killed, int bounty, int flags)
{
	CleanupAfter(arena, killed);
}


void PPickupBall(int pid, byte *p, int len)
{
	int arena, i;
	struct BallData *bd;
	struct C2SPickupBall *bp = (struct C2SPickupBall*)p;

	arena = pd->players[pid].arena;

	if (len != sizeof(struct C2SPickupBall))
	{
		logm->Log(L_MALICIOUS, "<balls> [%s] Bad size for ball pickup packet", pd->players[pid].name);
		return;
	}

	if (ARENA_BAD(arena) || pd->players[pid].status != S_PLAYING)
	{
		logm->Log(L_WARN, "<balls> [%s] Ball pickup packet from bad arena or status", pd->players[pid].name);
		return;
	}

	LOCK_STATUS(arena);

	if (bp->ballid >= balldata[arena].ballcount)
	{
		logm->Log(L_MALICIOUS, "<balls> [%s] Tried to pick up a nonexistent ball", pd->players[pid].name);
		UNLOCK_STATUS(arena);
		return;
	}

	bd = balldata[arena].balls + bp->ballid;

	/* make sure someone else didn't get it first */
	if (bd->state != BALL_ONMAP)
	{
		logm->Log(L_MALICIOUS, "<balls> {%s} [%s] Tried to pick up a carried ball",
				aman->arenas[arena].name,
				pd->players[pid].name);
		UNLOCK_STATUS(arena);
		return;
	}

	/* make sure player doesnt carry more than one ball */
	for (i=0; i < balldata[arena].ballcount; i++)
		if (balldata[arena].balls[i].carrier == pid && balldata[arena].balls[i].state == BALL_CARRIED)
		{
			UNLOCK_STATUS(arena);
			return;
		}

	bd->state = BALL_CARRIED;
	bd->x = pd->players[pid].position.x;
	bd->y = pd->players[pid].position.y;
	bd->xspeed = 0;
	bd->yspeed = 0;
	bd->carrier = pid;
	bd->freq = pd->players[pid].freq;
	bd->time = 0;
	SendBallPacket(arena, bp->ballid, NET_UNRELIABLE | NET_PRI_P3);

	UNLOCK_STATUS(arena);

	/* now call callbacks */
	DO_CBS(CB_BALLPICKUP, arena, BallPickupFunc,
			(arena, pid, bp->ballid));

	logm->Log(L_DRIVEL, "<balls> {%s} [%s] Player picked up ball %d",
			aman->arenas[arena].name,
			pd->players[pid].name,
			bp->ballid);
}


void PFireBall(int pid, byte *p, int len)
{
	int arena, bid;
	struct BallData *bd;
	struct BallPacket *fb = (struct BallPacket *)p;

	arena = pd->players[pid].arena;
	bid = fb->ballid;

	if (len != sizeof(struct BallPacket))
	{
		logm->Log(L_MALICIOUS, "<balls> [%s] Bad size for ball fire packet", pd->players[pid].name);
		return;
	}

	if (ARENA_BAD(arena) || pd->players[pid].status != S_PLAYING)
	{
		logm->Log(L_WARN, "<balls> [%s] Ball fire packet from bad arena or status", pd->players[pid].name);
		return;
	}

	if (pd->players[pid].shiptype >= SPEC)
	{
		logm->Log(L_MALICIOUS, "<balls> [%s] Ball fire packet from spec", pd->players[pid].name);
		return;
	}

	LOCK_STATUS(arena);

	if (bid < 0 || bid >= balldata[arena].ballcount)
	{
		logm->Log(L_MALICIOUS, "<balls> [%s] Tried to fire up a nonexistent ball", pd->players[pid].name);
		UNLOCK_STATUS(arena);
		return;
	}

	bd = balldata[arena].balls + bid;

	if (bd->state != BALL_CARRIED || bd->carrier != pid)
	{
		logm->Log(L_MALICIOUS, "<balls> [%s] Player tried to fire ball he wasn't carrying", pd->players[pid].name);
		UNLOCK_STATUS(arena);
		return;
	}

	bd->state = BALL_ONMAP;
	bd->x = fb->x;
	bd->y = fb->y;
	bd->xspeed = fb->xspeed;
	bd->yspeed = fb->yspeed;
	bd->freq = pd->players[pid].freq;
	bd->time = fb->time;
	SendBallPacket(arena, bid, NET_UNRELIABLE | NET_PRI_P3);

	UNLOCK_STATUS(arena);

	/* finally call callbacks */
	DO_CBS(CB_BALLFIRE, arena, BallFireFunc, (arena, pid, bid));

	logm->Log(L_DRIVEL, "<balls> {%s} [%s] Player fired ball %d",
			aman->arenas[arena].name,
			pd->players[pid].name,
			bid);
}


void PGoal(int pid, byte *p, int len)
{
	struct C2SGoal *g = (struct C2SGoal*)p;
	int arena = pd->players[pid].arena, bid;
	struct BallData *bd;

	if (len != sizeof(struct C2SGoal))
	{
		logm->Log(L_MALICIOUS, "<balls> [%s] Bad size for goal packet", pd->players[pid].name);
		return;
	}

	if (ARENA_BAD(arena) || pd->players[pid].status != S_PLAYING)
	{
		logm->Log(L_WARN, "<balls> [%s] Goal packet from bad arena or status", pd->players[pid].name);
		return;
	}

	if (pd->players[pid].shiptype >= SPEC)
	{
		logm->Log(L_MALICIOUS, "<balls> [%s] Goal packet from spec", pd->players[pid].name);
		return;
	}

	LOCK_STATUS(arena);

	bid = g->ballid;

	if (bid < 0 || bid >= balldata[arena].ballcount)
	{
		logm->Log(L_MALICIOUS, "<balls> [%s] Sent a goal for a nonexistent ball", pd->players[pid].name);
		UNLOCK_STATUS(arena);
		return;
	}

	bd = balldata[arena].balls + bid;

	/* we use this as a flag to check for dupilicated goals */
	if (bd->carrier == -1)
	{
		UNLOCK_STATUS(arena);
		return;
	}

	if (bd->state != BALL_ONMAP)
	{
		logm->Log(L_MALICIOUS, "<balls> [%s] Player sent goal for ball he didn't fire or that's being carried", pd->players[pid].name);
		UNLOCK_STATUS(arena);
		return;
	}

	/* do callbacks before spawning */
	DO_CBS(CB_GOAL, arena, GoalFunc, (arena, bd->carrier, g->ballid, g->x, g->y));

	/* send ball update */
	if (bd->state != BALL_ONMAP)
	{
		/* don't respawn ball */
	}
	else if (pballdata[arena].goaldelay == 0)
	{
		/* we don't want a delay */
		SpawnBall(arena, bid);
	}
	else
	{
		/* phase it, then set it to waiting */
		PhaseBall(arena, bid, NET_UNRELIABLE);
		bd->state = BALL_WAITING;
		bd->carrier = -1;
		bd->time = GTC() + pballdata[arena].goaldelay;
	}

	UNLOCK_STATUS(arena);

	logm->Log(L_DRIVEL, "<balls> {%s} [%s] Goal with ball %d",
			aman->arenas[arena].name,
			pd->players[pid].name,
			g->ballid);
}


int BasicBallTimer(void *dummy)
{
	int arena;

	for (arena = 0; arena < MAXARENA; arena++)
	{
		LOCK_STATUS(arena);
		if (balldata[arena].ballcount > 0)
		{
			/* see if we are ready to send packets */
			int gtc = GTC();

			if ( ((int)gtc - (int)pballdata[arena].lastsent) > pballdata[arena].sendtime)
			{
				int bc, bid;
				struct BallData *b;

				/* now check the balls up to bc */
				bc = balldata[arena].ballcount;
				b = balldata[arena].balls;
				for (bid = 0; bid < bc; bid++, b++)
					if (b->state == BALL_ONMAP)
					{
						/* it's on the map, just send the position
						 * update */
						SendBallPacket(arena, bid, NET_UNRELIABLE);
					}
					else if (b->state == BALL_CARRIED)
					{
						/* it's being carried, update it's x,y coords */
						struct PlayerPosition *pos =
							&pd->players[b->carrier].position;
						b->x = pos->x;
						b->y = pos->y;
						SendBallPacket(arena, bid, NET_UNRELIABLE);
					}
					else if (b->state == BALL_WAITING)
					{
						if (gtc >= b->time)
							SpawnBall(arena, bid);
					}
				pballdata[arena].lastsent = gtc;
			}
		}
		UNLOCK_STATUS(arena);
	}

	return 1;
}


