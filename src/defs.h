
#ifndef __DEFS_H
#define __DEFS_H

#include <stddef.h>


#define ASSSVERSION "0.7.0"
#define ASSSVERSION_NUM 0x00000700
#define BUILDDATE __DATE__ " " __TIME__


/* do it upfront so we don't have to worry :) */
#ifdef WIN32
#pragma warning ( disable : 4103 )
#endif
#pragma pack(1)
#ifdef WIN32
#pragma warning ( default : 4013 )
#endif

/* an alias to keep most stuff local to modules */
#define local static


/* bring in local config options */
#include "param.h"


/* really important constants */
#define MAXPLAYERS CFG_MAX_PLAYERS
#define MAXARENA CFG_MAX_ARENAS

#define MAXPACKET 512
#define MAXBIGPACKET CFG_MAX_BIG_PACKET


/* client types */

enum
{
	T_UNKNOWN,
	/* this probably won't be used */

	T_FAKE,
	/* no client, internal to server */

	T_VIE,
	/* original vie client */

	T_CONT,
	/* continuum client */

	T_CHAT,
	/* simple chat client */
};


/* player status codes */

enum
{
	S_FREE,
	/* this player entry is free to be reused */

	S_CONNECTED,
	/* player is connected (key exchange completed) but has not logged
	 * in yet */

	S_NEED_AUTH,
	/* player sent login, auth request will be sent */

	S_WAIT_AUTH,
	/* waiting for auth response */

	S_NEED_GLOBAL_SYNC,
	/* auth done, will request global sync */

	S_WAIT_GLOBAL_SYNC,
	/* waiting for sync global persistant data to complete */

	S_DO_GLOBAL_CALLBACKS,
	/* global sync done, will call global player connecting callbacks */

	S_SEND_LOGIN_RESPONSE,
	/* callbacks done, will send arena response */

	S_LOGGEDIN,
	/* player is finished logging in but is not in an arena yet status
	 * returns here after leaving an arena, also */

	S_DO_FREQ_AND_ARENA_SYNC,
	/* player has requested entering an arena, needs to be assigned a
	 * freq and have arena data syched */

	S_WAIT_ARENA_SYNC,
	/* waiting for scores sync */

	S_SEND_ARENA_RESPONSE,
	/* done with scores, needs to send arena response */

	S_DO_ARENA_CALLBACKS,
	/* area response sent, now call arena entering callbacks */

	S_PLAYING,
	/* player is playing in an arena. typically the longest stage */

	S_LEAVING_ARENA,
	/* player has left arena, callbacks need to be called will return to
	 * S_LOGGEDIN after this */

	S_LEAVING_ZONE,
	/* player is leaving zone, call disconnecting callbacks, go to
	 * TIMEWAIT after this */

	S_TIMEWAIT
	/* the connection is all set to be ended. the network layer will
	 * move the player to S_FREE after this. */
};


/* hopefully useful exit codes */
#define ERROR_NONE      0 /* an exit from *shutdown */
#define ERROR_RECYCLE   1 /* an exit from *recycle */
#define ERROR_GENERAL   2 /* a general 'something went wrong' error */
#define ERROR_MEMORY    3 /* we ran out of memory */
#define ERROR_BIND      4 /* we can't bind the port */
#define ERROR_MODCONF   5 /* the initial module file is missing */
#define ERROR_MODLOAD   6 /* an error loading initial modules */


/* authentication return codes */
#define AUTH_OK             0x00
#define AUTH_UNKNOWN        0x01
#define AUTH_BADPASSWORD    0x02
#define AUTH_ARENAFULL      0x03
#define AUTH_LOCKEDOUT      0x04
#define AUTH_NOPERMISSION   0x05
#define AUTH_SPECONLY       0x06
#define AUTH_TOOMANYPOINTS  0x07
#define AUTH_TOOSLOW        0x08
#define AUTH_NOPERMISSION2  0x09
#define AUTH_NONEWCONN      0x0A
#define AUTH_BADNAME        0x0B
#define AUTH_OFFENSIVENAME  0x0C
#define AUTH_NOSCORES       0x0D
#define AUTH_SERVERBUSY     0x0E
#define AUTH_EXPONLY        0x0F
#define AUTH_ISDEMO         0x10
#define AUTH_TOOMANYDEMO    0x11
#define AUTH_NODEMO         0x12


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


/* some ship names */
#define WARBIRD   0
#define JAVELIN   1
#define SPIDER    2
#define LEVIATHAN 3
#define TERRIER   4
#define WEASEL    5
#define LANCASTER 6
#define SHARK     7
#define SPEC      8


/* sound constants */
enum
{
	SOUND_NONE = 0,
	SOUND_BEEP1,
	SOUND_BEEP2,
	SOUND_NOTATT,
	SOUND_VIOLENT,
	SOUND_HALLELLULA,
	SOUND_REAGAN,
	SOUND_INCONCEIVABLE,
	SOUND_CHURCHILL,
	SOUND_LISTEN,
	SOUND_CRYING,
	SOUND_BURP,
	SOUND_GIRL,
	SOUND_SCREAM,
	SOUND_FART1,
	SOUND_FART2,
	SOUND_PHONE,
	SOUND_WORLDATTACK,
	SOUND_GIBBERISH,
	SOUND_OOO,
	SOUND_GEE,
	SOUND_OHH,
	SOUND_AWW,
	SOUND_GAMESUCKS,
	SOUND_SHEEP,
	SOUND_CANTLOGIN,
	SOUND_BEEP3,
	SOUND_MUSICLOOP = 100,
	SOUND_MUSICSTOP,
	SOUND_MUSICONCE,
	SOUND_DING,
	SOUND_GOAL
};


/* this struct/union thing will be used to refer to a set of players */
typedef struct
{
	enum
	{
		T_NONE,
		T_PID,
		T_ARENA,
		T_FREQ,
		T_ZONE,
		T_SET
	} type;
	union
	{
		int pid;
		int arena;
		struct { int arena, freq; } freq;
		int *set;
	} u;
} Target;


/* useful typedefs */
typedef unsigned char byte;


/* platform-specific stuff */

#ifndef WIN32
#define EXPORT
#define TRUE (1)
#define FALSE (0)
#else
#include "win32compat.h"
#endif


#include "packets/sizes.h"

#include "packets/types.h"

#include "packets/ppk.h"

#include "packets/pdata.h"

#include "packets/simple.h"


#endif

