
#ifndef __PACKETS_CLIENTSET_H
#define __PACKETS_CLIENTSET_H

/* structs for packet types and data */

struct WeaponBits
{
	unsigned ShrapnelMax    : 5;
	unsigned ShrapnelRate   : 5;
	unsigned CloakStatus    : 2;
	unsigned StealthStatus  : 2;
	unsigned XRadarStatus   : 2;
	unsigned AntiWarpStatus : 2;
	unsigned InitialGuns    : 2;
	unsigned MaxGuns        : 2;
	unsigned InitialBombs   : 2;
	unsigned MaxBombs       : 2;
	unsigned DoubleBarrel   : 1;
	unsigned EmpBomb        : 1;
	unsigned SeeMines       : 1;
	unsigned Unused1        : 3;
};

struct MiscBitfield
{
	unsigned SeeBombLevel   : 2;
	unsigned DisableFastShooting : 1;
	unsigned Padding1       : 5;
	unsigned Radius         : 8;
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
	i8 type; /* 0x0F */
	struct
	{
		unsigned ExactDamage : 1;
		unsigned HideFlags : 1;
		unsigned NoXRadar : 1;
		unsigned Padding : 21;
	} bit_set;
	struct ShipSettings ships[8];
	i32 long_set[24];
	i16 short_set[58];
	i8 byte_set[32];
	i8 prizeweight_set[28];
};


#endif

