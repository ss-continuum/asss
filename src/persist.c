
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <db1/db.h>
#include <assert.h>

#include "asss.h"


/* defines */

typedef enum db_command
{
	DB_NULL,
	DB_GET,
	DB_PUT,
	DB_SYNCWAIT,
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
local void ScoreAA(int arena, int action);
local DB *OpenDB(char *fname);


/* private data */

local Imodman *mm;
local Ilogman *log;
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
local DB *globaldb;

local int cfg_syncseconds;

local Ipersist _myint =
{
	RegPersistantData, UnregPersistantData,
	SyncToFile, SyncFromFile, StabilizeScores
};



int MM_persist(int action, Imodman *_mm, int arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;
		mm->RegInterest(I_PLAYERDATA, &pd);
		mm->RegInterest(I_LOGMAN, &log);
		mm->RegInterest(I_CONFIG, &cfg);
		mm->RegInterest(I_ARENAMAN, &aman);
		mm->RegInterest(I_MAINLOOP, &ml);

		arenas = aman->data;

		mm->RegCallback(CALLBACK_ARENAACTION, ScoreAA, ALLARENAS);

		LLInit(&ddlist);
		MPInit(&dbq);
		pthread_create(&dbthread, NULL, DBThread, NULL);

		globaldb = OpenDB("global.db");

		mm->RegInterface(I_PERSIST, &_myint);

		cfg_syncseconds = cfg ?
				cfg->GetInt(GLOBAL, "Persist", "SyncSeconds", 60) : 60;

		ml->SetTimer(SyncTimer, 12000, cfg_syncseconds * 100, NULL);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		ml->ClearTimer(SyncTimer);
		mm->UnregInterface(I_PERSIST, &_myint);
		mm->UnregCallback(CALLBACK_ARENAACTION, ScoreAA, ALLARENAS);
		mm->UnregInterest(I_PLAYERDATA, &pd);
		mm->UnregInterest(I_LOGMAN, &log);
		mm->UnregInterest(I_CONFIG, &cfg);
		mm->UnregInterest(I_ARENAMAN, &aman);

		{
			DBMessage *msg = amalloc(sizeof(DBMessage));
			msg->command = DB_QUIT;
			MPAdd(&dbq, msg);
		}
		pthread_join(dbthread, NULL);
		MPDestroy(&dbq);
		LLEmpty(&ddlist);

		globaldb->close(globaldb);

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
		log->Log(LOG_ERROR, "persist: Database not open for arena %i", arena);
	else /* do it! */ 
		if (db->put(db, &key, &val, 0) == -1)
			log->Log(LOG_ERROR, "persist: Error entering key in database");
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

	if (db->get(db, &key, &val, 0) == -1)
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

			case DB_GET:  DoGet(msg->pid, msg->data); break;

			case DB_PUT:  DoPut(msg->pid, msg->data); break;

			case DB_SYNCWAIT:
				for (i = 0; i < MAXARENA; i++)
					if (databases[i])
						databases[i]->sync(databases[i], 0);
				globaldb->sync(globaldb, 0);
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

	return NULL;
}


DB *OpenDB(char *name)
{
	return dbopen(name, O_CREAT|O_RDWR, 0644, DB_HASH, NULL);
}


void ScoreAA(int arena, int action)
{
	DB *db;
	if (action == AA_CREATE)
	{
		char fname[PATH_MAX], *template = NULL;

		if (cfg)
			template = cfg->GetStr(GLOBAL, "Scores", "ScoreFile");

		if (template == NULL)
			template = "arenas/%s/scores.db";

		sprintf(fname, template, arenas[arena].name);

		/* lock here */
		pthread_mutex_lock(&dbmtx);

		if (databases[arena])
		{
			DB *db = databases[arena];
			log->Log(LOG_ERROR, "persist: Score database already exists for new arena '%s'", arenas[arena].name);
			db->close(db);
		}

		log->Log(LOG_DEBUG, "Opening db '%s'", fname);
		db = OpenDB(fname);

		if (!db)
		{
			/* this db doesn't exist, try default */
			sprintf(fname, template, "default");
			log->Log(LOG_DEBUG, "Opening db '%s'", fname);
			db = OpenDB(fname);

			if (!db)
				log->Log(LOG_ERROR, "persist: Error opening scores database '%s'", fname);
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
		{
			log->Log(LOG_DEBUG, "Closing db");
			db->close(db);
		}
		else
		{
			log->Log(LOG_ERROR, "persist: Score database doesn't exist for closing arena '%s'", arenas[arena].name);
		}

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
	StabilizeScores(0);
	return 1;
}

