
#ifndef __PACKETS_PPK_H
#define __PACKETS_PPK_H

/* ppk.h - position and weapons packets */

#pragma pack(1)

struct Weapons
{   /* this is a bit field. the whole thing should fit into 16 bits */
	unsigned int type : 5;
	unsigned int level : 2;
	unsigned int unknown : 1;
	unsigned int shraplevel : 2;
	unsigned int shrap : 5;
	unsigned int multimine : 1;
};

struct ExtraPosData
{
	unsigned energy : 16;
	unsigned kothtimer : 16; /* unverified */
	unsigned flagtimer : 16;
	unsigned super : 1;
	unsigned shields : 1;
	unsigned bursts : 4;
	unsigned repels : 4;
	unsigned thors : 4;
	unsigned bricks : 4;
	unsigned decoys : 4;
	unsigned rockets : 4;
	unsigned portals : 4;
	unsigned padding : 2;
};

struct S2CWeapons
{
	u8 type; /* 0x05 */
	i8 rotation;
	u16 time;
	i16 x;
	i16 yspeed;
	u16 playerid;
	i16 xspeed;
	u8 checksum;
	u8 status;
	u8 unknown1;
	i16 y;
	u16 bounty;
	struct Weapons weapon;
	struct ExtraPosData extra;
};

struct S2CPosition
{
	u8 type; /* 0x28 */
	i8 rotation;
	u16 time;
	i16 x;
	u8 unknown1;
	u8 bounty;
	u8 playerid;
	i8 status;
	i16 yspeed;
	i16 y;
	i16 xspeed;
	struct ExtraPosData extra;
};

struct C2SPosition
{
	u8 type; /* 0x03 */
	i8 rotation;
	u32 time;
	i16 xspeed;
	i16 y;
	i8 checksum;
	i8 status;
	i16 x;
	i16 yspeed;
	u16 bounty;
	i16 energy;
	struct Weapons weapon;
	struct ExtraPosData extra;
};


#endif

