

#include "asss.h"


int MM_arenaman(int action, Imodman *mm_)
{

}


void CallAA(int action, int arena)
{
	LinkedList *funcs;
	Link *l;

	funcs = mm->LookupGenCallback(CALLBACK_ARENAACTION);

	for (l = GetHead(funcs); l; l = l->next)
		((ArenaActionFunc)l->data)(action, arena);

	LLFree(funcs);
}


