
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
#define IS_STANDARD(p) (p->type == T_CONT || p->type == T_VIE)
#define IS_CHAT(p) (p->type == T_CHAT)
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


#include "packets/pdata.h"
#include "packets/ppk.h"
#include "packets/login.h"

struct Arena;

struct PlayerPosition
{
	int x, y, xspeed, yspeed, rotation;
	int bounty, status;
};


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

	int pid, status, type, pflags, whenloggedin;
	Arena *arena, *oldarena;
	char name[24], squad[24];
	i16 xres, yres;
	unsigned int connecttime;
	/* this is a number between 0 and RAND_MAX. for each incoming
	 * weapon, if rand() is less than this, it's ignored. this really
	 * shouldn't be here, i know. */
	unsigned int ignoreweapons;
	struct PlayerPosition position;
	u32 macid, permid;
	byte playerextradata[0];
};


/* flag bits for the pflags field */

/* set when the player has changed freqs or ships, but before he has
 * acknowleged it */
#define F_DURING_CHANGE 0x01
#define SET_DURING_CHANGE(p) ((p)->pflags |= F_DURING_CHANGE)
#define RESET_DURING_CHANGE(p) ((p)->pflags &= ~F_DURING_CHANGE)
#define IS_DURING_CHANGE(p) ((p)->pflags & F_DURING_CHANGE)

/* if player wants optional .lvz files */
#define F_ALL_LVZ 0x02
#define SET_ALL_LVZ(p) ((p)->pflags |= F_ALL_LVZ)
#define UNSET_ALL_LVZ(p) ((p)->pflags &= ~F_ALL_LVZ)
#define WANT_ALL_LVZ(p) ((p)->pflags & F_ALL_LVZ)

/* if player is waiting for db query results */
#define F_DURING_QUERY 0x04
#define SET_DURING_QUERY(p) ((p)->pflags |= F_DURING_QUERY)
#define UNSET_DURING_QUERY(p) ((p)->pflags &= ~F_DURING_QUERY)
#define IS_DURING_QUERY(p) ((p)->pflags & F_DURING_QUERY)

/* if the player's lag is too high to let him have flags or balls */
#define F_NO_FLAGS_BALLS 0x08
#define SET_NO_FLAGS_BALLS(p) ((p)->pflags |= F_NO_FLAGS_BALLS)
#define UNSET_NO_FLAGS_BALLS(p) ((p)->pflags &= ~F_NO_FLAGS_BALLS)
#define IS_NO_FLAGS_BALLS(p) ((p)->pflags & F_NO_FLAGS_BALLS)

/* if the player has sent a position packet since entering the arena */
#define F_SENT_PPK 0x10
#define SET_SENT_PPK(p) ((p)->pflags |= F_SENT_PPK)
#define UNSET_SENT_PPK(p) ((p)->pflags &= ~F_SENT_PPK)
#define HAS_SENT_PPK(p) ((p)->pflags & F_SENT_PPK)

/* if the player has been authenticated by either a billing server or a
 * password file */
#define F_AUTHENTICATED 0x20
#define SET_AUTHENTICATED(p) ((p)->pflags |= F_AUTHENTICATED)
#define UNSET_AUTHENTICATED(p) ((p)->pflags &= ~F_AUTHENTICATED)
#define IS_AUTHENTICATED(p) ((p)->pflags & F_AUTHENTICATED)


#define I_PLAYERDATA "playerdata-2"

typedef struct Iplayerdata
{
	INTERFACE_HEAD_DECL

	Player * (*NewPlayer)(int type);
	void (*FreePlayer)(Player *p);
	void (*KickPlayer)(Player *p);

	void (*LockPlayer)(Player *p);
	void (*UnlockPlayer)(Player *p);

	Player * (*PidToPlayer)(int pid);
	Player * (*FindPlayer)(const char *name);

	void (*TargetToSet)(const Target *target, LinkedList *set);

	int (*AllocatePlayerData)(size_t bytes);
	/* returns -1 on failure */
	void (*FreePlayerData)(int key);

	void (*Lock)(void);
	void (*WriteLock)(void);
	void (*Unlock)(void);
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

