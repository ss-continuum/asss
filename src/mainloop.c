

#include "asss.h"



typedef struct TimerData
{
	TimerFunc func;
	unsigned int interval, when;
	void *param;
} TimerData;



local void SetTimer(TimerFunc, int, int, void *);
local void ClearTimer(TimerFunc);

local void AddMainLoop(MainLoopFunc);
local void RemoveMainLoop(MainLoopFunc);
local void RunLoop();
local void KillML();



local Imainloop _int =
{
	AddMainLoop, RemoveMainLoop, SetTimer, ClearTimer, RunLoop, KillML
};

local int privatequit;
local LinkedList *funcs, *timers;


int MM_mainloop(int action, Imodman *mm)
{
	if (action == MM_LOAD)
	{
		privatequit = 0;
		funcs = LLAlloc();
		timers = LLAlloc();
		mm->RegisterInterface(I_MAINLOOP, &_int);
	}
	else if (action == MM_UNLOAD)
	{
		LLFree(funcs);
		LLFree(timers);
		mm->UnregisterInterface(&_int);
	}
	else if (action == MM_DESCRIBE)
	{
		mm->desc = "mainloop - provides an interface for modules to insert "
				   "events into the main server loop";
	}
	return MM_OK;
}


void AddMainLoop(MainLoopFunc f)
{
	LLAdd(funcs, f);
}

void RemoveMainLoop(MainLoopFunc f)
{
	LLRemove(funcs, f);
}

void RunLoop()
{
	MainLoopFunc f;
	TimerData *td;
	unsigned int gtc;

	while (!privatequit)
	{
		/* call all funcs */
		LLRewind(funcs);
		while ((f = LLNext(funcs)))
			f();

		gtc = GTC();

		/* do timers */
		LLRewind(timers);
		while ((td = LLNext(timers)))
			if (td->func && td->when <= gtc)
			{
				if ( td->func(td->param) )
					td->when = gtc + td->interval;
				else
					LLRemove(timers, td);
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
	TimerData *td;
	
	LLRewind(timers);
	while ((td = LLNext(timers)))
		if (td->func == f)
			LLRemove(timers, td);
}



