
#include <stdarg.h>
#include <stdio.h>

#include "asss.h"



local void Log(int, char *, ...);



local Imodman *mm;
local Ilogman _int = { Log };


int MM_logman(int action, Imodman *mm_)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		mm->RegisterInterface(I_LOGMAN, &_int);
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregisterInterface(&_int);
	}
	else if (action == MM_DESCRIBE)
	{
		mm->desc = "logman - formats log entries and passes them to a managed "
					"set of logging modules";
	}
	return MM_OK;
}


void Log(int level, char *format, ...)
{
	LinkedList *lst;
	Link *l;
	va_list argptr;
	static char buf[1024];
	
	va_start(argptr, format);
	vsprintf(buf, format, argptr);
	va_end(argptr);

	lst = mm->LookupGenCallback(CALLBACK_LOGFUNC);
	for (l = LLGetHead(lst); l; l = l->next)
		((LogFunc)(l->data))(level, buf);
	mm->FreeLookupResult(lst);
}



