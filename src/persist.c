
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#ifndef WIN32
#include <unistd.h>
#else
#include <direct.h>
#endif

#include "db.h"

#if DB_VERSION_MAJOR < 4
#error This version of bdb is too old.
#elif DB_VERSION_MAJOR > 5
#warning Your version of bdb is too new. Things might not work right.
#endif

#include "asss.h"

#include "db_layout.h"


/* defines */

#define MAXSGLEN 16

typedef enum db_command
{
	DBCMD_NULL,        /* no params */
	DBCMD_GET_PLAYER,  /* data1 = pid, data2 = arena */
	DBCMD_PUT_PLAYER,  /* data1 = pid, data2 = arena */
	DBCMD_GET_ARENA,   /* data1 = arena */
	DBCMD_PUT_ARENA,   /* data1 = arena */
	DBCMD_SYNCWAIT,    /* data1 = seconds */
	DBCMD_PUTALL,      /* no params */
	DBCMD_ENDINTERVAL  /* data1 = arena, data2 = interval */
} db_command;

/* structs */

typedef struct DBMessage
{
	db_command command;
	int data1, data2;
	void (*callback)(int pid);
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
local struct
{
	char score_group[MAXSGLEN]; /* for shared intervals */
	char name[MAXSGLEN]; /* for non-shared intervals */
} arena_data[MAXARENA];

local DB_ENV *dbenv;
local DB *db;

local int cfg_syncseconds;



local inline int good_arena(int scope, int arena)
{
	if (scope == PERSIST_ALLARENAS && ARENA_OK(arena))
		return TRUE;
	else if (scope == arena)
		return TRUE;
	else
		return FALSE;
}


local void fill_in_ag(char buf[MAXSGLEN], int arena, int interval)
{
	if (arena == PERSIST_GLOBAL)
		strncpy(buf, SG_GLOBAL, MAXSGLEN);
	else if (INTERVAL_IS_SHARED(interval))
		strncpy(buf, arena_data[arena].score_group, MAXSGLEN);
	else
		strncpy(buf, arena_data[arena].name, MAXSGLEN);
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
		lm->Log(L_INFO, "<persist> Initializing serial number for "
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


local void put_one_arena(PersistentData *data, int arena, int serialno)
{
	byte buf[MAXPERSISTLENGTH];
	struct arena_record_key keydata;
	int size, err;
	DBT key, val;

	memset(&keydata, 0, sizeof(keydata));
	strncpy(keydata.arena, aman->arenas[arena].name, sizeof(keydata.arena));

	/* get data */
	size = data->GetData(arena, buf, sizeof(buf));

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
local void put_one_player(PersistentData *data, int pid, int arena, int serialno)
{
	byte buf[MAXPERSISTLENGTH];
	struct player_record_key keydata;
	int size, err;
	DBT key, val;

	memset(&keydata, 0, sizeof(keydata));
	strncpy(keydata.name, pd->players[pid].name, sizeof(keydata.name));

	/* get data */
	size = data->GetData(pid, buf, sizeof(buf));

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


local void get_one_arena(PersistentData *data, int arena, int serialno)
{
	struct arena_record_key keydata;
	byte buf[MAXPERSISTLENGTH];
	int err;
	DBT key, val;

	/* always clear data first */
	data->ClearData(arena);

	/* prepare key */
	memset(&keydata, 0, sizeof(keydata));
	strncpy(keydata.arena, aman->arenas[arena].name, sizeof(keydata.arena));
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
		data->SetData(arena, val.data, val.size);
	else if (err != DB_NOTFOUND)
		lm->Log(L_WARN, "<persist> db->get error (2): %s",
				db_strerror(err));
}


/* call with player locked */
local void get_one_player(PersistentData *data, int pid, int arena, int serialno)
{
	struct player_record_key keydata;
	byte buf[MAXPERSISTLENGTH];
	int err;
	DBT key, val;

	/* always clear data first */
	data->ClearData(pid);

	/* prepare key */
	memset(&keydata, 0, sizeof(keydata));
	strncpy(keydata.name, pd->players[pid].name, sizeof(keydata.name));
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
		data->SetData(pid, val.data, val.size);
	else if (err != DB_NOTFOUND)
		lm->Log(L_WARN, "<persist> db->get error (3): %s",
				db_strerror(err));
}


local void do_put_arena(int arena)
{
	Link *l;
	for (l = LLGetHead(&arenapd); l; l = l->next)
	{
		PersistentData *data = (PersistentData*)l->data;
		if (good_arena(data->scope, arena))
			put_one_arena(l->data, arena, -1);
	}
}


local void do_put_player(int pid, int arena)
{
	Link *l;

	pd->LockPlayer(pid);

	for (l = LLGetHead(&playerpd); l; l = l->next)
	{
		PersistentData *data = (PersistentData*)l->data;
		if (good_arena(data->scope, arena))
			put_one_player(l->data, pid, arena, -1);
	}

	pd->UnlockPlayer(pid);
}


local void do_get_arena(int arena)
{
	Link *l;
	for (l = LLGetHead(&arenapd); l; l = l->next)
	{
		PersistentData *data = (PersistentData*)l->data;
		if (good_arena(data->scope, arena))
			get_one_arena(data, arena, -1);
	}
}


local void do_get_player(int pid, int arena)
{
	Link *l;

	pd->LockPlayer(pid);

	for (l = LLGetHead(&playerpd); l; l = l->next)
	{
		PersistentData *data = (PersistentData*)l->data;
		if (good_arena(data->scope, arena))
			get_one_player(data, pid, arena, -1);
	}

	pd->UnlockPlayer(pid);
}


local void do_end_interval(int arena, int interval)
{
	char ag[MAXSGLEN];
	Link *l;
	int statmin, statmax, pid;
	unsigned int serialno;

	/* get serial number */
	fill_in_ag(ag, arena, interval);
	serialno = get_serialno(ag, interval);

	/* figure out when to mess with data */
	if (arena == PERSIST_GLOBAL)
	{
		/* global data is loaded during S_WAIT_GLOBAL_SYNC, so we want to
		 * perform the getting/clearing if the player is after that. */
		statmin = S_WAIT_GLOBAL_SYNC;
		/* it's saved during S_LEAVING_ZONE, so perform the get/clear if
		 * the player is before that. */
		statmax = S_LEAVING_ZONE;
	}
	else
	{
		/* similar to above */
		statmin = S_WAIT_ARENA_SYNC;
		statmax = S_LEAVING_ARENA;
	}

	/* now get/clear all data for players in this arena/interval */
	for (pid = 0; pid < MAXPLAYERS; pid++)
	{
		pd->LockPlayer(pid);
		if (pd->players[pid].status > statmin &&
		    pd->players[pid].status < statmax &&
		    (arena == PERSIST_GLOBAL || pd->players[arena].arena == arena))
			for (l = LLGetHead(&playerpd); l; l = l->next)
			{
				PersistentData *data = (PersistentData*)l->data;
				if (good_arena(data->scope, arena) &&
				    data->interval == interval)
				{
					/* first, grab the latest data and dump it under the
					 * old serialno. */
					put_one_player(data, pid, arena, serialno);
					/* then clear data that will be associated with the
					 * new serialno. */
					data->ClearData(pid);
				}
			}
		pd->UnlockPlayer(pid);
	}

	/* now get/clear all data for this arena/interval itself */
	for (l = LLGetHead(&arenapd); l; l = l->next)
	{
		
		PersistentData *data = (PersistentData*)l->data;
		if (good_arena(data->scope, arena) &&
		    data->interval == interval)
		{
			/* grab latest arena data */
			put_one_arena(data, arena, serialno);
			/* then clear it */
			data->ClearData(arena);
		}
	}

	/* increment the serial number for this interval */
	serialno++;
	put_serialno(ag, interval, serialno);
}


local void *DBThread(void *dummy)
{
	DBMessage *msg;
	int i;

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
				do_get_player(msg->data1, msg->data2);
				break;

			case DBCMD_PUT_PLAYER:
				do_put_player(msg->data1, msg->data2);
				break;

			case DBCMD_GET_ARENA:
				do_get_arena(msg->data1);
				break;

			case DBCMD_PUT_ARENA:
				do_put_arena(msg->data1);
				break;

			case DBCMD_PUTALL:
				/* try to sync all players */
				for (i = 0; i < MAXPLAYERS; i++)
				{
					int status, arena;
					pd->LockStatus();
					status = pd->players[i].status;
					arena = pd->players[i].arena;
					pd->UnlockStatus();
					if (status == S_PLAYING)
					{
						do_put_player(i, PERSIST_GLOBAL);
						if (ARENA_OK(arena))
							do_put_player(i, arena);
					}
					/* if we're doing a lot of work, at least be nice
					 * about it */
				}

				/* now sync all arenas */
				for (i = 0; i < MAXARENA; i++)
				{
					int status;
					aman->LockStatus();
					status = aman->arenas[i].status;
					aman->UnlockStatus();
					if (status == ARENA_RUNNING)
						do_put_arena(i);
					pthread_mutex_unlock(&dbmtx);
					sched_yield();
					pthread_mutex_lock(&dbmtx);
				}
				break;

			case DBCMD_SYNCWAIT:
				db->sync(db, 0);
				/* make sure nobody modifies db's for some time */
				pthread_mutex_unlock(&dbmtx);
				sleep(msg->data1);
				pthread_mutex_lock(&dbmtx);
				break;

			case DBCMD_ENDINTERVAL:
				do_end_interval(msg->data1, msg->data2);
				break;
		}

		/* and unlock */
		pthread_mutex_unlock(&dbmtx);

		/* if we were looking for notification, notify */
		if (msg->callback)
			msg->callback(msg->data1);

		/* free the message */
		afree(msg);

		/* and give up some time */
		sched_yield();
	}

	return NULL;
}


/* interface funcs */

local void RegPlayerPD(const PersistentData *pd)
{
	pthread_mutex_lock(&dbmtx);
	if (pd->interval >= 0 &&
	    pd->interval < MAX_INTERVAL &&
	    pd->scope >= PERSIST_GLOBAL &&
	    pd->scope < MAXARENA)
		LLAdd(&playerpd, (void*)pd);
	pthread_mutex_unlock(&dbmtx);
}

local void UnregPlayerPD(const PersistentData *pd)
{
	pthread_mutex_lock(&dbmtx);
	LLRemove(&playerpd, (void*)pd);
	pthread_mutex_unlock(&dbmtx);
}


local void RegArenaPD(const PersistentData *pd)
{
	pthread_mutex_lock(&dbmtx);
	if (pd->interval >= 0 &&
	    pd->interval < MAX_INTERVAL &&
	    pd->scope >= PERSIST_ALLARENAS &&
	    pd->scope < MAXARENA)
		LLAdd(&arenapd, (void*)pd);
	pthread_mutex_unlock(&dbmtx);
}

local void UnregArenaPD(const PersistentData *pd)
{
	pthread_mutex_lock(&dbmtx);
	LLRemove(&arenapd, (void*)pd);
	pthread_mutex_unlock(&dbmtx);
}


local void PutPlayer(int pid, int arena, void (*callback)(int pid))
{
	DBMessage *msg = amalloc(sizeof(*msg));

	msg->command = DBCMD_PUT_PLAYER;
	msg->data1 = pid;
	msg->data2 = arena;
	msg->callback = callback;

	MPAdd(&dbq, msg);
}

local void GetPlayer(int pid, int arena, void (*callback)(int pid))
{
	DBMessage *msg = amalloc(sizeof(*msg));

	msg->command = DBCMD_GET_PLAYER;
	msg->data1 = pid;
	msg->data2 = arena;
	msg->callback = callback;

	MPAdd(&dbq, msg);
}

local void PutArena(int arena, void (*callback)(int pid))
{
	DBMessage *msg = amalloc(sizeof(*msg));

	msg->command = DBCMD_PUT_ARENA;
	msg->data1 = arena;
	msg->callback = callback;

	MPAdd(&dbq, msg);
}

local void GetArena(int arena, void (*callback)(int pid))
{
	DBMessage *msg = amalloc(sizeof(*msg));

	msg->command = DBCMD_GET_ARENA;
	msg->data1 = arena;
	msg->callback = callback;

	MPAdd(&dbq, msg);
}

local void EndInterval(int arena, int interval)
{
	DBMessage *msg = amalloc(sizeof(*msg));

	msg->command = DBCMD_ENDINTERVAL;
	msg->data1 = arena;
	msg->data2 = interval;
	msg->callback = NULL;

	MPAdd(&dbq, msg);
}

local void StabilizeScores(int seconds, int query, void (*callback)(int dummy))
{
	DBMessage *msg = amalloc(sizeof(*msg));

	if (query)
	{
		DBMessage *msg2 = amalloc(sizeof(*msg2));
		msg2->command = DBCMD_PUTALL;
		msg2->callback = NULL;
		MPAdd(&dbq, msg2);
	}

	msg->command = DBCMD_SYNCWAIT;
	msg->data1 = seconds;
	msg->callback = callback;

	MPAdd(&dbq, msg);
}


local int SyncTimer(void *dummy)
{
	DBMessage *msg = amalloc(sizeof(*msg));
	msg->command = DBCMD_PUTALL;
	msg->callback = NULL;
	MPAdd(&dbq, msg);

	msg = amalloc(sizeof(*msg));
	msg->command = DBCMD_SYNCWAIT;
	msg->data1 = 0;
	msg->callback = NULL;
	MPAdd(&dbq, msg);

	lm->Log(L_DRIVEL, "<persist> Collecting all persistent data and syncing to disk");

	return 1;
}


/* other stuff */

local void aaction(int arena, int action)
{
	pthread_mutex_lock(&dbmtx);

	if (action == AA_CREATE || action == AA_DESTROY)
	{
		memset(arena_data[arena].score_group, 0, MAXSGLEN);
		memset(arena_data[arena].name, 0, MAXSGLEN);
	}

	if (action == AA_CREATE)
	{
		/* score_group is used for shared intervals */
		if (aman->arenas[arena].ispublic)
			astrncpy(arena_data[arena].score_group, SG_PUBLIC, MAXSGLEN);
		else
			astrncpy(arena_data[arena].score_group, aman->arenas[arena].basename, MAXSGLEN);

		/* name is used for non-shared intervals */
		astrncpy(arena_data[arena].name, aman->arenas[arena].name, MAXSGLEN);
	}

	pthread_mutex_unlock(&dbmtx);
}


local int init_db(void)
{
	int err;
	mkdir(DB_HOME, 0755);
	if ((err = db_env_create(&dbenv, 0)))
	{
		fprintf(stderr, "db_env_create: %s\n", db_strerror(err));
		return MM_FAIL;
	}
	if ((err = dbenv->open(
				dbenv,
				DB_HOME,
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
				DB_FILENAME,
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


EXPORT int MM_persist(int action, Imodman *_mm, int arena)
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

		ml->SetTimer(SyncTimer, 12000, cfg_syncseconds * 100, NULL, -1);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_myint, ALLARENAS))
			return MM_FAIL;
		ml->ClearTimer(SyncTimer, -1);
		mm->UnregCallback(CB_ARENAACTION, aaction, ALLARENAS);
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

