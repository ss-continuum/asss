
/* dist: public */

#ifndef __PACKETS_PDATA_H
#define __PACKETS_PDATA_H

/* pdata.h - player data packet plus internal fields */

#include "ppk.h"
#include "login.h"

struct Arena;

struct PlayerPosition
{
	int x, y, xspeed, yspeed, rotation;
	int bounty, status;
};


typedef struct PlayerData
{
	u8 pktype;
	u8 shiptype;
	u8 flags;
	char sendname[20];
	char sendsquad[20];
	i32 killpoints;
	i32 flagpoints;
	i16 pid;
	i16 freq;
	i16 wins;
	i16 losses;
	i16 attachedto;
	i16 flagscarried;
	u8 miscbits;
	/* stuff below this point is not part of the recieved data */
	int status;
	Arena *arena, *oldarena;
	/* these only need to go up to 255 or so, so save space. the above
	 * two can fit in bytes too, but they're accessed very frequently. */
	unsigned char type, whenloggedin, pflags;
	char name[24], squad[24];
	i16 xres, yres;
	unsigned int connecttime;
	/* this is a number between 0 and RAND_MAX. for each incoming
	 * weapon, if rand() is less than this, it's ignored. this really
	 * shouldn't be here, i know. */
	unsigned int ignoreweapons;
	struct PlayerPosition position;
	u32 macid, permid;
} PlayerData;


/* flag bits for the miscbits field */

/* whether the player has a crown */
#define F_HAS_CROWN 0x01
#define SET_HAS_CROWN(pid) (pd->players[pid].miscbits |= F_HAS_CROWN)
#define UNSET_HAS_CROWN(pid) (pd->players[pid].miscbits &= ~F_HAS_CROWN)

/* whether clients should send data for damage done to this player */
#define F_SEND_DAMAGE 0x02
#define SET_SEND_DAMAGE(pid) (pd->players[pid].miscbits |= F_SEND_DAMAGE)
#define UNSET_SEND_DAMAGE(pid) (pd->players[pid].miscbits &= ~F_SEND_DAMAGE)


/* flag bits for the pflags field */

/* set when the player has changed freqs or ships, but before he has
 * acknowleged it */
#define F_DURING_CHANGE 0x01
#define SET_DURING_CHANGE(pid) (pd->players[pid].pflags |= F_DURING_CHANGE)
#define RESET_DURING_CHANGE(pid) (pd->players[pid].pflags &= ~F_DURING_CHANGE)
#define IS_DURING_CHANGE(pid) (pd->players[pid].pflags & F_DURING_CHANGE)

/* if player wants optional .lvz files */
#define F_ALL_LVZ 0x02
#define SET_ALL_LVZ(pid) (pd->players[pid].pflags |= F_ALL_LVZ)
#define UNSET_ALL_LVZ(pid) (pd->players[pid].pflags &= ~F_ALL_LVZ)
#define WANT_ALL_LVZ(pid) (pd->players[pid].pflags & F_ALL_LVZ)

/* if player is waiting for db query results */
#define F_DURING_QUERY 0x04
#define SET_DURING_QUERY(pid) (pd->players[pid].pflags |= F_DURING_QUERY)
#define UNSET_DURING_QUERY(pid) (pd->players[pid].pflags &= ~F_DURING_QUERY)
#define IS_DURING_QUERY(pid) (pd->players[pid].pflags & F_DURING_QUERY)

/* if the player's lag is too high to let him have flags or balls */
#define F_NO_FLAGS_BALLS 0x08
#define SET_NO_FLAGS_BALLS(pid) (pd->players[pid].pflags |= F_NO_FLAGS_BALLS)
#define UNSET_NO_FLAGS_BALLS(pid) (pd->players[pid].pflags &= ~F_NO_FLAGS_BALLS)
#define IS_NO_FLAGS_BALLS(pid) (pd->players[pid].pflags & F_NO_FLAGS_BALLS)

/* if the player has sent a position packet since entering the arena */
#define F_SENT_PPK 0x10
#define SET_SENT_PPK(pid) (pd->players[pid].pflags |= F_SENT_PPK)
#define UNSET_SENT_PPK(pid) (pd->players[pid].pflags &= ~F_SENT_PPK)
#define HAS_SENT_PPK(pid) (pd->players[pid].pflags & F_SENT_PPK)

#endif


