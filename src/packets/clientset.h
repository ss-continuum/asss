
/* dist: public */

#ifndef __PACKETS_CLIENTSET_H
#define __PACKETS_CLIENTSET_H

/* structs for packet types and data */

struct WeaponBits
{
	unsigned int ShrapnelMax    : 5;
	unsigned int ShrapnelRate   : 5;
	unsigned int CloakStatus    : 2;
	unsigned int StealthStatus  : 2;
	unsigned int XRadarStatus   : 2;
	unsigned int AntiWarpStatus : 2;
	unsigned int InitialGuns    : 2;
	unsigned int MaxGuns        : 2;
	unsigned int InitialBombs   : 2;
	unsigned int MaxBombs       : 2;
	unsigned int DoubleBarrel   : 1;
	unsigned int EmpBomb        : 1;
	unsigned int SeeMines       : 1;
	unsigned int Unused1        : 3;
};

struct MiscBitfield
{
	unsigned short SeeBombLevel   : 2;
	unsigned short DisableFastShooting : 1;
	unsigned short Padding1       : 5;
	unsigned short Radius         : 8;
};

struct ShipSettings /* 144 bytes */
{
	i32 long_set[2];
	i16 short_set[49];
	i8 byte_set[18];
	struct WeaponBits Weapons;
	byte Padding[16];
};


struct ClientSettings
{
	struct
	{
		unsigned int type : 8; /* 0x0F */
		unsigned int ExactDamage : 1;
		unsigned int HideFlags : 1;
		unsigned int NoXRadar : 1;
		unsigned int SlowFrameRate : 3;
		unsigned int DisableScreenshot : 1;
		unsigned int MaxTimerDrift : 3;
		unsigned int Pad2 : 14;
	} bit_set;
	struct ShipSettings ships[8];
	i32 long_set[20];
	struct
	{
		unsigned int x : 10;
		unsigned int y : 10;
		unsigned int r : 9;
		unsigned int pad : 3;
	} spawn_pos[4];
	i16 short_set[58];
	i8 byte_set[32];
	i8 prizeweight_set[28];
};


#endif

