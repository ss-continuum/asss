
#ifndef __PACKETS_PPK_H
#define __PACKETS_PPK_H

/* ppk.h - position and weapons packets */


struct Weapons
{   // this is a bit field. the whole thing should fit into 16 bits
	unsigned int type : 5;
	unsigned int level : 2;
	unsigned int unknown : 1;
	unsigned int shraplevel : 2;
	unsigned int shrap : 5;
	unsigned int multimine : 1;
};

struct S2CWeapons
{
	i8 type; /* 0x05 */
	i8 rotation;
	u16 time;
	i16 x;
	i16 yspeed;
	u8 playerid;
	i8 flags;
	i16 xspeed;
	u8 checksum;
	i16 unknown1; /* latency? */
	i16 y;
	i16 bounty;
	struct Weapons weapon;
	byte extradata[0];
};

struct S2CPosition
{
	i8 type; /* 0x28 */
	i8 rotation;
	u16 time;
	i16 x;
	i16 unknown1; /* latency? */
	u8 playerid;
	i8 flags;
	i16 yspeed;
	i16 y;
	i16 xspeed;
	byte extradata[0];
};

struct C2SPosition
{
	i8 type;
	i8 rotation;
	u32 time;
	i16 xspeed;
	i16 y;
	i8 checksum;
	i8 flags;
	i16 x;
	i16 yspeed;
	i16 bounty;
	i16 energy;
	struct Weapons weapon;
	byte extradata[0];
};


#endif

