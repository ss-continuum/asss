
#ifndef __PACKETS_PDATA_H
#define __PACKETS_PDATA_H

/* pdata.h - player data packet plus internal fields */


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
	i8 unknown1[3];
	/* stuff below this point is not part of the recieved data */
	int status, arena;
	/* these only need to go up to 255 or so, so save space. the above
	 * two can fit in bytes too, but they're accessed very frequently. */
	unsigned char type, whenloggedin, pflags, oldarena;
	char name[24], squad[24];
	i16 xres, yres;
	struct PlayerPosition position;
} PlayerData;


/* flag bits for the flags bits */

/* set when the player has changed freqs or ships, but before he has
 * acknowleged it */
#define F_DURING_CHANGE 0x01
#define SET_DURING_CHANGE(pid) pd->players[pid].pflags |= F_DURING_CHANGE
#define RESET_DURING_CHANGE(pid) pd->players[pid].pflags &= ~F_DURING_CHANGE
#define IS_DURING_CHANGE(pid) (pd->players[pid].pflags & F_DURING_CHANGE)

#endif


