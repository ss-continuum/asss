
/* dist: public */

#include <string.h>

#include "asss.h"


local Imodman *mm;
local Iauth *oldauth;
local Icapman *capman;
local char prefix[MAXPLAYERS];
local void (*CachedDone)(int pid, AuthData *data);


local void MyDone(int pid, AuthData *data)
{
	AuthData mydata;

	memcpy(&mydata, data, sizeof(mydata));
	if (prefix[pid])
	{
		/* we have a prefix. check for the appropriate capability */
		char cap[] = "prefix_@";
		strchr(cap, '@')[0] = prefix[pid];
		if (capman->HasCapabilityByName(mydata.name, cap))
		{
			/* only add back the letter if he has the capability, and
			 * only add it to the sendname. */
			memmove(mydata.sendname+1, mydata.sendname, 19);
			mydata.sendname[0] = prefix[pid];
		}
	}

	CachedDone(pid, &mydata);
}


local void Authenticate(int pid, struct LoginPacket *lp, int lplen,
		void (*Done)(int pid, AuthData *data))
{
	struct LoginPacket mylp;

	/* save Done to call later */
	CachedDone = Done;

	/* construct new login packet with prefix removed, if any */
	memcpy(&mylp, lp, lplen);
	if (strchr("+->@#$", mylp.name[0]))
	{
		prefix[pid] = mylp.name[0];
		memmove(mylp.name, mylp.name + 1, 31);
	}
	else
		prefix[pid] = 0;

	/* call it */
	oldauth->Authenticate(pid, &mylp, lplen, MyDone);
}


local Iauth myauth =
{
	INTERFACE_HEAD_INIT_PRI(I_AUTH, "auth-prefix", 10)
	Authenticate
};


EXPORT int MM_auth_prefix(int action, Imodman *_mm, int arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;
		oldauth = mm->GetInterface(I_AUTH, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);
		if (!oldauth || !capman) /* need another auth to layer on top of */
			return MM_FAIL;
		mm->RegInterface(&myauth, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&myauth, ALLARENAS))
			return MM_FAIL;
		mm->ReleaseInterface(oldauth);
		mm->ReleaseInterface(capman);
		return MM_OK;
	}
	return MM_FAIL;
}

