
#ifndef __PLAYER_H
#define __PLAYER_H

#include "packets/pdata.h"


#define PID_OK(pid) \
	((pid) >= 0 && (pid) < MAXPLAYERS)

#define PID_BAD(pid) \
	((pid) < 0 || (pid) >= MAXPLAYERS)



/* client types */

enum
{
	T_UNKNOWN,
	/* this probably won't be used */

	T_FAKE,
	/* no client, internal to server */

	T_VIE,
	/* original vie client */

	T_CONT,
	/* continuum client */

	T_CHAT,
	/* simple chat client */

	T_OUTGOING,
	/* an outgoing connection from the server (e.g. to the biller) */
};

/* macros for testing types */
#define IS_STANDARD(pid) (pd->players[(pid)].type == T_CONT || pd->players[(pid)].type == T_VIE)
#define IS_CHAT(pid) (pd->players[(pid)].type == T_CHAT)
#define IS_HUMAN(pid) (IS_STANDARD(pid) || IS_CHAT(pid))


/* player status codes */

enum
{
	S_FREE,
	/* this player entry is free to be reused */

	S_CONNECTED,
	/* player is connected (key exchange completed) but has not logged
	 * in yet */

	S_NEED_AUTH,
	/* player sent login, auth request will be sent */

	S_WAIT_AUTH,
	/* waiting for auth response */

	S_NEED_GLOBAL_SYNC,
	/* auth done, will request global sync */

	S_WAIT_GLOBAL_SYNC,
	/* waiting for sync global persistent data to complete */

	S_DO_GLOBAL_CALLBACKS,
	/* global sync done, will call global player connecting callbacks */

	S_SEND_LOGIN_RESPONSE,
	/* callbacks done, will send arena response */

	S_LOGGEDIN,
	/* player is finished logging in but is not in an arena yet status
	 * returns here after leaving an arena, also */

	S_DO_FREQ_AND_ARENA_SYNC,
	/* player has requested entering an arena, needs to be assigned a
	 * freq and have arena data syched */

	S_WAIT_ARENA_SYNC,
	/* waiting for scores sync */

	S_SEND_ARENA_RESPONSE,
	/* done with scores, needs to send arena response */

	S_DO_ARENA_CALLBACKS,
	/* area response sent, now call arena entering callbacks */

	S_PLAYING,
	/* player is playing in an arena. typically the longest stage */

	S_LEAVING_ARENA,
	/* player has left arena, callbacks need to be called will return to
	 * S_LOGGEDIN after this */

	S_LEAVING_ZONE,
	/* player is leaving zone, call disconnecting callbacks, go to
	 * TIMEWAIT after this */

	S_TIMEWAIT
	/* the connection is all set to be ended. the network layer will
	 * move the player to S_FREE after this. */
};



#define I_PLAYERDATA "playerdata-1"

typedef struct Iplayerdata
{
	INTERFACE_HEAD_DECL

	PlayerData *players;

	int (*NewPlayer)(int type);
	void (*FreePlayer)(int pid);
	void (*KickPlayer)(int pid);

	void (*LockPlayer)(int pid);
	void (*UnlockPlayer)(int pid);

	void (*LockStatus)(void);
	void (*UnlockStatus)(void);

	int (*FindPlayer)(const char *name);

	void (*TargetToSet)(const Target *target, int set[MAXPLAYERS+1]);
} Iplayerdata;

#endif

