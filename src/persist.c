
/* dist: public */

#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#ifndef WIN32
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#else
#include <direct.h>
#endif

#include "db.h"

#ifdef DB_VERSION_MAJOR
#if DB_VERSION_MAJOR < 4
#error This version of bdb is too old.
#elif DB_VERSION_MAJOR > 5
#warning Your version of bdb is too new. Things might not work right.
#endif
#else
#error This version of bdb is too old.
#endif

#include "asss.h"
#include "persist.h"


/* defines */

#define MAXPERSISTLENGTH CFG_MAX_PERSIST_LENGTH


typedef enum db_command
{
	DBCMD_NULL,        /* no params */
	DBCMD_GET_PLAYER,  /* data1 = player, data2 = arena */
	DBCMD_PUT_PLAYER,  /* data1 = player, data2 = arena */
	DBCMD_GET_ARENA,   /* data1 = arena */
	DBCMD_PUT_ARENA,   /* data1 = arena */
	DBCMD_SYNCWAIT,    /* data1 = seconds */
	DBCMD_PUTALL,      /* no params */
	DBCMD_ENDINTERVAL  /* agorname = ag (for shared) or name (nonshared), data2 = interval */
} db_command;

/* structs */

typedef struct DBMessage
{
	db_command command;
	Player *p;
	Arena *arena;
	char agorname[MAXAGLEN];
	int data;
	void (*playercb)(Player *p);
	void (*arenacb)(Arena *a);
} DBMessage;

/* private data */

local Imodman *mm;
local Ilogman *lm;
local Iconfig *cfg;
local Iplayerdata *pd;
local Imainloop *ml;
local Iarenaman *aman;

local pthread_t dbthread;
local MPQueue dbq;

local pthread_mutex_t dbmtx = PTHREAD_MUTEX_INITIALIZER;
/* this mutex protects these vars */
local LinkedList playerpd;
local LinkedList arenapd;
struct adata
{
	char arenagrp[MAXAGLEN]; /* for shared intervals */
	char name[MAXAGLEN]; /* for non-shared intervals */
};
local int adkey;

local DB_ENV *dbenv;
local DB *db;

local int cfg_syncseconds;



local void fill_in_ag(char buf[MAXAGLEN], Arena *arena, int interval)
{
	struct adata *adata = P_ARENA_DATA(arena, adkey);
	if (arena == NULL)
		strncpy(buf, AG_GLOBAL, MAXAGLEN);
	else if (INTERVAL_IS_SHARED(interval))
		strncpy(buf, adata->arenagrp, MAXAGLEN);
	else
		strncpy(buf, adata->name, MAXAGLEN);
}


local void put_serialno(const char *sg, int interval, unsigned int serialno)
{
	struct current_serial_record_key curr;
	int err;
	DBT key, val;

	memset(&curr, 0, sizeof(curr));
	strncpy(curr.arenagrp, sg, sizeof(curr.arenagrp));
	curr.interval = interval;

	memset(&key, 0, sizeof(key));
	key.data = &curr;
	key.size = sizeof(curr);
	memset(&val, 0, sizeof(val));
	val.data = &serialno;
	val.size = sizeof(serialno);

	err = db->put(db, NULL, &key, &val, 0);
	if (err)
		lm->Log(L_WARN, "<persist> db->put error (1): %s",
				db_strerror(err));
}

local unsigned int get_serialno(const char *sg, int interval)
{
	DBT key, val;
	struct current_serial_record_key curr;
	int err;
	unsigned int serialno = 0;

	memset(&curr, 0, sizeof(curr));
	strncpy(curr.arenagrp, sg, sizeof(curr.arenagrp));
	curr.interval = interval;

	/* prepare for query */
	memset(&key, 0, sizeof(key));
	key.data = &curr;
	key.size = sizeof(curr);
	memset(&val, 0, sizeof(val));
	val.data = &serialno;
	val.ulen = sizeof(serialno);
	val.flags = DB_DBT_USERMEM;

	/* query current serial number for this sg/interval */
	err = db->get(db, NULL, &key, &val, 0);
	if (err == DB_NOTFOUND)
	{
		/* if it's not found, initialize it and return 0 */
		lm->Log(L_INFO, "<persist> initializing serial number for "
				"interval %s, arenagrp %s to zero",
				get_interval_name(interval), sg);
		put_serialno(sg, interval, 0);
		serialno = 0;
	}
	else if (err)
		lm->Log(L_WARN, "<persist> db->get error (1): %s",
				db_strerror(err));

	return serialno;
}


/* for the next four functions: the serialno param is just for
 * optimization if we happen to know it already (only in end_interval
 * for now). set to -1 if you don't know it. */


local void put_one_arena(ArenaPersistentData *data, Arena *arena, int serialno)
{
	byte buf[MAXPERSISTLENGTH];
	struct arena_record_key keydata;
	int size, err;
	DBT key, val;

	memset(&keydata, 0, sizeof(keydata));
	strncpy(keydata.arena, arena->name, sizeof(keydata.arena));

	/* get data */
	size = data->GetData(arena, buf, sizeof(buf), data->clos);

	if (size > 0)
	{
		/* prepare key */
		keydata.interval = data->interval;
		if (serialno == -1)
			keydata.serialno = get_serialno(keydata.arena, data->interval);
		else
			keydata.serialno = serialno;
		keydata.key = data->key;

		/* prepare dbt's */
		memset(&key, 0, sizeof(key));
		key.data = &keydata;
		key.size = sizeof(keydata);

		memset(&val, 0, sizeof(val));
		val.data = buf;
		val.size = size;

		/* put in db */
		err = db->put(db, NULL, &key, &val, 0);

		if (err)
			lm->Log(L_WARN, "<persist> db->put error (2): %s",
					db_strerror(err));
	}
	else
	{
		memset(&key, 0, sizeof(key));
		key.data = &keydata;
		key.size = sizeof(keydata);
		err = db->del(db, NULL, &key, 0);
		if (err != 0 && err != DB_NOTFOUND)
			lm->Log(L_WARN, "<persist> db->del error (1): %s",
					db_strerror(err));
	}
}


/* call with player locked */
local void put_one_player(PlayerPersistentData *data, Player *p, Arena *arena, int serialno)
{
	byte buf[MAXPERSISTLENGTH];
	struct player_record_key keydata;
	int size, err;
	DBT key, val;

	memset(&keydata, 0, sizeof(keydata));
	astrncpy(keydata.name, p->name, sizeof(keydata.name));
	ToLowerStr(keydata.name);

	/* get data */
	size = data->GetData(p, buf, sizeof(buf), data->clos);

	if (size > 0)
	{
		/* prepare key */
		keydata.interval = data->interval;
		fill_in_ag(keydata.arenagrp, arena, data->interval);
		if (serialno == -1)
			keydata.serialno = get_serialno(keydata.arenagrp, data->interval);
		else
			keydata.serialno = serialno;
		keydata.key = data->key;

		/* prepare dbt's */
		memset(&key, 0, sizeof(key));
		key.data = &keydata;
		key.size = sizeof(keydata);

		memset(&val, 0, sizeof(val));
		val.data = buf;
		val.size = size;

		/* put in db */
		err = db->put(db, NULL, &key, &val, 0);

		if (err)
			lm->Log(L_WARN, "<persist> db->put error (3): %s",
					db_strerror(err));
	}
	else
	{
		memset(&key, 0, sizeof(key));
		key.data = &keydata;
		key.size = sizeof(keydata);
		err = db->del(db, NULL, &key, 0);
		if (err != 0 && err != DB_NOTFOUND)
			lm->Log(L_WARN, "<persist> db->del error (2): %s",
					db_strerror(err));
	}
}


local void get_one_arena(ArenaPersistentData *data, Arena *arena, int serialno)
{
	struct arena_record_key keydata;
	byte buf[MAXPERSISTLENGTH];
	int err;
	DBT key, val;

	/* always clear data first */
	data->ClearData(arena, data->clos);

	/* prepare key */
	memset(&keydata, 0, sizeof(keydata));
	strncpy(keydata.arena, arena->name, sizeof(keydata.arena));
	keydata.interval = data->interval;
	if (serialno == -1)
		keydata.serialno = get_serialno(keydata.arena, data->interval);
	else
		keydata.serialno = serialno;
	keydata.key = data->key;

	/* prepare dbt's */
	memset(&key, 0, sizeof(key));
	key.data = &keydata;
	key.size = sizeof(keydata);

	memset(&val, 0, sizeof(val));
	val.data = buf;
	val.ulen = MAXPERSISTLENGTH;
	val.flags = DB_DBT_USERMEM;

	/* try to get data */
	err = db->get(db, NULL, &key, &val, 0);

	if (err == 0)
		data->SetData(arena, val.data, val.size, data->clos);
	else if (err != DB_NOTFOUND)
		lm->Log(L_WARN, "<persist> db->get error (2): %s",
				db_strerror(err));
}


/* call with player locked */
local void get_one_player(PlayerPersistentData *data, Player *p, Arena *arena, int serialno)
{
	struct player_record_key keydata;
	byte buf[MAXPERSISTLENGTH];
	int err;
	DBT key, val;

	/* always clear data first */
	data->ClearData(p, data->clos);

	/* prepare key */
	memset(&keydata, 0, sizeof(keydata));
	astrncpy(keydata.name, p->name, sizeof(keydata.name));
	ToLowerStr(keydata.name);
	keydata.interval = data->interval;
	fill_in_ag(keydata.arenagrp, arena, data->interval);
	if (serialno == -1)
		keydata.serialno = get_serialno(keydata.arenagrp, data->interval);
	else
		keydata.serialno = serialno;
	keydata.key = data->key;

	/* prepare dbt's */
	memset(&key, 0, sizeof(key));
	key.data = &keydata;
	key.size = sizeof(keydata);

	memset(&val, 0, sizeof(val));
	val.data = buf;
	val.ulen = MAXPERSISTLENGTH;
	val.flags = DB_DBT_USERMEM;

	/* try to get data */
	err = db->get(db, NULL, &key, &val, 0);

	if (err == 0)
		data->SetData(p, val.data, val.size, data->clos);
	else if (err != DB_NOTFOUND)
		lm->Log(L_WARN, "<persist> db->get error (3): %s",
				db_strerror(err));
}


local void do_put_arena(Arena *arena)
{
	Link *l;
	for (l = LLGetHead(&arenapd); l; l = l->next)
		put_one_arena(l->data, arena, -1);
}


local void do_put_player(Player *p, Arena *arena)
{
	Link *l;

	pd->LockPlayer(p);
	for (l = LLGetHead(&playerpd); l; l = l->next)
	{
		PlayerPersistentData *ppd = l->data;
		if ((ppd->scope == PERSIST_GLOBAL && arena == NULL) ||
		    (ppd->scope == PERSIST_ALLARENAS && arena != NULL))
			put_one_player(ppd, p, arena, -1);
	}
	pd->UnlockPlayer(p);
}


local int arena_match(Arena *arena, const char *ag, int interval)
{
	struct adata *adata = P_ARENA_DATA(arena, adkey);
	return arena && !strcmp(INTERVAL_IS_SHARED(interval) ? adata->arenagrp : adata->name, ag);
}


local void do_end_interval(const char *ag, int interval)
{
	/* ag is an ag or a name, depending on whether interval is shared */
	Link *link, *l;
	Player *p;
	Arena *arena;
	int statmin, statmax;
	int global = (strcmp(ag, AG_GLOBAL) == 0);
	unsigned int serialno;

	/* get serial number */
	serialno = get_serialno(ag, interval);

	/* figure out when to mess with data */
	if (global)
	{
		/* global data is loaded during S_WAIT_GLOBAL_SYNC, so we want to
		 * perform the getting/clearing if the player is after that. */
		statmin = S_DO_ARENA_CALLBACKS;
		/* after we've saved global data for the last time, status goes
		 * to S_TIMEWAIT, so if we're before that, we still have data to
		 * save. */
		statmax = S_WAIT_GLOBAL_SYNC2;
	}
	else
	{
		/* similar to above, but for arena data */
		statmin = S_SEND_ARENA_RESPONSE;
		statmax = S_WAIT_ARENA_SYNC2;
	}

	/* first get/clear all data for players in these arenas */
	pd->Lock();
	FOR_EACH_PLAYER(p)
	{
		pd->LockPlayer(p);
		if (p->status >= statmin &&
		    p->status <= statmax &&
		    (global || arena_match(p->arena, ag, interval)))
				for (l = LLGetHead(&playerpd); l; l = l->next)
				{
					PlayerPersistentData *data = l->data;
					if (data->interval == interval)
					{
						/* first, grab the latest data and dump it under the
						 * old serialno. */
						put_one_player(data, p, p->arena, serialno);
						/* then clear data that will be associated with the
						 * new serialno. */
						data->ClearData(p, data->clos);
					}
				}
		pd->UnlockPlayer(p);
	}
	pd->Unlock();

	/* then get/clear all data for these arenas themselves */
	aman->Lock();
	FOR_EACH_ARENA(arena)
		if (arena_match(arena, ag, interval))
			for (l = LLGetHead(&arenapd); l; l = l->next)
			{
				ArenaPersistentData *data = l->data;
				if (data->interval == interval)
				{
					/* grab latest arena data */
					put_one_arena(data, arena, serialno);
					/* then clear it */
					data->ClearData(arena, data->clos);
				}
			}
	aman->Unlock();

	/* finally increment the serial number for this ag/interval */
	serialno++;
	put_serialno(ag, interval, serialno);
}


local void *DBThread(void *dummy)
{
	DBMessage *msg;
	Link *l, *link;
	Arena *arena;
	Player *i;

	for (;;)
	{
		/* get next command */
		msg = MPRemove(&dbq);

		/* break on null */
		if (!msg) break;

		/* lock data descriptor lists */
		pthread_mutex_lock(&dbmtx);

		/* and do something with it */
		switch (msg->command)
		{
			case DBCMD_NULL:
				break;

			case DBCMD_GET_PLAYER:
				pd->LockPlayer(msg->p);
				for (l = LLGetHead(&playerpd); l; l = l->next)
				{
					PlayerPersistentData *ppd = l->data;
					if ((ppd->scope == PERSIST_GLOBAL && msg->arena == NULL) ||
					    (ppd->scope == PERSIST_ALLARENAS && msg->arena != NULL))
						get_one_player(ppd, msg->p, msg->arena, -1);
				}
				pd->UnlockPlayer(msg->p);
				break;

			case DBCMD_PUT_PLAYER:
				do_put_player(msg->p, msg->arena);
				break;

			case DBCMD_GET_ARENA:
				for (l = LLGetHead(&arenapd); l; l = l->next)
					get_one_arena(l->data, msg->arena, -1);
				break;

			case DBCMD_PUT_ARENA:
				do_put_arena(msg->arena);
				break;

			case DBCMD_PUTALL:
				/* try to sync all players */
				pd->Lock();
				FOR_EACH_PLAYER(i)
				{
					int status;
					Arena *arena;
					status = i->status;
					arena = i->arena;
					if (status == S_PLAYING)
					{
						do_put_player(i, NULL);
						if (arena)
							do_put_player(i, arena);
					}
					/* if we're doing a lot of work, at least be nice
					 * about it */
					pthread_mutex_unlock(&dbmtx);
					sched_yield();
					pthread_mutex_lock(&dbmtx);
				}
				pd->Unlock();

				/* now sync all arenas */
				aman->Lock();
				FOR_EACH_ARENA(arena)
				{
					if (arena->status == ARENA_RUNNING)
						do_put_arena(arena);
					pthread_mutex_unlock(&dbmtx);
					sched_yield();
					pthread_mutex_lock(&dbmtx);
				}
				aman->Unlock();
				break;

			case DBCMD_SYNCWAIT:
				db->sync(db, 0);
				/* make sure nobody modifies db's for some time */
				pthread_mutex_unlock(&dbmtx);
				sleep(msg->data);
				pthread_mutex_lock(&dbmtx);
				break;

			case DBCMD_ENDINTERVAL:
				do_end_interval(msg->agorname, msg->data);
				break;
		}

		/* and unlock */
		pthread_mutex_unlock(&dbmtx);

		/* if we were looking for notification, notify */
		if (msg->playercb) msg->playercb(msg->p);
		if (msg->arenacb) msg->arenacb(msg->arena);

		/* free the message */
		afree(msg);

		/* and give up some time */
		sched_yield();
	}

	return NULL;
}


/* interface funcs */

local void RegPlayerPD(const PlayerPersistentData *pd)
{
	pthread_mutex_lock(&dbmtx);
	if (pd->interval >= 0 &&
	    pd->interval < MAX_INTERVAL)
		LLAdd(&playerpd, (void*)pd);
	pthread_mutex_unlock(&dbmtx);
}

local void UnregPlayerPD(const PlayerPersistentData *pd)
{
	pthread_mutex_lock(&dbmtx);
	LLRemove(&playerpd, (void*)pd);
	pthread_mutex_unlock(&dbmtx);
}


local void RegArenaPD(const ArenaPersistentData *pd)
{
	pthread_mutex_lock(&dbmtx);
	if (pd->interval >= 0 &&
	    pd->interval < MAX_INTERVAL &&
	    pd->scope != PERSIST_GLOBAL)
		LLAdd(&arenapd, (void*)pd);
	pthread_mutex_unlock(&dbmtx);
}

local void UnregArenaPD(const ArenaPersistentData *pd)
{
	pthread_mutex_lock(&dbmtx);
	LLRemove(&arenapd, (void*)pd);
	pthread_mutex_unlock(&dbmtx);
}


local void PutPlayer(Player *p, Arena *arena, void (*callback)(Player *p))
{
	DBMessage *msg = amalloc(sizeof(*msg));

	msg->command = DBCMD_PUT_PLAYER;
	msg->p = p;
	msg->arena = arena;
	msg->playercb = callback;

	MPAdd(&dbq, msg);
}

local void GetPlayer(Player *p, Arena *arena, void (*callback)(Player *p))
{
	DBMessage *msg = amalloc(sizeof(*msg));

	msg->command = DBCMD_GET_PLAYER;
	msg->p = p;
	msg->arena = arena;
	msg->playercb = callback;

	MPAdd(&dbq, msg);
}

local void PutArena(Arena *arena, void (*callback)(Arena *a))
{
	DBMessage *msg = amalloc(sizeof(*msg));

	msg->command = DBCMD_PUT_ARENA;
	msg->arena = arena;
	msg->arenacb = callback;

	MPAdd(&dbq, msg);
}

local void GetArena(Arena *arena, void (*callback)(Arena *a))
{
	DBMessage *msg = amalloc(sizeof(*msg));

	msg->command = DBCMD_GET_ARENA;
	msg->arena = arena;
	msg->arenacb = callback;

	MPAdd(&dbq, msg);
}

local void EndInterval(const char *agorname, int interval)
{
	DBMessage *msg = amalloc(sizeof(*msg));

	msg->command = DBCMD_ENDINTERVAL;
	astrncpy(msg->agorname, agorname, sizeof(msg->agorname));
	msg->data = interval;

	MPAdd(&dbq, msg);
}

local void StabilizeScores(int seconds, int query, void (*callback)(Player *dummy))
{
	DBMessage *msg = amalloc(sizeof(*msg));

	if (query)
	{
		DBMessage *msg2 = amalloc(sizeof(*msg2));
		msg2->command = DBCMD_PUTALL;
		MPAdd(&dbq, msg2);
	}

	msg->command = DBCMD_SYNCWAIT;
	msg->data = seconds;
	msg->playercb = callback;

	MPAdd(&dbq, msg);
}


local int SyncTimer(void *dummy)
{
	DBMessage *msg = amalloc(sizeof(*msg));
	msg->command = DBCMD_PUTALL;
	MPAdd(&dbq, msg);

	msg = amalloc(sizeof(*msg));
	msg->command = DBCMD_SYNCWAIT;
	msg->data = 0;
	MPAdd(&dbq, msg);

	lm->Log(L_DRIVEL, "<persist> collecting all persistent data and syncing to disk");

	return 1;
}


/* other stuff */

local void aaction(Arena *arena, int action)
{
	struct adata *adata = P_ARENA_DATA(arena, adkey);

	pthread_mutex_lock(&dbmtx);

	if (action == AA_CREATE || action == AA_DESTROY)
	{
		memset(adata->arenagrp, 0, MAXAGLEN);
		memset(adata->name, 0, MAXAGLEN);
	}

	if (action == AA_CREATE)
	{
		/* arenagrp is used for shared intervals */
		if (arena->ispublic)
			astrncpy(adata->arenagrp, AG_PUBLIC, MAXAGLEN);
		else
			astrncpy(adata->arenagrp, arena->basename, MAXAGLEN);

		/* name is used for non-shared intervals */
		astrncpy(adata->name, arena->name, MAXAGLEN);
	}

	pthread_mutex_unlock(&dbmtx);
}


local int init_db(void)
{
	int err;
	mkdir(ASSS_DB_HOME, 0755);
	if ((err = db_env_create(&dbenv, 0)))
	{
		fprintf(stderr, "db_env_create: %s\n", db_strerror(err));
		return MM_FAIL;
	}
	if ((err = dbenv->open(
				dbenv,
				ASSS_DB_HOME,
				DB_INIT_CDB | DB_INIT_MPOOL | DB_CREATE,
				0644)))
	{
		fprintf(stderr, "db_env_create: %s\n", db_strerror(err));
		goto close_env;
	}
	if ((err = db_create(&db, dbenv, 0)))
	{
		fprintf(stderr, "db_env_create: %s\n", db_strerror(err));
		goto close_env;
	}
	if ((err = db->open(
				db,
#if DB_VERSION_MAJOR >= 4 && DB_VERSION_MINOR >= 1
				/* they added a transaction parameter to the db->open
				 * call in 4.1.0. */
				NULL,
#endif
				ASSS_DB_FILENAME,
				NULL,
				DB_BTREE,
				DB_CREATE,
				0644)))
	{
		fprintf(stderr, "db_env_create: %s\n", db_strerror(err));
		goto close_db;
	}
	/* sync once */
	db->sync(db, 0);

	return MM_OK;

close_db:
	db->close(db, 0);
close_env:
	dbenv->close(dbenv, 0);
	return MM_FAIL;
}

local void close_db(void)
{
	db->close(db, 0);
	dbenv->close(dbenv, 0);
}



local Ipersist _myint =
{
	INTERFACE_HEAD_INIT(I_PERSIST, "persist-db4")
	RegPlayerPD, UnregPlayerPD,
	RegArenaPD, UnregArenaPD,
	PutPlayer, GetPlayer,
	PutArena, GetArena,
	EndInterval,
	StabilizeScores
};


EXPORT int MM_persist(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		if (init_db() == MM_FAIL)
			return MM_FAIL;

		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);

		adkey = aman->AllocateArenaData(sizeof(struct adata));
		if (adkey == -1) return MM_FAIL;

		mm->RegCallback(CB_ARENAACTION, aaction, ALLARENAS);

		LLInit(&playerpd);
		LLInit(&arenapd);
		MPInit(&dbq);

		pthread_create(&dbthread, NULL, DBThread, NULL);

		mm->RegInterface(&_myint, ALLARENAS);

		/* cfghelp: Persist:SyncSeconds, global, int, def: 180
		 * The interval at which all persistent data is synced to the
		 * database. */
		cfg_syncseconds = cfg ?
				cfg->GetInt(GLOBAL, "Persist", "SyncSeconds", 180) : 180;

		ml->SetTimer(SyncTimer, 12000, cfg_syncseconds * 100, NULL, NULL);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_myint, ALLARENAS))
			return MM_FAIL;
		ml->ClearTimer(SyncTimer, NULL);
		mm->UnregCallback(CB_ARENAACTION, aaction, ALLARENAS);
		aman->FreeArenaData(adkey);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(ml);

		MPAdd(&dbq, NULL);
		pthread_join(dbthread, NULL);
		MPDestroy(&dbq);
		LLEmpty(&playerpd);
		LLEmpty(&arenapd);

		close_db();

		return MM_OK;
	}
	return MM_FAIL;
}

