
/* information on the directory server protocol was obtained from
 * Hammuravi's page at
 * http://www4.ncsu.edu/~rniyenga/subspace/old/dprotocol.html
 */


#ifndef WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#else
#include <winsock.h>
#define close(s) closesocket(s)
#endif

#include <string.h>

#include "asss.h"


/* the thing to send */
struct S2DInfo
{
	u32 ip;
	u16 port;
	u16 players;
	u16 scoresp;
	u32 version;
	char servername[32];
	char password[48];
	char description[386]; /* fill out to 480 bytes */
};



/* interface pointers */
local Iconfig *cfg;
local Imainloop *ml;
local Iplayerdata *pd;
local Ilogman *lm;

local LinkedList servers;
local struct S2DInfo data;
local int sock;


local int SendUpdates(void *dummy)
{
	int n, count = 0;
	Link *l;

	lm->Log(L_DRIVEL, "<directory> Sending information to directory servers");

	/* figure out player count */
	pd->LockStatus();
	for (n = 0; n < MAXPLAYERS; n++)
		if (pd->players[n].status == S_PLAYING &&
		    pd->players[n].type != T_FAKE)
			count++;
	pd->UnlockStatus();

	data.players = count;

	n = sizeof(data) - sizeof(data.description) + strlen(data.description) + 1;

	for (l = LLGetHead(&servers); l; l = l->next)
	{
		struct sockaddr_in *sin = l->data;
		sendto(sock, (byte*)&data, n, 0, (const struct sockaddr *)sin, n);
	}

	return TRUE;
}


local void init_data()
{
	const char *t;

	memset(&data, 0, sizeof(data));
	data.ip = 0;
	data.port = cfg->GetInt(GLOBAL, "Net", "Port", 5000);
	data.players = 0; /* fill in later */;
	data.scoresp = 1; /* always keep scores */
	data.version = ASSSVERSION_NUM;
	data.version = 134; /* priit's updated dirserv require this */
	if ((t = cfg->GetStr(GLOBAL, "Directory", "Name")))
		astrncpy(data.servername, t, sizeof(data.servername));
	else
		astrncpy(data.servername, "<no name provided>", sizeof(data.servername));
	if ((t = cfg->GetStr(GLOBAL, "Directory", "Password")))
		astrncpy(data.password, t, sizeof(data.password));
	else
		astrncpy(data.password, "cane", sizeof(data.password));
	if ((t = cfg->GetStr(GLOBAL, "Directory", "Description")))
		astrncpy(data.description, t, sizeof(data.description));
	else
		astrncpy(data.description, "<no description provided>", sizeof(data.description));
	lm->Log(L_DRIVEL, "<directory> Server name: %s", data.servername);
}


local void init_servers()
{
	char skey[] = "Server#", pkey[] = "Port#";
	unsigned short i, defport, port;

	LLInit(&servers);

	defport = cfg->GetInt(GLOBAL, "Directory", "Port", 4991);

	for (i = 1; i < 10; i++)
	{
		const char *name;

		skey[6] = '0' + i;
		pkey[4] = '0' + i;
		name = cfg->GetStr(GLOBAL, "Directory", skey);
		port = cfg->GetInt(GLOBAL, "Directory", pkey, defport);

		if (name)
		{
			struct sockaddr_in *sin;
			struct hostent *ent = gethostbyname(name);
			if (ent && ent->h_length == sizeof(sin->sin_addr))
			{
				sin = amalloc(sizeof(*sin));
				sin->sin_family = AF_INET;
				sin->sin_port = htons(port);
				memcpy(&sin->sin_addr, ent->h_addr, sizeof(sin->sin_addr));
				LLAdd(&servers, sin);
				lm->Log(L_INFO, "<directory> Using '%s' at %s as a directory server",
						ent->h_name, inet_ntoa(sin->sin_addr));
			}
		}
	}
}


local void deinit_servers()
{
	Link *l;
	for (l = LLGetHead(&servers); l; l = l->next)
		afree(l->data);
	LLEmpty(&servers);
}



EXPORT int MM_directory(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);

		if (!cfg || !ml || !pd || !lm)
			return MM_FAIL;

		sock = socket(PF_INET, SOCK_DGRAM, 0);
		if (sock == -1)
			return MM_FAIL;

		init_data();
		init_servers();

		ml->SetTimer(SendUpdates, 1000, 6000, NULL);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		ml->ClearTimer(SendUpdates);
		deinit_servers();
		close(sock);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(lm);
		return MM_OK;
	}
	return MM_FAIL;
}

