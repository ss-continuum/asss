
#ifndef __PACKETS_PDATA_H
#define __PACKETS_PDATA_H

/* pdata.h - player data packet plus internal fields */


struct PlayerPosition
{
	int x, y, xspeed, yspeed;
	int bounty, status;
};


typedef struct PlayerData
{
	u8 type;
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
	int status, whenloggedin, arena, oldarena;
	char name[24], squad[24];
	i16 xres, yres;
	struct PlayerPosition position;
} PlayerData;


#endif


