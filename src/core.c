
#include <zlib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include "asss.h"



#define EXECHECKSUM 0xF1429CE8


/* STRUCTS */

#include "packets/logon.h"

#include "packets/logonresp.h"

#include "packets/goarena.h"


/* PROTOTYPES */

/* packet funcs */
local void PLogin(int, byte *, int);
local void PArena(int, byte *, int);
local void PLeaving(int, byte *, int);

local void SendLogonResponse(int, AuthData *);

/* timers */
local int SendKeepalive(void *);
local int ReapArenas(void *);

/* extras to help PArena */
local int AssignArena(struct GoArenaPacket *);
local int DefaultAssignFreq(int, int, byte);

/* arena management funcs */
local int FindArena(char *);
local int CreateArena(char *);
local void FreeArena(int);

/* default auth, can be replaced */
local void DefaultAuth(int, struct LogonPacket *);



/* GLOBALS */

local PlayerData *players;
local Imainloop *ml;
local Iconfig *cfg;
local Inet *net;
local Imodman *mm;
local Ilogman *log;

local PlayerData *players;
local ArenaData *arenas[MAXARENA];

local Icore _icore = { arenas, SendLogonResponse };
local Iassignfreq _iaf = { DefaultAssignFreq };
local Iauth _iauth = { DefaultAuth };


/* FUNCTIONS */

int MM_core(int action, Imodman *mm_)
{
	if (action == MM_LOAD)
	{
		/* get interface pointers */
		mm = mm_;
		net = mm->GetInterface(I_NET);
		log = mm->GetInterface(I_LOGMAN);
		cfg = mm->GetInterface(I_CONFIG);
		ml = mm->GetInterface(I_MAINLOOP);
		players = mm->players;

		if (!net || !cfg || !log || !ml) return MM_FAIL;

		/* zero out arenas */
		memset(arenas,0,sizeof(arenas));

		/* set up callbacks */
		net->AddPacket(C2S_LOGON, PLogin);
		net->AddPacket(C2S_GOTOARENA, PArena);
		net->AddPacket(C2S_LEAVING, PLeaving);

		/* register default interfaces which may be replaced later */
		mm->RegisterInterface(I_AUTH, &_iauth);
		mm->RegisterInterface(I_ASSIGNFREQ, &_iaf);

		/* set up periodic events */
		ml->SetTimer(SendKeepalive, 500, 500, NULL);
		ml->SetTimer(ReapArenas, 1000, 6000, NULL);

		/* register the main interface */
		mm->RegisterInterface(I_CORE, &_icore);
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregisterInterface(&_icore);
		mm->UnregisterInterface(&_iaf);
		mm->UnregisterInterface(&_iauth);
		net->RemovePacket(C2S_LOGON, PLogin);
		net->RemovePacket(C2S_GOTOARENA, PArena);
		net->RemovePacket(C2S_LEAVING, PLeaving);
	}
	else if (action == MM_DESCRIBE)
	{
		mm->desc = "core - handles core game packets, including logons, arenas";
	}
	return MM_OK;
}



void PLogin(int pid, byte *p, int l)
{
	if (l != sizeof(struct LogonPacket))
		log->Log(LOG_BADDATA,"Bad packet length (%s)",players[pid].name);
	else
		((Iauth*)mm->GetInterface(I_AUTH))->Authenticate
			(pid, (struct LogonPacket *)p, SendLogonResponse);
}


void SendLogonResponse(int pid, AuthData *auth)
{
	struct LogonResponse lr =
		{ S2C_LOGONRESPONSE, 0, 134, 0, EXECHECKSUM, {0, 0},
			0, 0x281CC948, 0, {0, 0, 0, 0, 0, 0, 0, 0} };

	lr.code = auth->code;
	lr.demodata = auth->demodata;
	lr.newschecksum = newschecksum; /* FIXME NOW: get mapnewsdl interface and use GetNewsChecksum */

	/* set up player struct */
	memset(players + pid, 0, sizeof(PlayerData));
	players[pid].type = S2C_PLAYERENTERING; /* restore type */
	strncpy(players[pid].sendname, auth->name, 20);
	astrncpy(players[pid].name, auth->name, 21);
	strncpy(players[pid].sendsquad, auth->squad, 20);
	astrncpy(players[pid].squad, auth->squad, 21);
	players[pid].attachedto = -1;
	players[pid].pid = pid;
	players[pid].shiptype = SPEC;
	players[pid].arena = -1;

	/* send response */
	net->SendToOne(pid, (char*)&lr, sizeof(struct LogonResponse), NET_RELIABLE);

	log->Log(LOG_INFO, "Player logged on (%s)", players[pid].name);
}


void DefaultAuth(int pid, struct LogonPacket *p)
{
	AuthData auth;

	auth.demodata = 0;
	auth.code = AUTH_OK;
	astrncpy(auth.name, p->name, 24);
	auth.squad[0] = 0;

	SendLogonResponse(pid, &auth);
}


int DefaultAssignFreq(int pid, int freq, byte ship)
{
	return freq;
}


void PArena(int pid, byte *p, int l)
{
	if (l != sizeof(struct GoArenaPacket))
		log->Log(LOG_BADDATA,"Bad packet length (%s)",players[pid].name);
	else
	{
		struct GoArenaPacket *pk = (struct GoArenaPacket*)p;
		struct SimplePacket whoami = {S2C_WHOAMI, pid, 0, 0, 0, 0};
		int i, arena;
		byte misc;

		/* check shiptype */
		if (pk->shiptype > SPEC || pk->shiptype < 0)
		{
			log->Log(LOG_BADDATA,"Bad ship number: %i (%s)",
					pk->shiptype, players[pid].name);
			return;
		}

		/* send whoami packet */
		net->SendToOne(pid, (byte*)&whoami, 3, NET_RELIABLE);

		/* set up info */
		players[pid].shiptype = pk->shiptype;
		players[pid].xres = pk->xres;
		players[pid].yres = pk->yres;
		players[pid].arena = arena = AssignArena(pk);
		players[pid].freq = ((Iassignfreq*)mm->GetInterface(I_ASSIGNFREQ))->
			AssignFreq(pid, BADFREQ, pk->shiptype);

		/* send settings */
		net->SendToOne(pid, arenas[arena]->settings, SETTINGSIZE, NET_RELIABLE);

		/* send player list */
		for (i = 0; i < MAXPLAYERS; i++)
			if (	net->GetStatus(i) == S_CONNECTED &&
					players[i].arena == arena)
				net->SendToOne(pid, (byte*)(players+i), 64, NET_RELIABLE);

		/* send brick clear and finisher */
		net->SendToOne(pid, (byte*)&arenas[arena]->map,
				sizeof(struct MapFilename), NET_RELIABLE);
		misc = S2C_BRICK;
		net->SendToOne(pid, &misc, 1, NET_RELIABLE);
		misc = 0x02;
		net->SendToOne(pid, &misc, 1, NET_RELIABLE);

		/* alert others */
		net->SendToArena(players[pid].arena, pid,
				(byte*)(players+pid), 64, NET_RELIABLE);

		log->Log(LOG_INFO, "Player entering arena '%s' (%s)",
				arenas[arena]->name, players[pid].name);
	}
}


int AssignArena(struct GoArenaPacket *p)
{
	char _buf[2] = {'0', 0}, *name = _buf;
	int arena;

	if (p->arenatype == -3)
		name = p->arenaname;
	else if (p->arenatype >= 0 && p->arenatype <= 9)
		name[0] = '0' + p->arenatype;

	if ((arena = FindArena(name)) == -1)
		if ((arena = CreateArena(name)) == -1)
			arena = 0;

	return arena;
}


void PLeaving(int pid, byte *p, int q)
{
	struct SimplePacket pk = { S2C_PLAYERLEAVING, pid, 0, 0, 0, 0 };
	net->SendToArena(players[pid].arena, pid, (byte*)&pk, 3, NET_RELIABLE);
	players[pid].arena = -1;
}


void PMapRequest(int pid, byte *p, int q)
{
	int arena = players[pid].arena;
	if (p[0] == C2S_MAPREQUEST)
	{
		log->Log(LOG_DEBUG,"Sending map to %s", players[pid].name);
		if (arena < 0)
		{
			log->Log(LOG_BADDATA, "Map request before entering arena (%s)",
					players[pid].name);
			return;
		}
		net->SendToOne(pid, arenas[arena]->mapdata,
			arenas[arena]->mapdatalen, NET_RELIABLE | NET_PRESIZE);
	}
	else if (p[0] == C2S_NEWSREQUEST)
	{
		log->Log(LOG_DEBUG,"Sending news to %s", players[pid].name);
		net->SendToOne(pid, newsdata, newsdatasize, NET_RELIABLE | NET_PRESIZE);
	}
}


#define KEEPALIVESECONDS 5

int SendKeepalive(void *q)
{
	byte keepalive = S2C_KEEPALIVE;
	net->SendToAll(&keepalive, 1, NET_UNRELIABLE);
	return 1;
}


#define REAPARENASECONDS 30

int ReapArenas(void *q)
{
	int i, j, count;

	for (i = 0; i < MAXARENA; i++)
		if (arenas[i])
		{
			count = 0;
			for (j = 0; j < MAXPLAYERS; j++)
				if (	net->GetStatus(j) == S_CONNECTED &&
						players[j].arena == i)
					count++;
			if (count == 0)
			{
				log->Log(LOG_USELESSINFO, "Arena %s (%i) being reaped",
						arenas[i]->name, i);
				FreeArena(i);
			}
		}
	return 1;
}


int CreateArena(char *name)
{
	FILE *f;
    struct stat st;
	int i = 0, mapfd, fsize;
    byte *map, *cmap;
	ConfigHandle config;
	uLong csize;
	ArenaData *me = amalloc(sizeof(ArenaData));
	char fname[64];

	/* find a slot */
	while (arenas[i] && i < MAXARENA) i++;
	if (i == MAXARENA)
	{
		log->Log(LOG_ERROR,"Too many arenas!!! Cannot create arena %s", name);
		return -1;
	}

	/* fill in simple data */
	strcpy(me->name, name);

	/* try to open config file */
	snprintf(fname, 64, "arena-%s", name);
	config = cfg->OpenConfigFile(fname);
	/* if not, try default config */
	if (!config)
	{
		log->Log(LOG_USELESSINFO,"Config file '%s' not found, using default", fname);
		config = cfg->OpenConfigFile("arena-default");
	}
	if (!config)
	{	/* if not, fail */
		log->Log(LOG_ERROR,"Default config file not found");
		free(me); return -1;
	}
	me->config = config;

	arenas[i] = me;
	log->Log(LOG_USELESSINFO,"Arena %s (%i) created sucessfully", name, i);
	return i;
}


void FreeArena(int i)
{
	cfg->CloseConfigFile(arenas[i]->config);
	free(arenas[i]->mapdata);
	free(arenas[i]);
	arenas[i] = NULL;
}


int FindArena(char *name)
{
	int i;
	for (i = 0; i < MAXARENA; i++)
		if (arenas[i] && !strcasecmp(arenas[i]->name, name))
			return i;
	return -1;
}



