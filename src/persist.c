
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#ifndef WIN32
#include <unistd.h>
#include <db1/db.h>
#else
#include "db.h"
#endif

#include "asss.h"


/* defines */

typedef enum db_command
{
	DB_NULL,
	DB_GET,
	DB_PUT,
	DB_SYNCWAIT,
	DB_PUTALL
} db_command;

/* structs */

typedef struct DBMessage
{
	db_command command;
	int pid, data;
	void (*callback)(int pid);
} DBMessage;


/* prototypes */

/* interface funcs */
local void RegPersistantData(const PersistantData *pd);
local void UnregPersistantData(const PersistantData *pd);
local void SyncToFile(int pid, int arena, void (*callback)(int pid));
local void SyncFromFile(int pid, int arena, void (*callback)(int pid));
local void StabilizeScores(int seconds);

/* timer func */
int SyncTimer(void *);

/* private funcs */
local void *DBThread(void *);
local void PersistAA(int arena, int action);
local DB *OpenDB(char *fname);
local void CloseDB(DB *db);


/* private data */

local Imodman *mm;
local Ilogman *lm;
local Iconfig *cfg;
local Iarenaman *aman;
local Iplayerdata *pd;
local Imainloop *ml;

local ArenaData *arenas;

local pthread_t dbthread;
local MPQueue dbq;

local LinkedList ddlist;
/* this mutex protects both ddlist and the databases array below */
local pthread_mutex_t dbmtx = PTHREAD_MUTEX_INITIALIZER;

/* big array of DB's */
local DB *databases[MAXARENA];
/* maintain these separately. globaldb is obvious. defarenadb is needed
 * because otherwise we would end up opening the same scores.db file
 * multiple times, and that would be bad. */
local DB *globaldb, *defarenadb;

local int cfg_syncseconds;

local Ipersist _myint =
{
	INTERFACE_HEAD_INIT(I_PERSIST, "persist-bdb185")
	RegPersistantData, UnregPersistantData,
	SyncToFile, SyncFromFile, StabilizeScores
};



EXPORT int MM_persist(int action, Imodman *_mm, int arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);

		arenas = aman->arenas;

		mm->RegCallback(CB_ARENAACTION, PersistAA, ALLARENAS);

		LLInit(&ddlist);
		MPInit(&dbq);
		pthread_create(&dbthread, NULL, DBThread, NULL);

		globaldb = OpenDB("global.db");
		defarenadb = OpenDB("defaultarena/scores.db");

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
		mm->UnregCallback(CB_ARENAACTION, PersistAA, ALLARENAS);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(aman);

		MPAdd(&dbq, NULL);
		pthread_join(dbthread, NULL);
		MPDestroy(&dbq);
		LLEmpty(&ddlist);

		if (globaldb) globaldb->close(globaldb);
		if (defarenadb) defarenadb->close(defarenadb);

		return MM_OK;
	}
	return MM_FAIL;
}



local inline int good_arena(int data_arena, int arena)
{
	if (data_arena == PERSIST_GLOBAL && arena == PERSIST_GLOBAL)
		return 1;
	else if (data_arena == PERSIST_ALLARENAS && ARENA_OK(arena))
		return 1;
	else if (data_arena == arena)
		return 1;
	else
		return 0;
}


local void DoPut(int pid, int arena)
{
	struct
	{
		char namebuf[24];
		int key;
	} keydata;
	int size = 0, err;
	byte buf[MAXPERSISTLENGTH];
	Link *l;
	DB *db;
	DBT key, val;

	db = (arena == PERSIST_GLOBAL) ? globaldb : databases[arena];

	if (!db)
		/* no scores for this arena */
		return;

	strncpy(keydata.namebuf, pd->players[pid].name, 24);

	for (l = LLGetHead(&ddlist); l; l = l->next)
	{
		PersistantData *data = (PersistantData*)l->data;
		if (good_arena(data->arena, arena))
		{
			/* get data */
			pd->LockPlayer(pid);
			size = data->GetData(pid, buf, sizeof(buf));
			pd->UnlockPlayer(pid);

			if (size > 0)
			{
				/* put in db */
				keydata.key = data->key;
				key.data = &keydata;
				key.size = sizeof(keydata);
				val.data = buf;
				val.size = size;
				err = db->put(db, &key, &val, 0);
				if (err == -1)
					lm->Log(L_WARN, "<persist> {%s} Error %d entering key in database",
							(arena == PERSIST_GLOBAL) ? "<global>" : arenas[arena].name);
			}
		}
	}
}


local void DoGet(int pid, int arena)
{
	struct
	{
		char namebuf[24];
		int key;
	} keydata;
	Link *l;
	DB *db;

	pd->LockPlayer(pid);

	/* first clear data */
	for (l = LLGetHead(&ddlist); l; l = l->next)
	{
		PersistantData *data = (PersistantData*)l->data;
		if (good_arena(data->arena, arena))
			data->ClearData(pid);
	}

	db = (arena == PERSIST_GLOBAL) ? globaldb : databases[arena];

	if (!db) return;

	/* now try to retrieve it */
	strncpy(keydata.namebuf, pd->players[pid].name, 24);

	for (l = LLGetHead(&ddlist); l; l = l->next)
	{
		PersistantData *data = (PersistantData*)l->data;

		if (good_arena(data->arena, arena))
		{
			int err;
			DBT key, val;

			/* try to get data */
			keydata.key = data->key;
			key.data = &keydata;
			key.size = sizeof(keydata);

			err = db->get(db, &key, &val, 0);
			if (err == 0)
				data->SetData(pid, val.data, val.size);
		}
	}

	pd->UnlockPlayer(pid);
}


void *DBThread(void *dummy)
{
	DBMessage *msg;
	int i;

	for (;;)
	{
		/* get next command */
		msg = MPRemove(&dbq);
		/* break on null */
		if (!msg)
			break;
		/* lock data descriptor lists */
		pthread_mutex_lock(&dbmtx);
		/* and do something with it */
		switch (msg->command)
		{
			case DB_NULL: break;

			case DB_GET: DoGet(msg->pid, msg->data); break;

			case DB_PUT: DoPut(msg->pid, msg->data); break;

			case DB_PUTALL:
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
						DoPut(i, arena);
					}
					/* if we're doing a lot of work, at least be nice
					 * about it */
					sched_yield();
				}
				break;

			case DB_SYNCWAIT:
				for (i = 0; i < MAXARENA; i++)
					if (databases[i])
						databases[i]->sync(databases[i], 0);
				globaldb->sync(globaldb, 0);
				defarenadb->sync(defarenadb, 0);
				/* make sure nobody modifies db's for some time */
				sleep(msg->data);
				break;
		}
		/* and unlock */
		pthread_mutex_unlock(&dbmtx);
		/* if we were looking for notification, notify */
		if (msg->callback)
			msg->callback(msg->pid);
		/* free the message */
		afree(msg);
		/* and give up some time */
		sched_yield();
	}

	/* sync before leaving */
	for (i = 0; i < MAXARENA; i++)
		if (databases[i])
			databases[i]->sync(databases[i], 0);
	globaldb->sync(globaldb, 0);
	defarenadb->sync(defarenadb, 0);

	return NULL;
}


DB *OpenDB(char *name)
{
	DB *db = dbopen(name, O_CREAT|O_RDWR, 0644, DB_BTREE, NULL);
	/* sync once to avoid being left with zero-length files */
	if (db) db->sync(db, 0);
	return db;
}

void CloseDB(DB *db)
{
	if (db != globaldb && db != defarenadb)
		db->close(db);
}


void PersistAA(int arena, int action)
{
	DB *db;
	if (action == AA_CREATE)
	{
		/* lock here */
		pthread_mutex_lock(&dbmtx);

		if (databases[arena])
		{
			lm->Log(L_ERROR, "<persist> {%s} Score database already exists for new arena", arenas[arena].name);
			CloseDB(databases[arena]);
		}

		if (arenas[arena].ispublic)
			db = defarenadb;
		else
		{
			char fname[PATH_MAX];
			snprintf(fname, PATH_MAX, "arenas/%s/scores.db", arenas[arena].name);
			db = OpenDB(fname);
		}

		if (!db)
			lm->Log(L_INFO, "<persist> {%s} Can't open/create database; no scores will be saved", arenas[arena].name);

		/* enter in db array */
		databases[arena] = db;

		pthread_mutex_unlock(&dbmtx);
		/* and unlock */
	}
	else if (action == AA_DESTROY)
	{
		pthread_mutex_lock(&dbmtx);

		db = databases[arena];

		if (db)
			CloseDB(db);
		else
			lm->Log(L_DRIVEL, "<persist> {%s} Score database doesn't exist for closing arena", arenas[arena].name);

		databases[arena] = NULL;

		pthread_mutex_unlock(&dbmtx);
	}
}


void RegPersistantData(const PersistantData *pd)
{
	pthread_mutex_lock(&dbmtx);
	LLAdd(&ddlist, (void*)pd);
	pthread_mutex_unlock(&dbmtx);
}


void UnregPersistantData(const PersistantData *pd)
{
	pthread_mutex_lock(&dbmtx);
	LLRemove(&ddlist, (void*)pd);
	pthread_mutex_unlock(&dbmtx);
}


void SyncToFile(int pid, int arena, void (*callback)(int pid))
{
	DBMessage *msg = amalloc(sizeof(DBMessage));

	msg->command = DB_PUT;
	msg->pid = pid;
	msg->data = arena;
	msg->callback = callback;

	MPAdd(&dbq, msg);
}


void SyncFromFile(int pid, int arena, void (*callback)(int pid))
{
	DBMessage *msg = amalloc(sizeof(DBMessage));

	msg->command = DB_GET;
	msg->pid = pid;
	msg->data = arena;
	msg->callback = callback;

	MPAdd(&dbq, msg);
}


void StabilizeScores(int seconds)
{
	DBMessage *msg = amalloc(sizeof(DBMessage));

	msg->command = DB_SYNCWAIT;
	msg->data = seconds;
	msg->callback = NULL;

	MPAdd(&dbq, msg);
}


int SyncTimer(void *dummy)
{
	DBMessage *msg = amalloc(sizeof(DBMessage));
	msg->command = DB_PUTALL;
	msg->callback = NULL;
	MPAdd(&dbq, msg);

	msg = amalloc(sizeof(DBMessage));
	msg->command = DB_SYNCWAIT;
	msg->data = 0;
	msg->callback = NULL;
	MPAdd(&dbq, msg);

	lm->Log(L_DRIVEL, "<persist> Collecting all persistant data and syncing to disk");

	return 1;
}

