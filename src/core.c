
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

#include "packets/login.h"

#include "packets/loginresp.h"


/* PROTOTYPES */

/* packet funcs */
local void PLogin(int, byte *, int);
local void PLeaving(int, byte *, int);

local void SendLoginResponse(int, AuthData *);

local int SendKeepalive(void *);

/* default auth, can be replaced */
local void DefaultAuth(int, struct LoginPacket *);



/* GLOBALS */

local PlayerData *players;
local Imainloop *ml;
local Iconfig *cfg;
local Inet *net;
local Imodman *mm;
local Ilogman *log;

local PlayerData *players;

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

		/* set up callbacks */
		net->AddPacket(C2S_LOGIN, PLogin);
		net->AddPacket(C2S_LEAVING, PLeaving);

		/* register default interfaces which may be replaced later */
		mm->RegisterInterface(I_AUTH, &_iauth);
		mm->RegisterInterface(I_ASSIGNFREQ, &_iaf);

		/* set up periodic events */
		ml->SetTimer(SendKeepalive, 500, 500, NULL);

	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregisterInterface(&_iaf);
		mm->UnregisterInterface(&_iauth);
		net->RemovePacket(C2S_LOGIN, PLogin);
		net->RemovePacket(C2S_LEAVING, PLeaving);
	}
	else if (action == MM_DESCRIBE)
	{
		mm->desc = "core - handles core game packets, including logins";
	}
	return MM_OK;
}



void PLogin(int pid, byte *p, int l)
{
	if (l != sizeof(struct LoginPacket))
		log->Log(LOG_BADDATA,"Bad packet length (%s)",players[pid].name);
	else
		((Iauth*)mm->GetInterface(I_AUTH))->Authenticate
			(pid, (struct LoginPacket *)p, SendLoginResponse);
}


void SendLoginResponse(int pid, AuthData *auth)
{
	struct LoginResponse lr =
		{ S2C_LOGINRESPONSE, 0, 134, 0, EXECHECKSUM, {0, 0},
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
	net->SendToOne(pid, (char*)&lr, sizeof(struct LoginResponse), NET_RELIABLE);

	log->Log(LOG_INFO, "Player logged on (%s)", players[pid].name);
}


void DefaultAuth(int pid, struct LoginPacket *p)
{
	AuthData auth;

	auth.demodata = 0;
	auth.code = AUTH_OK;
	astrncpy(auth.name, p->name, 24);
	auth.squad[0] = 0;

	SendLoginResponse(pid, &auth);
}


int DefaultAssignFreq(int pid, int freq, byte ship)
{
	return freq;
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


#define KEEPALIVESECONDS 5

int SendKeepalive(void *q)
{
	byte keepalive = S2C_KEEPALIVE;
	net->SendToAll(&keepalive, 1, NET_UNRELIABLE);
	return 1;
}


