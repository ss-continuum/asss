
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

local void SendLoginResponse(int, AuthData *);

local int SendKeepalive(void *);

/* default auth, can be replaced */
local int DefaultAuth(int, struct LoginPacket *, void (*)(int, AuthData *));
local int DefaultAssignFreq(int, int, byte);


/* GLOBALS */

local PlayerData *players;
local Imainloop *ml;
local Iconfig *cfg;
local Inet *net;
local Imodman *mm;
local Ilogman *log;
local Imapnewsdl *map;
local Iauth *auth;

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
		mm->RegInterest(I_NET, &net);
		mm->RegInterest(I_LOGMAN, &log);
		mm->RegInterest(I_CONFIG, &cfg);
		mm->RegInterest(I_MAINLOOP, &ml);
		mm->RegInterest(I_MAPNEWSDL, &map);
		mm->RegInterest(I_AUTH, &auth);
		players = mm->players;

		if (!net || !ml) return MM_FAIL;

		/* set up callbacks */
		net->AddPacket(C2S_LOGIN, PLogin);

		/* register default interfaces which may be replaced later */
		mm->RegInterface(I_AUTH, &_iauth);
		mm->RegInterface(I_ASSIGNFREQ, &_iaf);

		/* set up periodic events */
		ml->SetTimer(SendKeepalive, 500, 500, NULL);
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterface(I_ASSIGNFREQ, &_iaf);
		mm->UnregInterface(I_AUTH, &_iauth);
		net->RemovePacket(C2S_LOGIN, PLogin);
		mm->UnregInterest(I_NET, &net);
		mm->UnregInterest(I_LOGMAN, &log);
		mm->UnregInterest(I_CONFIG, &cfg);
		mm->UnregInterest(I_MAINLOOP, &ml);
		mm->UnregInterest(I_MAPNEWSDL, &map);
		mm->UnregInterest(I_AUTH, &auth);
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
		log->Log(LOG_BADDATA,"Bad login packet length (%s)",players[pid].name);
	else
		auth->Authenticate
			(pid, (struct LoginPacket *)p, SendLoginResponse);
}


void SendLoginResponse(int pid, AuthData *auth)
{
	struct LoginResponse lr =
		{ S2C_LOGINRESPONSE, 0, 134, 0, EXECHECKSUM, {0, 0},
			0, 0x281CC948, 0, {0, 0, 0, 0, 0, 0, 0, 0} };

	lr.code = auth->code;
	lr.demodata = auth->demodata;
	lr.newschecksum = map->GetNewsChecksum();

	/* set up player struct */
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


int DefaultAuth(int pid, struct LoginPacket *p, void (*SendLoginResponse)(int, AuthData *))
{
	AuthData auth;

	auth.demodata = 0;
	auth.code = AUTH_OK;
	astrncpy(auth.name, p->name, 24);
	auth.squad[0] = 0;

	SendLoginResponse(pid, &auth);
	return 0;
}


int DefaultAssignFreq(int pid, int freq, byte ship)
{
	return freq;
}



#define KEEPALIVESECONDS 5

int SendKeepalive(void *q)
{
	byte keepalive = S2C_KEEPALIVE;
	net->SendToAll(&keepalive, 1, NET_UNRELIABLE);
	return 1;
}


