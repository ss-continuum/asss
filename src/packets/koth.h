
/* dist: public */

#ifndef __PACKETS_KOTH_H
#define __PACKETS_KOTH_H

struct S2CKoth
{
	u8 type;
	u8 action;
	u32 time;
	i16 pid;
};

struct S2CSetKothTimer
{
	u8 type;
	u32 time;
};

#endif

