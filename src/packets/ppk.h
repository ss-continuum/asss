
#ifndef __PACKETS_PPK_H
#define __PACKETS_PPK_H

/* ppk.h - position and weapons packets */


/* weapon codes */
#define W_NULL          0
#define W_BULLET        1
#define W_BOUNCEBULLET  2
#define W_BOMB          3
#define W_PROXBOMB      4
#define W_REPEL         5
#define W_DECOY         6
#define W_BURST         7
#define W_THOR          8
#define W_WORMHOLE      0 /* used in watchdamage packet only */

struct Weapons
{
	/* this is a bit field. the whole thing should fit into 16 bits */
	unsigned short type : 5;
	unsigned short level : 2;
	unsigned short shrapbouncing : 1;
	unsigned short shraplevel : 2;
	unsigned short shrap : 5;
	unsigned short alternate : 1;
};


struct ExtraPosData
{
	unsigned int energy : 16;
	unsigned int s2cping : 16;
	unsigned int timer : 16;
	unsigned int shields : 1;
	unsigned int super : 1;
	unsigned int bursts : 4;
	unsigned int repels : 4;
	unsigned int thors : 4;
	unsigned int bricks : 4;
	unsigned int decoys : 4;
	unsigned int rockets : 4;
	unsigned int portals : 4;
	unsigned int padding : 2;
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
	u8 ping;
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
	u8 ping;
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

