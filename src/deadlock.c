
/* dist: public */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "asss.h"

local pthread_t thd;
local volatile int counter;

local void * thread_check(void *dummy)
{
	for (;;)
	{
		int seen = counter;
		sleep(10);
		if (counter == seen)
		{
			fprintf(stderr, "E <deadlock> deadlock detected, aborting\n");
			abort();
		}
	}
}

local void increment(void)
{
	counter++;
}

EXPORT int MM_deadlock(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		pthread_create(&thd, NULL, thread_check, NULL);
		mm->RegCallback(CB_MAINLOOP, increment, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CB_MAINLOOP, increment, ALLARENAS);
		pthread_cancel(thd);
		pthread_join(thd, NULL);
		return MM_OK;
	}
	return MM_FAIL;
}

