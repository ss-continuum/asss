
#ifndef WIN32
#include <unistd.h>
#endif

#include "asss.h"



typedef struct TimerData
{
	TimerFunc func;
	unsigned int interval, when;
	void *param;
	int key;
} TimerData;



local void StartTimer(TimerFunc, int, int, void *, int);
local void ClearTimer(TimerFunc, int);
local void CleanupTimer(TimerFunc func, int key, CleanupFunc cleanup);

local int RunLoop(void);
local void KillML(int code);



local Imainloop _int =
{
	INTERFACE_HEAD_INIT(I_MAINLOOP, "mainloop")
	StartTimer, ClearTimer, CleanupTimer, RunLoop, KillML
};

local int privatequit;
local LinkedList timers;
local Imodman *mm;

local pthread_mutex_t tmrmtx = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&tmrmtx)
#define UNLOCK() pthread_mutex_unlock(&tmrmtx)


EXPORT int MM_mainloop(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		privatequit = 0;
		LLInit(&timers);
		mm->RegInterface(&_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		LLEmpty(&timers);
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;
		return MM_OK;
	}
	return MM_FAIL;
}


int RunLoop(void)
{
	TimerData *td;
	LinkedList freelist;
	Link *l;
	unsigned int gtc;

	LLInit(&freelist);

	while (!privatequit)
	{
		/* call all funcs */
		DO_CBS(CB_MAINLOOP, ALLARENAS, MainLoopFunc, ());

		gtc = GTC();

		/* do timers */
		LOCK();
		for (l = LLGetHead(&timers); l; l = l->next)
		{
			td = (TimerData*) l->data;
			if (td->func && td->when <= gtc)
			{
				UNLOCK();
				if ( td->func(td->param) )
					td->when = gtc + td->interval;
				else
					LLAdd(&freelist, td);
				LOCK();
			}
		}

		/* free timers */
		for (l = LLGetHead(&freelist); l; l = l->next)
		{
			LLRemove(&timers, l->data);
			afree(l->data);
		}
		LLEmpty(&freelist);
		UNLOCK();

		/* rest a bit: 1/100 sec */
		usleep(10000);
	}

	return privatequit & 0xff;
}


void KillML(int code)
{
	privatequit = 0x100 | (code & 0xff);
}


void StartTimer(TimerFunc f, int startint, int interval, void *param, int key)
{
	TimerData *data = amalloc(sizeof(TimerData));

	data->func = f;
	data->interval = interval;
	data->when = GTC() + startint;
	data->param = param;
	data->key = key;
	LOCK();
	LLAdd(&timers, data);
	UNLOCK();
}


void CleanupTimer(TimerFunc func, int key, CleanupFunc cleanup)
{
	Link *l, *next;

	LOCK();
	for (l = LLGetHead(&timers); l; l = next)
	{
		TimerData *td = l->data;
		next = l->next;

		if (td->func == func && (td->key == key || key == -1))
		{
			if (cleanup)
				cleanup(td->param);
			LLRemove(&timers, td);
			afree(td);
		}
	}
	UNLOCK();
}

void ClearTimer(TimerFunc f, int key)
{
	CleanupTimer(f, key, NULL);
}

