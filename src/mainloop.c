

#include "asss.h"



typedef struct TimerData
{
	TimerFunc func;
	unsigned int interval, when;
	void *param;
} TimerData;



local void SetTimer(TimerFunc, int, int, void *);
local void ClearTimer(TimerFunc);

local void RunLoop();
local void KillML();



local Imainloop _int =
{
	SetTimer, ClearTimer, RunLoop, KillML
};

local int privatequit;
local LinkedList *timers;
local Imodman *mm;


int MM_mainloop(int action, Imodman *mm_)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		privatequit = 0;
		timers = LLAlloc();
		mm->RegInterface(I_MAINLOOP, &_int);
	}
	else if (action == MM_UNLOAD)
	{
		LLFree(timers);
		mm->UnregInterface(&_int);
	}
	else if (action == MM_DESCRIBE)
	{
		mm->desc = "mainloop - provides an interface for modules to insert "
				   "events into the main server loop";
	}
	return MM_OK;
}


void RunLoop()
{
	TimerData *td;
	LinkedList *lst;
	Link *l;
	unsigned int gtc;

	while (!privatequit)
	{
		/* call all funcs */
		lst = mm->LookupCallback(CALLBACK_MAINLOOP);
		for (l = LLGetHead(lst); l; l = l->next)
			((MainLoopFunc)l->data)();
		mm->FreeLookupResult(lst);

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
					LLRemove(timers, td);
			}
		}
	}
}

void KillML()
{
	privatequit = 1;
}


void SetTimer(TimerFunc f, int startint, int interval, void *param)
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
			LLRemove(timers, l->data);
}



