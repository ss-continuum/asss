
#ifndef __PACKETS_BILLMISC_H
#define __PACKETS_BILLMISC_H

#include "banners.h"

/* billmisc.h - miscellaneous billing protocol packets */

struct S2BLogin
{
	u8 type; /* S2B_LOGIN */
	i32 serverid;
	i32 groupid;
	i32 scoreid;
	char name[0x80];
	char pw[0x20];
};

struct S2BPlayerEntering
{
	u8 type; /* S2B_PLAYERLOGIN */
	i8 loginflag;
	u32 ipaddy;
	char name[32];
	char pw[32];
	i32 pid;
	i32 macid;
	i32 timezone;
	i16 unk4;
	byte contid[64]; /* optional, for cont clients */
};

struct S2BPlayerLeaving
{
	u8 type; /* S2B_PLAYERLEAVING */
	u32 pid;
};

struct S2BRemotePriv
{
	u8 type; /* S2B_PRIVATEMSG */
	i32 pid;
	i32 groupid;
	i16 unknown1; // == 0x0002
	char text[0];
};

struct S2BLogMessage
{
	u8 type; /* S2B_LOGMESSAGE */
	i32 uid;
	i32 targetuid; /* if it was a private message, has their userid, else -1 */
	char text[0];
};

struct S2BWarning
{
	u8 type; /* S2B_WARNING */
	i32 unknown;
	char text[0];
};

struct S2BStatus
{
	u8 type; /* S2B_STATUS */
	i32 pid;
	i32 unknown[4];
};

struct S2BCommand
{
	u8 type; /* S2B_COMMAND */
	i32 pid;
	char text[0];
};

struct S2BChat
{
	u8 type; /* S2B_CHATMSG */
	i32 pid;
	char channel[32];
	char text[0];
};


struct B2SPlayerResponse
{
	u8 type; /* B2S_PLAYERDATA */
	i8 loginflag;
	i32 pid;
	char name[24];
	char squad[24];
	Banner banner;
	i32 usage;
	i16 year, month, day, hour, minute, second;
	i32 unk1;
	i32 userid;
	i32 unk4;
	// optional:
	i16 wins;
	i16 losses;
	i16 flagvictories;
	i32 killpoints;
	i32 flagpoints;
};

struct B2SShutdown
{
	u8 type; /* B2S_SHUTDOWN */
	u8 unknown1;
	i32 unknown2; // == 1
	i32 unknown3; // == 2
};

struct B2SZoneMessage
{
	u8 type; /* B2S_ZONEMESSAGE */
	i32 unknown1; // == 0x3000
	i16 unknown2; // == 0x0002
	char text[0];
};

struct B2SRecycle
{
	u8 type; /* B2S_RECYCLE */
	u8 unknown1;
	i32 unknown2; // == 1
	i32 unknown3; // == 2
};

struct B2SKickUser
{
	u8 type; /* B2S_KICKUSER */
	i32 uid;
	i32 reason;
};

struct B2SRemotePriv
{
	u8 type; /* B2S_SINGLEMESSAGE */
	i32 uid;
	char text[0];
};

struct B2SChat
{
	u8 type; /* B2S_CHATMSG */
	i32 uid;
	u8 channel;
	char text[0];
};

#endif

