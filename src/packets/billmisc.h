
#ifndef __PACKETS_BILLMISC_H
#define __PACKETS_BILLMISC_H

/* billmisc.h - miscellaneous billing protocol packets */


struct S2BLogon
{
	i8 type;
	i32 serverid;
	i32 groupid;
	i32 scoreid;
	char name[0x80];
	char pw[0x20];
};

struct S2BPlayerEntering
{
	i8 type;
	i8 logonflag;
	i32 ipaddy;
	char name[32];
	char pw[32];
	i32 pid;
	i32 macid;
	i32 timezone;
	i16 unk4;
};

struct B2SPlayerResponse
{
	i8 type;
	i8 logonflag;
	i32 pid;
	char name[24];
	char squad[24];
	byte banner[96];
	i32 usage;
	i16 year, month, day, hour, minute, second;
	i32 unk1;
	i32 userid;
	i32 unk4;
	// optional:
	i16 wins;
	i16 losses;
	i16 unk5;
	i32 killpoints;
	i32 flagpoints;
};

struct S2BChat
{
	i8 type;
	i32 pid;
	char channel[32];
	char text[0];
};

struct B2SChat
{
	i8 type;
	i32 pid;
	byte channel;
	char text[0];
};

struct S2BRemotePriv
{
	i8 type;
	i32 pid;
	i32 groupid;
	i16 unknown1; // == 0x0002
	char text[0];
};

struct B2SRemotePriv
{
	i8 type;
	i32 scoreid;
	i16 unknown1; // == 0x0002
	char text[0];
};

struct S2BCommand
{
	i8 type;
	i32 pid;
	char text[0];
};


#endif

