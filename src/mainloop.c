
#ifndef WIN32
#include <unistd.h>
#endif

#include "asss.h"



typedef struct TimerData
{
	TimerFunc func;
	unsigned int interval, when;
	void *param;
} TimerData;



local void StartTimer(TimerFunc, int, int, void *);
local void ClearTimer(TimerFunc);

local void RunLoop(void);
local void KillML(void);



local Imainloop _int =
{
	INTERFACE_HEAD_INIT(I_MAINLOOP, "mainloop")
	StartTimer, ClearTimer, RunLoop, KillML
};

local int privatequit;
local LinkedList *timers;
local Imodman *mm;


EXPORT int MM_mainloop(int action, Imodman *mm_, int arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		privatequit = 0;
		timers = LLAlloc();
		mm->RegInterface(&_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		LLFree(timers);
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;
		return MM_OK;
	}
	return MM_FAIL;
}


void RunLoop(void)
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
		for (l = LLGetHead(timers); l; l = l->next)
		{
			td = (TimerData*) l->data;
			if (td->func && td->when <= gtc)
			{
				if ( td->func(td->param) )
					td->when = gtc + td->interval;
				else
					LLAdd(&freelist, td);
			}
		}

		/* free timers */
		for (l = LLGetHead(&freelist); l; l = l->next)
		{
			LLRemove(timers, l->data);
			afree(l->data);
		}
		LLEmpty(&freelist);

		/* rest a bit */
		sched_yield();
		usleep(10000); /* 1/100 sec */
	}
}


void KillML(void)
{
	privatequit = 1;
}


void StartTimer(TimerFunc f, int startint, int interval, void *param)
{
	TimerData *data = amalloc(sizeof(TimerData));

	data->func = f;
	data->interval = interval;
	data->when = GTC() + startint;
	data->param = param;
	LLAdd(timers, data);
}


void ClearTimer(TimerFunc f)
{
	Link *l;

	for (l = LLGetHead(timers); l; l = l->next)
		if (((TimerData*)l->data)->func == f)
		{
			LLRemove(timers, l->data);
			afree(l->data);
			return;
		}
}



