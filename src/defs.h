
/* dist: public */

#ifndef __DEFS_H
#define __DEFS_H

#include <stddef.h>

#include "util.h"

#define ASSSVERSION "0.9.13"
#define ASSSVERSION_NUM 0x0000090D
#define BUILDDATE __DATE__ " " __TIME__


/* do it upfront so we don't have to worry */
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


/* hopefully useful exit codes */
#define EXIT_NONE      0 /* an exit from *shutdown */
#define EXIT_RECYCLE   1 /* an exit from *recycle */
#define EXIT_GENERAL   2 /* a general 'something went wrong' error */
#define EXIT_MEMORY    3 /* we ran out of memory */
#define EXIT_MODCONF   4 /* the initial module file is missing */
#define EXIT_MODLOAD   5 /* an error loading initial modules */
#define EXIT_CHROOT    6 /* can't chroot or setuid */


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


/* prize constants */
enum
{
	PRIZE_RECHARGE = 1,
	PRIZE_ENERGY,
	PRIZE_ROTATION,
	PRIZE_STEALTH,
	PRIZE_CLOAK,
	PRIZE_XRADAR,
	PRIZE_WARP,
	PRIZE_GUN,
	PRIZE_BOMB,
	PRIZE_BOUNCE,
	PRIZE_THRUST,
	PRIZE_SPEED,
	PRIZE_FULLCHARGE,
	PRIZE_SHUTDOWN,
	PRIZE_MULTI,
	PRIZE_PROX,
	PRIZE_SUPER,
	PRIZE_SHIELD,
	PRIZE_SHRAP,
	PRIZE_ANTIWARP,
	PRIZE_REPEL,
	PRIZE_BURST,
	PRIZE_DECOY,
	PRIZE_THOR,
	PRIZE_BRICK,
	PRIZE_ROCKET,
	PRIZE_PORTAL
};


typedef struct Arena Arena;
typedef struct Player Player;

/* this struct/union thing will be used to refer to a set of players */
typedef struct
{
	enum
	{
		T_NONE,
		T_PLAYER,
		T_ARENA,
		T_FREQ,
		T_ZONE,
		T_LIST
	} type;
	union
	{
		Player *p;
		Arena *arena;
		struct { Arena *arena; int freq; } freq;
		LinkedList list;
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

#include "packets/simple.h"


#endif

