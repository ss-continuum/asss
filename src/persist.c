
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

#include "asss.h"

#include "db_layout.h"


/* defines */

#define MAXSGLEN 16

typedef enum db_command
{
	DBCMD_NULL,
	DBCMD_GET,
	DBCMD_PUT,
	DBCMD_SYNCWAIT,
	DBCMD_PUTALL,
	DBCMD_ENDINTERVAL
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
local LinkedList ddlist;
local struct scope_data
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


local void fill_in_ag(char buf[16], int arena, int interval)
{
	if (arena == PERSIST_GLOBAL)
		strncpy(buf, SG_GLOBAL, sizeof(buf));
	else if (INTERVAL_IS_SHARED(interval))
		strncpy(buf, arena_data[arena].score_group, sizeof(buf));
	else
		strncpy(buf, arena_data[arena].name, sizeof(buf));
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


local void fill_in_record(struct player_record_key *key, int arena, int interval)
{
	key->interval = interval;
	fill_in_ag(key->arenagrp, arena, interval);
	key->serialno = get_serialno(key->arenagrp, interval);
}


local void DoPut(int pid, int arena)
{
	struct player_record_key keydata;
	int size = 0, err;
	byte buf[MAXPERSISTLENGTH];
	Link *l;
	DBT key, val;

	pd->LockPlayer(pid);

	memset(&keydata, 0, sizeof(keydata));
	strncpy(keydata.name, pd->players[pid].name, sizeof(keydata.name));

	for (l = LLGetHead(&ddlist); l; l = l->next)
	{
		PersistantData *data = (PersistantData*)l->data;
		if (good_arena(data->scope, arena))
		{
			/* get data */
			size = data->GetData(pid, buf, sizeof(buf));


			if (size > 0)
			{
				/* prepare key */
				fill_in_record(&keydata, arena, data->interval);
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
	}

	pd->UnlockPlayer(pid);
}


local void DoGet(int pid, int arena)
{
	struct player_record_key keydata;
	byte buf[MAXPERSISTLENGTH];
	Link *l;

	pd->LockPlayer(pid);

	memset(&keydata, 0, sizeof(keydata));
	strncpy(keydata.name, pd->players[pid].name, sizeof(keydata.name));

	for (l = LLGetHead(&ddlist); l; l = l->next)
	{
		PersistantData *data = (PersistantData*)l->data;

		if (good_arena(data->scope, arena))
		{
			int err;
			DBT key, val;

			/* always clear data first */
			data->ClearData(pid);

			/* prepare key */
			fill_in_record(&keydata, arena, data->interval);
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
			{
				data->SetData(pid, val.data, val.size);
			}
			else if (err != DB_NOTFOUND)
				lm->Log(L_WARN, "<persist> db->get error (2): %s",
						db_strerror(err));
		}
	}

	pd->UnlockPlayer(pid);
}


local void DoEnd(int arena, int interval)
{
	unsigned int serialno;
	char ag[16];
	Link *l;
	int statmin, statmax;

	/* increment the serial number for this interval */
	fill_in_ag(ag, arena, interval);
	serialno = get_serialno(ag, interval);
	serialno++;
	put_serialno(ag, interval, serialno);

	/* figure out when to clear data */
	if (arena == PERSIST_GLOBAL)
	{
		/* global data is loaded during S_WAIT_GLOBAL_SYNC, so we want to
		 * perform the clearing if the player is after that. */
		statmin = S_WAIT_GLOBAL_SYNC;
		/* it's saved during S_LEAVING_ZONE, so perform the clear if the
		 * player is before that. */
		statmax = S_LEAVING_ZONE;
	}
	else
	{
		/* similar to above */
		statmin = S_WAIT_ARENA_SYNC;
		statmax = S_LEAVING_ARENA;
	}

	/* now clear all data for this arena/interval */
	for (l = LLGetHead(&ddlist); l; l = l->next)
	{
		PersistantData *data = (PersistantData*)l->data;
		if (good_arena(data->scope, arena))
		{
			int pid;
			for (pid = 0; pid < MAXPLAYERS; pid++)
			{
				pd->LockPlayer(pid);
				if (pd->players[pid].status > statmin &&
				    pd->players[pid].status < statmax &&
				    (arena == PERSIST_GLOBAL || pd->players[arena].arena == arena))
					data->ClearData(pid);
				pd->UnlockPlayer(pid);
			}
		}
	}
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
			case DBCMD_NULL: break;

			case DBCMD_GET: DoGet(msg->data1, msg->data2); break;

			case DBCMD_PUT: DoPut(msg->data1, msg->data2); break;

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
						DoPut(i, PERSIST_GLOBAL);
						if (ARENA_OK(arena))
							DoPut(i, arena);
					}
					/* if we're doing a lot of work, at least be nice
					 * about it */
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

			case DBCMD_ENDINTERVAL: DoEnd(msg->data1, msg->data2); break;
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


local void RegPersistantData(const PersistantData *pd)
{
	pthread_mutex_lock(&dbmtx);
	if (pd->interval >= 0 &&
	    pd->interval < MAX_INTERVAL &&
	    pd->scope >= PERSIST_GLOBAL &&
	    pd->scope < MAXARENA)
		LLAdd(&ddlist, (void*)pd);
	pthread_mutex_unlock(&dbmtx);
}


local void UnregPersistantData(const PersistantData *pd)
{
	pthread_mutex_lock(&dbmtx);
	LLRemove(&ddlist, (void*)pd);
	pthread_mutex_unlock(&dbmtx);
}


local void SyncToFile(int pid, int arena, void (*callback)(int pid))
{
	DBMessage *msg = amalloc(sizeof(*msg));

	msg->command = DBCMD_PUT;
	msg->data1 = pid;
	msg->data2 = arena;
	msg->callback = callback;

	MPAdd(&dbq, msg);
}


local void SyncFromFile(int pid, int arena, void (*callback)(int pid))
{
	DBMessage *msg = amalloc(sizeof(*msg));

	msg->command = DBCMD_GET;
	msg->data1 = pid;
	msg->data2 = arena;
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


local void StabilizeScores(int seconds)
{
	DBMessage *msg = amalloc(sizeof(*msg));

	msg->command = DBCMD_SYNCWAIT;
	msg->data1 = seconds;
	msg->callback = NULL;

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

	lm->Log(L_DRIVEL, "<persist> Collecting all persistant data and syncing to disk");

	return 1;
}


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
		const char *sg = cfg->GetStr(aman->arenas[arena].cfg, "General", "ArenaGroup");
		snprintf(arena_data[arena].score_group, MAXSGLEN, "<%s>", sg ? sg : aman->arenas[arena].name);
		strncpy(arena_data[arena].name, aman->arenas[arena].name, MAXSGLEN);
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
	INTERFACE_HEAD_INIT(I_PERSIST, "persist-bdb185")
	RegPersistantData, UnregPersistantData,
	SyncToFile, SyncFromFile,
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

		LLInit(&ddlist);
		MPInit(&dbq);
		pthread_create(&dbthread, NULL, DBThread, NULL);

		mm->RegInterface(&_myint, ALLARENAS);

		cfg_syncseconds = cfg ?
				cfg->GetInt(GLOBAL, "Persist", "SyncSeconds", 180) : 180;

		ml->SetTimer(SyncTimer, 12000, cfg_syncseconds * 100, NULL);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		ml->ClearTimer(SyncTimer);
		if (mm->UnregInterface(&_myint, ALLARENAS))
			return MM_FAIL;
		mm->UnregCallback(CB_ARENAACTION, aaction, ALLARENAS);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfg);

		MPAdd(&dbq, NULL);
		pthread_join(dbthread, NULL);
		MPDestroy(&dbq);
		LLEmpty(&ddlist);

		close_db();

		return MM_OK;
	}
	return MM_FAIL;
}

