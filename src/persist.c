
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#ifndef WIN32
#include <unistd.h>
#include <db1/db.h>
#endif

#include "asss.h"


/* defines */

typedef enum db_command
{
	DB_NULL,
	DB_GET,
	DB_PUT,
	DB_SYNCWAIT,
	DB_PUTALL,
	DB_QUIT,
} db_command;

#define GLOBALTEST(arena,global) ((((arena)==PERSIST_GLOBAL) && (global)) || (((arena)!=PERSIST_GLOBAL) && (!(global))))

/* structs */

typedef struct DBMessage
{
	db_command command;
	int pid, data;
	void (*callback)(int pid);
} DBMessage;


/* prototypes */

/* interface funcs */
local void RegPersistantData(PersistantData const *pd);
local void UnregPersistantData(PersistantData const *pd);
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

		{
			DBMessage *msg = amalloc(sizeof(DBMessage));
			msg->command = DB_QUIT;
			MPAdd(&dbq, msg);
		}
		pthread_join(dbthread, NULL);
		MPDestroy(&dbq);
		LLEmpty(&ddlist);

		if (globaldb) globaldb->close(globaldb);
		if (defarenadb) defarenadb->close(defarenadb);

		return MM_OK;
	}
	return MM_FAIL;
}


local void DoPut(int pid, int arena)
{
	char namebuf[24];
	int size = 0;
	byte *value, *cp;
	Link *l;
	PersistantData *data;
	DB *db;
	DBT key, val;

	strncpy(namebuf, pd->players[pid].name, 24);

	for (l = LLGetHead(&ddlist); l; l = l->next)
	{
		data = (PersistantData*)l->data;
		if (GLOBALTEST(arena, data->global))
			size += data->length + 2 * sizeof(int); /* for length and id fields */
	}

	cp = value = alloca(size);

	pd->LockPlayer(pid);

	for (l = LLGetHead(&ddlist); l; l = l->next)
	{
		data = (PersistantData*)l->data;
		if (GLOBALTEST(arena, data->global))
		{
			int len = data->length, k = data->key;
			memcpy(cp, &len, sizeof(int));
			cp += sizeof(int);
			memcpy(cp, &k, sizeof(int));
			cp += sizeof(int);
			data->GetData(pid, cp);
			cp += len;
		}
	}

	pd->UnlockPlayer(pid);

	assert((cp - value) == size);

	/* LOCK: we don't lock player status here because arena shouldn't be
	 * changing while we're doing score stuff */
	db = (arena == PERSIST_GLOBAL) ? globaldb : databases[arena];

	key.data = namebuf;
	key.size = 24;
	val.data = value;
	val.size = size;

	if (!db)
		lm->Log(L_ERROR, "<persist> {%s} Database not open when we need it",
				(arena == PERSIST_GLOBAL) ? "<global>" : arenas[arena].name);
	else /* do it! */ 
		if (db->put(db, &key, &val, 0) == -1)
			lm->Log(L_ERROR, "<persist> {%s} Error entering key in database",
				(arena == PERSIST_GLOBAL) ? "<global>" : arenas[arena].name);
}


local void DoGet(int pid, int arena)
{
	char namebuf[24];
	int size = 0;
	byte *value, *cp;
	PersistantData *data;
	Link *l;
	DB *db;
	DBT key, val;

	pd->LockPlayer(pid);

	/* first clear data */
	for (l = LLGetHead(&ddlist); l; l = l->next)
	{
		data = (PersistantData*)l->data;
		if (GLOBALTEST(arena, data->global))
			data->ClearData(pid);
	}

	/* now try to retrieve it */
	strncpy(namebuf, pd->players[pid].name, 24);

	/* LOCK: see above */
	db = (arena == PERSIST_GLOBAL) ? globaldb : databases[arena];

	key.data = namebuf;
	key.size = 24;

	if (!db || db->get(db, &key, &val, 0) == -1)
		return; /* new player */

	cp = value = val.data;
	size = val.size;

	while ((cp - value) < size)
	{
		int len, k;
		memcpy(&len, cp, sizeof(int));
		cp += 4;
		memcpy(&k, cp, sizeof(int));
		cp += 4;
		for (l = LLGetHead(&ddlist); l; l = l->next)
		{
			data = (PersistantData*)l->data;
			if (data->key == k && data->length == len && GLOBALTEST(arena, data->global))
				data->SetData(pid, cp);
		}
		if (len > 0) cp += len;
	}

	pd->UnlockPlayer(pid);
}


void *DBThread(void *dummy)
{
	DBMessage *msg;
	int i, stop = 0;

	while (!stop)
	{
		/* get next command */
		msg = MPRemove(&dbq);
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

			case DB_QUIT: stop = 1; break;
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
	DB *db = dbopen(name, O_CREAT|O_RDWR, 0644, DB_HASH, NULL);
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
		char fname[PATH_MAX];
		const char *template = NULL;

		if (cfg)
			template = cfg->GetStr(GLOBAL, "Scores", "ScoreFile");

		if (template == NULL)
			template = "arenas/%s/scores.db";

		sprintf(fname, template, arenas[arena].name);

		/* lock here */
		pthread_mutex_lock(&dbmtx);

		if (databases[arena])
		{
			lm->Log(L_ERROR, "<persist> {%s} Score database already exists for new arena", arenas[arena].name);
			CloseDB(databases[arena]);
		}

		db = OpenDB(fname);

		if (!db)
		{
			/* this db doesn't exist, use default */
			db = defarenadb;
		}

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
			lm->Log(L_ERROR, "<persist> {%s} Score database doesn't exist for closing arena", arenas[arena].name);

		databases[arena] = NULL;

		pthread_mutex_unlock(&dbmtx);
	}
}


void RegPersistantData(PersistantData const *pd)
{
	pthread_mutex_lock(&dbmtx);
	if (pd->length > 0 && pd->length <= MAXPERSISTLENGTH)
		LLAdd(&ddlist, (void*)pd);
	pthread_mutex_unlock(&dbmtx);
}


void UnregPersistantData(PersistantData const *pd)
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

	lm->Log(L_INFO, "<persist> Collecting all persistant data and syncing to disk");

	return 1;
}

