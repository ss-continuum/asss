
/* dist: public */

#ifndef __PLAYER_H
#define __PLAYER_H


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
};

/* macros for testing types */
#define IS_STANDARD(p) ((p)->type == T_CONT || (p)->type == T_VIE)
#define IS_CHAT(p) ((p)->type == T_CHAT)
#define IS_HUMAN(p) (IS_STANDARD(p) || IS_CHAT(p))


/* player status codes */

enum
{
	S_CONNECTED,
	/* player is connected (key exchange completed) but has not logged
	 * in yet */

	S_NEED_AUTH,
	/* player sent login, auth request will be sent */

	S_WAIT_AUTH,
	/* waiting for auth response */

	S_NEED_GLOBAL_SYNC,
	/* auth done, will request global sync */

	S_WAIT_GLOBAL_SYNC1,
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

	S_WAIT_ARENA_SYNC1,
	/* waiting for scores sync */

	S_SEND_ARENA_RESPONSE,
	/* done with scores, needs to send arena response */

	S_DO_ARENA_CALLBACKS,
	/* area response sent, now call arena entering callbacks */

	S_PLAYING,
	/* player is playing in an arena. typically the longest stage */

	S_LEAVING_ARENA,
	/* player has left arena, callbacks need to be called. */

	S_WAIT_ARENA_SYNC2,
	/* waiting for scores sync, other direction */

	S_LEAVING_ZONE,
	/* player is leaving zone, call disconnecting callbacks */

	S_WAIT_GLOBAL_SYNC2,
	/* waiting for global sync, other direction */

	S_TIMEWAIT
	/* the connection is all set to be ended. the network layer will
	 * move the player to S_FREE after this. */
};


#include "packets/pdata.h"
#include "packets/ppk.h"
#include "packets/login.h"

struct Arena;

struct PlayerPosition
{
	int x, y, xspeed, yspeed, rotation;
	unsigned bounty, status;
};

#define STATUS_STEALTH  0x01U
#define STATUS_CLOAK    0x02U
#define STATUS_XRADAR   0x04U
#define STATUS_ANTIWARP 0x08U
#define STATUS_FLASH    0x10U
#define STATUS_SAFEZONE 0x20U
#define STATUS_UFO      0x40U


struct Player
{
	/* this is the packet that gets sent to clients. some info is kept
	 * in here */
	PlayerData pkt;

	/* these are some macros that make accessing fields in pkt look like
	 * regular fields. */
#define p_ship pkt.ship
#define p_freq pkt.freq
#define p_attached pkt.attachedto

	int pid, status, type, whenloggedin;
	Arena *arena, *oldarena;
	char name[24], squad[24];
	i16 xres, yres;
	ticks_t connecttime;
	/* this is a number between 0 and RAND_MAX. for each incoming
	 * weapon, if rand() is less than this, it's ignored. this really
	 * shouldn't be here, i know. */
	unsigned int ignoreweapons;
	struct PlayerPosition position;
	u32 macid, permid;
	char ipaddr[16];
	/* if the player has connected through a port that sets a default
	 * arena, that will be stored here. */
	const char *connectas;
	struct
	{
		/* if the player has been authenticated by either a billing
		 * server or a password file */
		unsigned authenticated : 1;
		/* set when the player has changed freqs or ships, but before he
		 * has acknowleged it */
		unsigned during_change : 1;
		/* if player wants optional .lvz files */
		unsigned want_all_lvz : 1;
		/* if player is waiting for db query results */
		unsigned during_query : 1;
		/* if the player's lag is too high to let him be in a ship */
		unsigned no_ship : 1;
		/* if the player's lag is too high to let him have flags or
		 * balls */
		unsigned no_flags_balls : 1;
		/* if the player has sent a position packet since entering the
		 * arena */
		unsigned sent_ppk : 1;
		/* if the player is a bot who wants all position packets */
		unsigned see_all_posn : 1;
		unsigned padding1 : 24;
		/* fill this up to 32 bits */
	} flags;
	byte playerextradata[0];
};


#define CB_NEWPLAYER "newplayer"
typedef void (*NewPlayerFunc)(Player *p, int isnew);


#define I_PLAYERDATA "playerdata-3"

typedef struct Iplayerdata
{
	INTERFACE_HEAD_DECL
	/* pyint: use  */

	Player * (*NewPlayer)(int type);
	void (*FreePlayer)(Player *p);
	void (*KickPlayer)(Player *p);
	/* pyint: player -> void */

	void (*LockPlayer)(Player *p);
	void (*UnlockPlayer)(Player *p);

	Player * (*PidToPlayer)(int pid);
	/* pyint: int -> player */
	Player * (*FindPlayer)(const char *name);
	/* pyint: string -> player */

	void (*TargetToSet)(const Target *target, LinkedList *set);

	int (*AllocatePlayerData)(size_t bytes);
	/* returns -1 on failure */
	void (*FreePlayerData)(int key);

	void (*Lock)(void);
	void (*WriteLock)(void);
	void (*Unlock)(void);
	void (*WriteUnlock)(void);
	/* these must always be used to iterate over all the players
	 * (with the FOR_EACH_PLAYER macro). you can usually use the reguar
	 * (read-only) lock. you need the write lock if you're going to be
	 * changing player status values. */

	LinkedList playerlist;
} Iplayerdata;


/* use this to access per-player data */
#define PPDATA(p, mykey) ((void*)((p)->playerextradata+mykey))

/* these assume you have a Link * named 'link' and that 'pd' points to
 * the player data interface. don't forget to use pd->Lock() first. */

/* this is the basic iterating over players macro */
#define FOR_EACH_PLAYER(p) \
	for ( \
			link = LLGetHead(&pd->playerlist); \
			link && ((p = link->data, link = link->next) || 1); )

/* this one gives you a pointer to some private data too */
#define FOR_EACH_PLAYER_P(p, d, key) \
	for ( \
			link = LLGetHead(&pd->playerlist); \
			link && ((p = link->data, \
			          d = PPDATA(p, key), \
			          link = link->next) || 1); )

#endif

