
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "asss.h"
#include "ooputils.h"
#include "elderd.h"


/* prototypes */

local int ConnectToSchemeServer();
local void * SchemeThread(void *);
local int ProcessMessage(void *, int);

local void Cscm(const char *, int, int);

/* global data */

local Iplayerdata *pd;
local Ichat *chat;
local Ilogman *log;
local Icmdman *cmd;
local Inet *net;
local Iconfig *cfg;
local Iarenaman *aman;
local Imodman *mm;
local PlayerData *players;
local ArenaData *arenas;

local MPQueue toscm;

local int schemesock;


int MM_scheme(int action, Imodman *_mm, int arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;
		mm->RegInterest(I_PLAYERDATA, &pd);
		mm->RegInterest(I_CHAT, &chat);
		mm->RegInterest(I_LOGMAN, &log);
		mm->RegInterest(I_CMDMAN, &cmd);
		mm->RegInterest(I_NET, &net);
		mm->RegInterest(I_CONFIG, &cfg);
		mm->RegInterest(I_ARENAMAN, &aman);
		if (!cmd || !net || !cfg || !aman) return MM_FAIL;

		players = pd->players;
		arenas = aman->arenas;

		schemesock = ConnectToSchemeServer();
		if (schemesock == -1) return MM_FAIL;

		MPInit(&toscm);

		/* this thread handles all io on the schemesock */
		StartThread(SchemeThread, NULL);

		cmd->AddCommand("scm", Cscm, 100);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("scm", Cscm);

		mm->UnregInterest(I_PLAYERDATA, &pd);
		mm->UnregInterest(I_CHAT, &chat);
		mm->UnregInterest(I_LOGMAN, &log);
		mm->UnregInterest(I_CMDMAN, &cmd);
		mm->UnregInterest(I_NET, &net);
		mm->UnregInterest(I_CONFIG, &cfg);
		mm->UnregInterest(I_ARENAMAN, &aman);
		return MM_OK;
	}
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
	return MM_FAIL;
}


int ConnectToSchemeServer()
{
	struct sockaddr_in sin;
	int sock;

	sock = socket(AF_INET, SOCK_STREAM, 0);

	if (sock == -1)
		return -1;

	memset(&sin.sin_zero, 0, sizeof(sin.sin_zero));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(ELDERDPORT);
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if (connect(sock, &sin, sizeof(sin)) == -1)
		return -1;

	return sock;
}


void * SchemeThread(void *dummy)
{
	int size, bufroom;
	struct data_a2e_evalstring *eval;
	void *buf;

	bufroom = 1024;
	buf = amalloc(bufroom);

	for ( ; ; )
	{
		/* first, pass queued strings to the daemon */
		eval = MPTryRemove(&toscm);
		if (eval)
		{
			size = sizeof(struct data_a2e_evalstring) + strlen(eval->string);
			write_message(schemesock, eval, size);
			afree(eval);
		}

		/* next, read some data from the daemon (and sleep a bit too) */
		{
			struct timeval tv = { 0, 100000 };
			fd_set fds;
			int n;

			FD_ZERO(&fds);
			FD_SET(schemesock, &fds);

			n = select(schemesock+1, &fds, NULL, NULL, &tv);
			if (n == 1)
			{	/* we got data, process it */
				read_full(schemesock, &size, sizeof(int));
				if (size > MAX_MESSAGE_SIZE)
				{	/* eat data */
					while (size > bufroom)
					{
						read_full(schemesock, buf, bufroom);
						size -= bufroom;
					}
					read_full(schemesock, buf, size);
				}
				else
				{
					/* make sure we have enough room */
					if (size > bufroom)
					{
						void *testbuf;
						int testbufroom;

						/* try to grow buffer */
						testbufroom = bufroom * 2;
						if (size > testbufroom) testbufroom = size;
						testbuf = realloc(buf, testbufroom);

						if (!testbuf)
						{	/* failed, eat data */
							log->Log(L_ERROR, "<scheme> realloc failed in SchemeThread, cannot process data");
							while (size > bufroom)
							{
								read_full(schemesock, buf, bufroom);
								size -= bufroom;
							}
							read_full(schemesock, buf, size);
						}
						else
						{	/* we're good to go, update buf and its size */
							buf = testbuf;
							bufroom = testbufroom;
						}
					}
					/* test again in case realloc failed */
					if (size <= bufroom)
					{
						read_full(schemesock, buf, size);
						if (ProcessMessage(buf, size) == 1)
							return NULL;
					}
				}
			}
		}
	}
}


void SendPlayerData(int pid)
{
	struct data_a2e_playerdata pd;

	pd.type = A2E_PLAYERDATA;
	pd.pid = pid;
	if (pid >= 0 && pid < MAXPLAYERS)
		memcpy(&pd.data, players + pid, sizeof(PlayerData));
	else
		memset(&pd.data, 0, sizeof(PlayerData));
	write_message(schemesock, &pd, sizeof(struct data_a2e_playerdata));
}


int ProcessMessage(void *data, int size)
{
	switch(*(int*)data)
	{
		case E2A_NULL:
			break;
		case E2A_SHUTTINGDOWN:
			return 1;
			break;
		case E2A_SENDMESSAGE:
			{
				struct data_e2a_sendmessage *d = data;
				if (chat)
					chat->SendMessage(d->pid, "%s", d->message);
			}
			break;
		case E2A_GETPLAYERDATA:
			break;
		case E2A_GETPLAYERLIST:
			break;
		case E2A_FINDPLAYER:
			{
				struct data_e2a_findplayer *d = data;
				int res = pd->FindPlayer(d->name);
				SendPlayerData(res);
			}
			break;
		case E2A_GETSETTING:
			break;
		case E2A_SETSETTING:
			break;
		case E2A_INSTALLCALLBACK:
			break;
		case E2A_RUNCOMMAND:
			break;

	}
	return 0;
}



void Cscm(const char *params, int pid, int target)
{
	struct data_a2e_evalstring *msg;

	msg = amalloc(sizeof(*msg) + strlen(params));

	msg->type = A2E_EVALSTRING;
	msg->pid = pid;
	strcpy(msg->string, params);
	MPAdd(&toscm, msg);
}




