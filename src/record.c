
/* dist: public */

#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#include <unistd.h>

#include <zlib.h>

#include "asss.h"
#include "fake.h"


/* recorded game file format */
enum
{
	EV_NULL,
	EV_ENTER,
	EV_LEAVE,
	EV_SHIPCHANGE,
	EV_FREQCHANGE,
	EV_KILL,
	EV_CHAT,
	EV_POS,
	/* TODO: not implemented yet: */
	EV_BRICK,
	EV_FLAGPICKUP,
	EV_FLAGDROP,
	EV_BALLPICKUP,
	EV_BALLFIRE,
	/* TODO: koth?, scores? */
};

struct event_header
{
	u32 tm;
	i16 type;
};

struct event_enter
{
	struct event_header head;
	i16 pid;
	char name[24], squad[24];
	u16 ship, freq;
};

struct event_leave
{
	struct event_header head;
	i16 pid;
};

struct event_sc
{
	struct event_header head;
	i16 pid;
	i16 newship, newfreq;
};

struct event_fc
{
	struct event_header head;
	i16 pid;
	i16 newfreq;
};

struct event_kill
{
	struct event_header head;
	i16 killer, killed, bty, flags;
};

struct event_chat
{
	struct event_header head;
	i16 pid;
	u8 type, sound;
	u16 len;
	char msg[1];
};

struct event_pos
{
	struct event_header head;
	/* special: the type byte holds the length of the rest of the
	 * packet, since the last two fields are optional. the time field
	 * holds the pid. */
	struct C2SPosition pos;
};

#define FILE_VERSION 1

struct file_header
{
	char header[8];        /* always "asssgame" */
	u32 version;           /* just one for now */
	u32 offset;            /* offset of start of events from beginning of the file */
	u32 events;            /* number of events in the file */
	u32 endtime;           /* ending time of recorded game */
	u32 maxpid;            /* the highest numbered pid in the file */
	u32 specfreq;          /* the spec freq at the time the game was recorded */
	time_t recorded;       /* the date and time this game was recorded */
	char recorder[24];     /* the name of the player who recorded it */
	char arenaname[24];    /* the name of the arena that was recorded */
	char comments[256];    /* misc. comments */
};


/* start of module code */

typedef struct rec_adata
{
	enum { s_none, s_recording, s_playing } state;
	gzFile gzf; /* reading */
	FILE *f; /* writing */
	const char *fname;
	u32 events, maxpid;
	ticks_t started, total;
	int specfreq;
	int ispaused;
	double curpos;
	MPQueue mpq;
	pthread_t thd;
} rec_adata;


local Imodman *mm;
local Iarenaman *aman;
local Iplayerdata *pd;
local Icmdman *cmd;
local Igame *game;
local Ifake *fake;
local Ilogman *lm;
local Ichat *chat;
local Inet *net;
local Iconfig *cfg;

local int adkey;
local pthread_mutex_t recmtx = PTHREAD_MUTEX_INITIALIZER;
#define LOCK(a) pthread_mutex_lock(&recmtx)
#define UNLOCK(a) pthread_mutex_unlock(&recmtx)


/********** game recording **********/

local inline int get_size(struct event_header *ev)
{
	switch (ev->type)
	{
		case EV_ENTER:          return sizeof(struct event_enter);
		case EV_LEAVE:          return sizeof(struct event_leave);
		case EV_SHIPCHANGE:     return sizeof(struct event_sc);
		case EV_FREQCHANGE:     return sizeof(struct event_fc);
		case EV_KILL:           return sizeof(struct event_kill);
		case EV_CHAT:           return ((struct event_chat *)ev)->len + offsetof(struct event_chat, msg);
		case EV_POS:            return ((struct event_pos *)ev)->pos.type + offsetof(struct event_pos, pos);
#if 0
		case EV_BRICK:          return sizeof(struct event_);
		case EV_FLAGPICKUP:     return sizeof(struct event_);
		case EV_FLAGDROP:       return sizeof(struct event_);
		case EV_BALLPICKUP:     return sizeof(struct event_);
		case EV_BALLFIRE:       return sizeof(struct event_);
#endif
		default:                return 0;
	}
}

local inline int get_event_pid(struct event_header *ev)
{
	int pid1, pid2;
	switch (ev->type)
	{
		case EV_ENTER:          return ((struct event_enter*)ev)->pid;
		case EV_LEAVE:          return ((struct event_leave*)ev)->pid;
		case EV_SHIPCHANGE:     return ((struct event_sc*)ev)->pid;
		case EV_FREQCHANGE:     return ((struct event_fc*)ev)->pid;
		case EV_CHAT:           return ((struct event_chat *)ev)->pid;
		case EV_POS:            return ((struct event_pos *)ev)->pos.time;
		case EV_KILL:
			pid1 = ((struct event_kill*)ev)->killer;
			pid2 = ((struct event_kill*)ev)->killed;
			return (pid1 > pid2) ? pid1 : pid2;
#if 0
		case EV_BRICK:          return ((struct event_)ev)->pid;
		case EV_FLAGPICKUP:     return ((struct event_)ev)->pid;
		case EV_FLAGDROP:       return ((struct event_)ev)->pid;
		case EV_BALLPICKUP:     return ((struct event_)ev)->pid;
		case EV_BALLFIRE:       return ((struct event_)ev)->pid;
#endif
		default:                return 0;
	}
}


/* callbacks that pass events to writing thread */

local void cb_paction(Player *p, int action, Arena *a)
{
	rec_adata *ra = P_ARENA_DATA(a, adkey);

	if (action == PA_ENTERARENA)
	{
		struct event_enter *ev = amalloc(sizeof(*ev));
		ev->head.tm = current_ticks();
		ev->head.type = EV_ENTER;
		ev->pid = p->pid;
		astrncpy(ev->name, p->name, sizeof(ev->name));
		astrncpy(ev->squad, p->squad, sizeof(ev->squad));
		ev->ship = p->p_ship;
		ev->freq = p->p_freq;
		MPAdd(&ra->mpq, ev);
	}
	else if (action == PA_LEAVEARENA)
	{
		struct event_leave *ev = amalloc(sizeof(*ev));
		ev->head.tm = current_ticks();
		ev->head.type = EV_LEAVE;
		ev->pid = p->pid;
		MPAdd(&ra->mpq, ev);
	}
}


local void cb_shipchange(Player *p, int ship, int freq)
{
	rec_adata *ra = P_ARENA_DATA(p->arena, adkey);
	struct event_sc *ev = amalloc(sizeof(*ev));

	ev->head.tm = current_ticks();
	ev->head.type = EV_SHIPCHANGE;
	ev->pid = p->pid;
	ev->newship = ship;
	ev->newfreq = freq;
	MPAdd(&ra->mpq, ev);
}


local void cb_freqchange(Player *p, int freq)
{
	rec_adata *ra = P_ARENA_DATA(p->arena, adkey);
	struct event_fc *ev = amalloc(sizeof(*ev));

	ev->head.tm = current_ticks();
	ev->head.type = EV_FREQCHANGE;
	ev->pid = p->pid;
	ev->newfreq = freq;
	MPAdd(&ra->mpq, ev);
}


local void cb_kill(Arena *a, Player *killer, Player *killed, int bty, int flags)
{
	rec_adata *ra = P_ARENA_DATA(a, adkey);
	struct event_kill *ev = amalloc(sizeof(*ev));

	ev->head.tm = current_ticks();
	ev->head.type = EV_KILL;
	ev->killer = killer->pid;
	ev->killed = killed->pid;
	ev->bty = bty;
	ev->flags = flags;
	MPAdd(&ra->mpq, ev);
}


local void cb_chat(Player *p, int type, int sound, Player *target, int freq, const char *txt)
{
	rec_adata *ra = P_ARENA_DATA(p->arena, adkey);

	if (type == MSG_ARENA || type == MSG_PUB || (type == MSG_FREQ && freq == ra->specfreq))
	{
		int len = strlen(txt) + 1;
		struct event_chat *ev = amalloc(sizeof(*ev) + len);

		ev->head.tm = current_ticks();
		ev->head.type = EV_CHAT;
		ev->pid = p->pid;
		ev->type = type;
		ev->sound = sound;
		ev->len = len;
		memcpy(ev->msg, txt, len);
		MPAdd(&ra->mpq, ev);
	}
}


local inline int check_arena(Arena *a)
{
	rec_adata *ra = P_ARENA_DATA(a, adkey);
	if (!a)
		return TRUE;
	LOCK(a);
	if (ra->state != s_recording)
	{
		UNLOCK(a);
		return TRUE;
	}
	UNLOCK(a);
	return FALSE;
}

local void ppk(Player *p, byte *pkt, int n)
{
	Arena *a = p->arena;
	rec_adata *ra = P_ARENA_DATA(a, adkey);
	struct event_pos *ev;

	if (check_arena(a)) return;

	ev = amalloc(n + offsetof(struct event_pos, pos));
	ev->head.tm = current_ticks();
	ev->head.type = EV_POS;
	memcpy(&ev->pos, pkt, n);
	ev->pos.type = n;
	ev->pos.time = p->pid;
	MPAdd(&ra->mpq, ev);
}


/* writer thread */

local void *recorder_thread(void *v)
{
	Arena *a = v;
	rec_adata *ra = P_ARENA_DATA(a, adkey);
	struct event_header *ev;
	int pid;

	assert(ra->f);

	while ((ev = MPRemove(&ra->mpq)))
	{
		int len = get_size(ev);
		/* normalize events to start from 0 */
		ev->tm = TICK_DIFF(ev->tm, ra->started);
		fwrite(ev, len, 1, ra->f);
		pid = get_event_pid(ev);
		if (pid > ra->maxpid)
			ra->maxpid = pid;
		afree(ev);
		ra->events++;
	}

	return NULL;
}


/* starting and stopping functions and helpers */

local void write_current_players(Arena *a)
{
	rec_adata *ra = P_ARENA_DATA(a, adkey);
	Link *link;
	Player *p;

	pd->Lock();
	FOR_EACH_PLAYER(p)
		if (p->arena == a && p->status == S_PLAYING)
		{
			struct event_enter ev;
			ev.head.tm = 0;
			ev.head.type = EV_ENTER;
			ev.pid = p->pid;
			if (p->pid > ra->maxpid)
				ra->maxpid = p->pid;
			astrncpy(ev.name, p->name, sizeof(ev.name));
			astrncpy(ev.squad, p->squad, sizeof(ev.squad));
			ev.ship = p->p_ship;
			ev.freq = p->p_freq;
			fwrite((byte*)&ev, sizeof(ev), 1, ra->f);
		}
	pd->Unlock();
}


local int start_recording(Arena *a, const char *file, const char *recorder, const char *comments)
{
	rec_adata *ra = P_ARENA_DATA(a, adkey);
	int ok = FALSE;

	LOCK(a);
	if (ra->state == s_none)
	{
		ra->f = fopen(file, "wb");
		if (ra->f)
		{
			/* leave the header wrong until we finish it properly in
			 * stop_recording */
			struct file_header header = { "ass$game" };

			/* fill in file header */
			header.version = 1;
			header.offset = sizeof(struct file_header);
			/* we don't know these next 3 yet */
			header.events = 0;
			header.endtime = 0;
			header.maxpid = 0;
			header.specfreq = cfg->GetInt(a->cfg, "Team", "SpectatorFrequency", 8025);
			time(&header.recorded);
			astrncpy(header.recorder, recorder, sizeof(header.recorder));
			astrncpy(header.arenaname, a->name, sizeof(header.arenaname));
			astrncpy(header.comments, comments, sizeof(header.comments));
			fwrite(&header, sizeof(header), 1, ra->f);

			/* generate fake enter events for the current players in
			 * this arena */
			ra->maxpid = 0;
			write_current_players(a);

			ra->specfreq = header.specfreq;

			MPInit(&ra->mpq);

			mm->RegCallback(CB_PLAYERACTION, cb_paction, a);
			mm->RegCallback(CB_SHIPCHANGE, cb_shipchange, a);
			mm->RegCallback(CB_FREQCHANGE, cb_freqchange, a);
			mm->RegCallback(CB_KILL, cb_kill, a);
			mm->RegCallback(CB_CHATMSG, cb_chat, a);

			ra->started = current_ticks();
			ra->fname = astrdup(file);

			pthread_create(&ra->thd, NULL, recorder_thread, a);

			ra->state = s_recording;

			ok = TRUE;
		}
	}
	UNLOCK(a);
	return ok;
}


local int stop_recording(Arena *a)
{
	rec_adata *ra = P_ARENA_DATA(a, adkey);
	int ok = FALSE;

	LOCK(a);
	if (ra->state == s_recording)
	{
		u32 fields[3];

		ra->state = s_none;

		MPAdd(&ra->mpq, NULL);
		pthread_join(ra->thd, NULL);

		mm->UnregCallback(CB_PLAYERACTION, cb_paction, a);
		mm->UnregCallback(CB_SHIPCHANGE, cb_shipchange, a);
		mm->UnregCallback(CB_FREQCHANGE, cb_freqchange, a);
		mm->UnregCallback(CB_KILL, cb_kill, a);
		mm->UnregCallback(CB_CHATMSG, cb_chat, a);

		MPDestroy(&ra->mpq);

		/* fill in header fields we couldn't get before */
		fields[0] = ra->events;
		fields[1] = TICK_DIFF(current_ticks(), ra->started);
		fields[2] = ra->maxpid;
		fseek(ra->f, offsetof(struct file_header, events), SEEK_SET);
		fwrite(fields, sizeof(fields), 1, ra->f);
		fseek(ra->f, 0, SEEK_SET);
		fwrite("asssgame", 8, 1, ra->f);
		fclose(ra->f);
		ra->f = NULL;

#ifndef WIN32
		/* spawn a gzip to zip it up */
		if (fork() == 0)
		{
			/* in child */
			close(0); close(1); close(2);
			execlp("gzip", "gzip", "-f", "-n", "-q", "-9", ra->fname, NULL);
		}
#endif
		afree(ra->fname);
		ra->fname = NULL;

		ok = TRUE;
	}
	UNLOCK(a);
	return ok;
}


/********** game playback functions **********/

local void get_watching_set(LinkedList *set, Arena *arena)
{
	Link *link;
	Player *p;
	pd->Lock();
	FOR_EACH_PLAYER(p)
		if (p->status == S_PLAYING &&
		    p->arena == arena &&
		    p->type != T_FAKE)
			LLAdd(set, p);
	pd->Unlock();
}

/* locking humans to spec */

local void freqman(Player *p, int *ship, int *freq)
{
	rec_adata *ra = P_ARENA_DATA(p->arena, adkey);
	*ship = SPEC;
	*freq = ra->specfreq;
}

local struct Ifreqman lockspecfm =
{
	INTERFACE_HEAD_INIT(I_FREQMAN, "fm-lock-spec")
	freqman, freqman, freqman
};


local void lock_all_spec(Arena *a)
{
	rec_adata *ra = P_ARENA_DATA(a, adkey);
	LinkedList set = LL_INITIALIZER;
	Link *l;

	mm->RegInterface(&lockspecfm, a);

	get_watching_set(&set, a);
	for (l = LLGetHead(&set); l; l = l->next)
		game->SetFreqAndShip(l->data, SPEC, ra->specfreq);
	LLEmpty(&set);
}

local void unlock_all_spec(Arena *a)
{
	mm->UnregInterface(&lockspecfm, a);
}


/* helpers for playback thread */

local inline int check_chat_len(Arena *a, int len)
{
	if (len >= 1 && len < 512)
		return TRUE;
	else
	{
		lm->LogA(L_WARN, "record", a, "bad chat msg length: %d", len);
		return FALSE;
	}
}

local inline int check_pos_len(Arena *a, int len)
{
	if (len == 22 || len == 24 || len == 32)
		return TRUE;
	else
	{
		lm->LogA(L_WARN, "record", a, "bad position length: %d", len);
		return FALSE;
	}
}

enum
{
	PC_NULL = 0,
	PC_STOP,
	PC_PAUSE,
	PC_RESUME,
	PC_RESTART,
};

/* playback thread */

local void *playback_thread(void *v)
{
	union
	{
		char buf[4096]; /* no event can be larger than this */
		struct event_header head;
		struct event_enter enter;
		struct event_enter leave;
		struct event_sc sc;
		struct event_fc fc;
		struct event_kill kill;
		struct event_chat chat;
		struct event_pos pos;
	} ev;

	Arena *a = v;
	rec_adata *ra = P_ARENA_DATA(a, adkey);
	int cmd, r;
	long startingoffset;
	ticks_t started, now, paused;

	Player **pidmap;
	int pidmaplen;

	startingoffset = gztell(ra->gzf);
	memset(ev.buf, 0, sizeof(ev));

	pidmaplen = ra->maxpid + 1;
	pidmap = amalloc(pidmaplen * sizeof(Player*));
	memset(pidmap, 0, pidmaplen * sizeof(Player*));

	started = current_ticks();
	ra->ispaused = 0;

	for (;;)
	{
		/* try reading a control command */
		cmd = (int)MPTryRemove(&ra->mpq);
		switch (cmd)
		{
			case PC_NULL:
				break;

			case PC_STOP:
				goto out;

			case PC_PAUSE:
				paused = current_ticks();
				LOCK(a);
				ra->ispaused = TRUE;
				UNLOCK(a);
				break;

			case PC_RESUME:
				LOCK(a);
				ra->ispaused = FALSE;
				UNLOCK(a);
				started = TICK_MAKE(started + TICK_DIFF(current_ticks(), paused));
				break;

			case PC_RESTART:
				/* this is a mess of stuff that needs to be done */
				/* clear out existing players */
				for (r = 0; r < pidmaplen; r++)
					if (pidmap[r])
						fake->EndFaked(pidmap[r]);
				memset(pidmap, 0, pidmaplen * sizeof(Player*));
				/* reset the event so we read a new one */
				memset(ev.buf, 0, sizeof(ev));
				/* status stuff */
				LOCK(a);
				ra->curpos = 0.0;
				ra->ispaused = FALSE;
				UNLOCK(a);
				/* reset file position */
				gzseek(ra->gzf, startingoffset, SEEK_SET);
				/* and mark where we're now starting  */
				started = current_ticks();
				break;
		}

		/* get an event if we don't have one */
		if (ev.head.type == 0)
		{
			/* read header */
			r = gzread(ra->gzf, &ev, sizeof(struct event_header));
			/* read rest */
			switch (ev.head.type)
			{
#define REST(type) (sizeof(struct type) - sizeof(struct event_header))
				case EV_NULL:
					break;
				case EV_ENTER:
					r = gzread(ra->gzf, &ev.enter.pid, REST(event_enter));
					break;
				case EV_LEAVE:
					r = gzread(ra->gzf, &ev.leave.pid, REST(event_leave));
					break;
				case EV_SHIPCHANGE:
					r = gzread(ra->gzf, &ev.sc.pid, REST(event_sc));
					break;
				case EV_FREQCHANGE:
					r = gzread(ra->gzf, &ev.fc.pid, REST(event_fc));
					break;
				case EV_KILL:
					r = gzread(ra->gzf, &ev.kill.killer, REST(event_kill));
					break;
				case EV_CHAT:
					/* read enough bytes to get len field */
					r = gzread(ra->gzf, &ev.chat.pid, REST(event_chat) - 1);
					if (!check_chat_len(a, ev.chat.len))
						goto out;
					/* now read more for len field */
					r = gzread(ra->gzf, ev.chat.msg, ev.chat.len);
					break;
				case EV_POS:
					r = gzread(ra->gzf, &ev.pos.pos, 1);
					if (!check_pos_len(a, ev.pos.pos.type))
						goto out;
					r = gzread(ra->gzf, ((char*)&ev.pos.pos) + 1, ev.pos.pos.type - 1);
					break;
				/* not impl */
				case EV_BRICK:
				case EV_FLAGPICKUP:
				case EV_FLAGDROP:
				case EV_BALLPICKUP:
				case EV_BALLFIRE:
					break;
				default:
					lm->LogA(L_WARN, "record", a, "bad event type in game file: %d",
							ev.head.type);
					goto out;
#undef REST
			}
			if (r == 0)
				goto out;
		}

		/* do stuff with current time */
		now = current_ticks();

		if (!ra->ispaused)
			ra->curpos = 100.0*(double)TICK_DIFF(now, started)/(double)ra->total;

		/* only process it if its time has come aready. if not, sleep
		 * for a bit and go for another iteration around the loop. */
		if (!ra->ispaused && TICK_DIFF(now, started) >= ev.head.tm)
		{
			Player *p1, *p2;

			switch (ev.head.type)
			{
#define CHECK(pid) \
	if ((pid) < 0 || (pid) >= pidmaplen) { \
		lm->LogA(L_WARN, "record", a, "bad pid in game file: %d", (pid)); \
		goto out; }

				case EV_NULL:
					break;
				case EV_ENTER:
				{
					char newname[20];
					CHECK(ev.enter.pid)
					snprintf(newname, 20, "~%s", ev.enter.name);
					p1 = fake->CreateFakePlayer(newname, a, ev.enter.ship, ev.enter.freq);
					if (p1)
						pidmap[ev.enter.pid] = p1;
					else
						lm->LogA(L_WARN, "record", a, "can't create fake player for pid %d",
								ev.enter.pid);
				}
					break;
				case EV_LEAVE:
					CHECK(ev.leave.pid)
					p1 = pidmap[ev.leave.pid];
					if (p1)
						fake->EndFaked(p1);
					else
						lm->LogA(L_WARN, "record", a, "no mapping for pid %d",
								ev.leave.pid);
					pidmap[ev.leave.pid] = NULL;
					break;
				case EV_SHIPCHANGE:
					CHECK(ev.sc.pid)
					p1 = pidmap[ev.sc.pid];
					if (p1)
						game->SetFreqAndShip(p1, ev.sc.newship, ev.sc.newfreq);
					else
						lm->LogA(L_WARN, "record", a, "no mapping for pid %d",
								ev.sc.pid);
					break;
				case EV_FREQCHANGE:
					CHECK(ev.fc.pid)
					p1 = pidmap[ev.fc.pid];
					if (p1)
						game->SetFreq(p1, ev.fc.newfreq);
					else
						lm->LogA(L_WARN, "record", a, "no mapping for pid %d",
								ev.sc.pid);
					break;
				case EV_KILL:
					CHECK(ev.kill.killer)
					CHECK(ev.kill.killed)
					p1 = pidmap[ev.kill.killer];
					p2 = pidmap[ev.kill.killed];
					if (p1 && p2)
						game->FakeKill(p1, p2, ev.kill.bty, ev.kill.flags);
					else
						lm->LogA(L_WARN, "record", a, "no mapping for pid %d",
								ev.sc.pid);
					break;
				case EV_CHAT:
					if (ev.chat.type == MSG_ARENA)
					{
						chat->SendArenaSoundMessage(a, ev.chat.sound, "%s", ev.chat.msg);
					}
					else if (ev.chat.type == MSG_PUB || ev.chat.type == MSG_FREQ)
					{
						CHECK(ev.chat.pid)
						p1 = pidmap[ev.chat.pid];
						if (p1)
						{
							LinkedList set = LL_INITIALIZER;
							get_watching_set(&set, a);
							chat->SendAnyMessage(&set, ev.chat.type,
									ev.chat.sound, p1, "%s",
									ev.chat.msg);
							LLEmpty(&set);
						}
						else
							lm->LogA(L_WARN, "record", a, "no mapping for pid %d",
									ev.sc.pid);
					}
					break;
				case EV_POS:
					CHECK(ev.pos.pos.time)
					p1 = pidmap[ev.pos.pos.time];
					if (p1)
					{
						ev.pos.pos.time = now;
						game->FakePosition(p1, &ev.pos.pos);
					}
					else
						lm->LogA(L_WARN, "record", a, "no mapping for pid %d",
								ev.sc.pid);
					break;
				/* not impl */
				case EV_BRICK:
				case EV_FLAGPICKUP:
				case EV_FLAGDROP:
				case EV_BALLPICKUP:
				case EV_BALLFIRE:
					break;
#undef CHECK
			}
			ev.head.type = 0; /* signal to read another event */
		}
		else
			usleep(10000);
	}

out:
	/* make sure everyone leaves */
	for (r = 0; r < pidmaplen; r++)
		if (pidmap[r])
			fake->EndFaked(pidmap[r]);
	afree(pidmap);

	chat->SendArenaMessage(a, "Game playback stopped");

	unlock_all_spec(a);

	LOCK(a);

	/* nobody should be playing with this except us */
	assert(ra->state == s_playing);
	ra->state = s_none;

	gzclose(ra->gzf);
	ra->gzf = NULL;
	afree(ra->fname);
	ra->fname = NULL;

	UNLOCK(a);

	return NULL;
}


/* starting and stopping playback */

local int start_playback(Arena *a, const char *file)
{
	rec_adata *ra = P_ARENA_DATA(a, adkey);
	int ok = FALSE;

	LOCK(a);
	if (ra->state == s_none)
	{
		ra->gzf = gzopen(file, "rb");
		if (ra->gzf)
		{
			struct file_header header;
			gzread(ra->gzf, &header, sizeof(header));
			if (header.version == FILE_VERSION)
			{
				char date[32];

				ra->fname = astrdup(file);
				ra->maxpid = header.maxpid;
				ra->total = header.endtime;
				ra->events = header.events;
				ra->specfreq = header.specfreq;

				/* tell people about the game and lock them in spec */
				chat->SendArenaMessage(a, "Starting game playback: %s", file);
				ctime_r(&header.recorded, date);
				chat->SendArenaMessage(a, "Game recorded in arena %s by %s on %s",
						header.arenaname, header.recorder, date);

				lock_all_spec(a);

				/* move to where the data is */
				gzseek(ra->gzf, header.offset, SEEK_SET);

				MPInit(&ra->mpq);

				pthread_create(&ra->thd, NULL, playback_thread, a);
				pthread_detach(ra->thd);

				ra->state = s_playing;

				ok = TRUE;
			}
			else
			{
				gzclose(ra->gzf);
				ra->gzf = NULL;
			}
		}
	}
	UNLOCK(a);
	return ok;
}


local int stop_playback(Arena *a)
{
	rec_adata *ra = P_ARENA_DATA(a, adkey);
	int ok = FALSE;

	LOCK(a);
	if (ra->state == s_playing)
	{
		/* all we can do is tell it to stop */
		MPAdd(&ra->mpq, (void*)PC_STOP);

		ok = TRUE;
	}
	UNLOCK(a);
	return ok;
}


/* the main controlling command */

local helptext_t gamerecord_help =
"Module: record\n"
"Targets: none\n"
"Args: status | record <file> | play <file> | pause | restart | stop\n"
"TODO: write more here.\n";

local void Cgamerecord(const char *params, Player *p, const Target *target)
{
	Arena *a = p->arena;
	rec_adata *ra = P_ARENA_DATA(a, adkey);

	if (strncasecmp(params, "record", 6) == 0)
	{
		const char *fn = params + 6;
		while (*fn && isspace(*fn)) fn++;
		if (*fn)
		{
			if (start_recording(a, fn, p->name, ""))
				chat->SendMessage(p, "Recording started.");
			else
				chat->SendMessage(p, "There was an error recording.");
		}
		else
			chat->SendMessage(p, "You must specify a filename to record to.");
	}
	else if (strncasecmp(params, "play", 4) == 0)
	{
		const char *fn = params + 4;
		while (*fn && isspace(*fn)) fn++;
		if (*fn)
		{
			if (start_playback(a, fn))
				/* this will issue an arena message */;
			else
				chat->SendMessage(p, "There was an error playing the recorded game.");
		}
		else
		{
			int state, isp;
			LOCK(a);
			state = ra->state;
			isp = ra->ispaused;
			UNLOCK(a);
			if (state == s_playing && isp)
				MPAdd(&ra->mpq, (void*)PC_RESUME);
			else
				chat->SendMessage(p, "You must specify a filename to play.");
		}
	}
	else if (strcasecmp(params, "stop") == 0)
	{
		int state;
		LOCK(a);
		state = ra->state;
		UNLOCK(a);
		if (state == s_none)
			chat->SendMessage(p, "There's nothing being played or recorded here.");
		else if (state == s_playing)
		{
			if (stop_playback(a))
				chat->SendMessage(p, "Stopped playback.");
			else
				chat->SendMessage(p, "There was an error stopping playback.");
		}
		else if (state == s_recording)
		{
			if (stop_recording(a))
				chat->SendMessage(p, "Stopped recording.");
			else
				chat->SendMessage(p, "There was an error stopping recording.");
		}
		else
			chat->SendMessage(p, "The recorder module is in an invalid state.");
	}
	else if (strcasecmp(params, "pause") == 0)
	{
		int state, isp;
		LOCK(a);
		state = ra->state;
		isp = ra->ispaused;
		UNLOCK(a);
		if (state == s_playing)
			MPAdd(&ra->mpq, isp ? (void*)PC_RESUME : (void*)PC_PAUSE);
		else
			chat->SendMessage(p, "There is no game being played back here.");
	}
	else if (strcasecmp(params, "restart") == 0)
	{
		int state;
		LOCK(a);
		state = ra->state;
		UNLOCK(a);
		if (state == s_playing)
			MPAdd(&ra->mpq, (void*)PC_RESTART);
		else
			chat->SendMessage(p, "There is no game being played back here.");
	}
	else
	{
		LOCK(a);
		switch (ra->state)
		{
			case s_none:
				chat->SendMessage(p, "No games are being played or recorded.");
				break;
			case s_recording:
				chat->SendMessage(p, "A game is being recorded (to '%s').",
						ra->fname);
				break;
			case s_playing:
				chat->SendMessage(p, "A game is being played (from '%s'), "
						"current pos %.1f%%%s",
						ra->fname,
						ra->curpos,
						ra->ispaused ? ", paused" : "");
				break;
			default:
				chat->SendMessage(p, "The recorder module is in an invalid state.");
		}
		UNLOCK(a);
	}
}


local void cb_aaction(Arena *a, int action)
{
	rec_adata *ra = P_ARENA_DATA(a, adkey);

	if (action == AA_CREATE)
	{
		ra->state = s_none;
	}
	else if (action == AA_DESTROY)
	{
		if (ra->state == s_recording)
			stop_recording(a);
		else if (ra->state == s_playing)
			stop_playback(a);
	}
}


EXPORT int MM_record(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		fake = mm->GetInterface(I_FAKE, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		if (!aman || !pd || !cmd || !game || !fake || !lm || !net || !chat || !cfg)
			return MM_FAIL;
		adkey = aman->AllocateArenaData(sizeof(rec_adata));
		if (adkey == -1) return MM_FAIL;
		cmd->AddCommand("gamerecord", Cgamerecord, gamerecord_help);
		cmd->AddCommand("rec", Cgamerecord, gamerecord_help);
		net->AddPacket(C2S_POSITION, ppk);
		mm->RegCallback(CB_ARENAACTION, cb_aaction, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		Arena *a;
		rec_adata *ra;
		Link *link;

		/* make sure that there is nothing being played or recorded
		 * right now */
		aman->Lock();
		FOR_EACH_ARENA_P(a, ra, adkey)
			if (ra->state != s_none)
			{
				aman->Unlock();
				return MM_FAIL;
			}
		aman->Unlock();

		aman->FreeArenaData(adkey);
		mm->UnregCallback(CB_ARENAACTION, cb_aaction, ALLARENAS);
		net->RemovePacket(C2S_POSITION, ppk);
		cmd->RemoveCommand("gamerecord", Cgamerecord);
		cmd->RemoveCommand("rec", Cgamerecord);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(fake);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cfg);
		return MM_OK;
	}
	return MM_FAIL;
}

