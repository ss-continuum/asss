
#ifndef __PACKETS_WATCHDAMAGE_H
#define __PACKETS_WATCHDAMAGE_H

struct S2CWatchDamage
{
	u8 type;
	i16 damageuid;
	u32 tick;
	i16 shooteruid;
	struct Weapons weapon;
	i16 energy;
	i16 damage;
	u8 unknown;
};

struct C2SWatchDamage
{
	u8 type;
	u32 tick;
	i16 shooteruid;
	struct Weapons weapon;
	i16 energy;
	i16 damage;
	u8 unknown;
};

#endif

