
#ifndef __DEFS_H
#define __DEFS_H

#include <stddef.h>

/* do it upfront so we don't have to worry :) */
#pragma pack(1)

/* an alias to keep most stuff local to modules */
#define local static


/* really important constants */
#define MAXPLAYERS 255
#define MAXARENA 50

#define MAXPACKET 512
#define MAXBIGPACKET 524288

#define MAXINTERFACE 256

#define TRUE (1)
#define FALSE (0)


/* interface ids, kept here to make sure they're unique */
#define I_NULL          0
#define I_MODMAN        1
#define I_PLAYERDATA    2
#define I_MAINLOOP      3
#define I_CONFIG        4
#define I_NET           5
#define I_LOGMAN        6
#define I_CMDMAN        7
#define I_CHAT          8
#define I_ARENAMAN      9
#define I_ASSIGNFREQ    10
#define I_AUTH          11
#define I_BILLCORE      12
#define I_MAPNEWSDL     13
#define I_CLIENTSET     14
#define I_PERSIST       15
#define I_STATS         16
#define I_LOG_FILE      17
#define I_FLAGS         18
#define I_ENCRYPTBASE   90
/*#define I_BALL */
/*#define I_DATABASE */
/*#define I_BRICK */
/*#define I_BANNER */


/* action codes for module main functions */
#define MM_LOAD     1
#define MM_UNLOAD   2
#define MM_ATTACH   3
#define MM_DETACH   4


/* return values for the aforementioned */
#define MM_FAIL 1
#define MM_OK   0


/* player status codes */

#define S_FREE                        0
/* this player entry is free to be reused */

#define S_NEED_KEY                    1
/* the player exists, but has not completed key exchange */

#define S_CONNECTED                   2
/* player is connected (key exchange completed)
 * but has not logged in yet */

#define S_NEED_AUTH                   3
/* player sent login, auth request will be sent */

#define S_WAIT_AUTH                   4
/* waiting for auth response */

#define S_NEED_GLOBAL_SYNC            5
/* auth done, will request global sync */

#define S_WAIT_GLOBAL_SYNC            6
/* waiting for sync global persistant data to complete */

#define S_DO_GLOBAL_CALLBACKS         7
/* global sync done, will call global player connecting callbacks */

#define S_SEND_LOGIN_RESPONSE         8
/* callbacks done, will send arena response */

#define S_LOGGEDIN                    9
/* player is finished logging in but is not in an arena yet 
 * status returns here after leaving an arena, also */

#define S_DO_FREQ_AND_ARENA_SYNC      10
/* player has requested entering an arena, needs to be assigned a freq
 * and have arena data syched */

#define S_WAIT_ARENA_SYNC             11
/* waiting for scores sync */

#define S_DO_ARENA_CALLBACKS          12
/* scores sync complete, will call arena entering callbacks */

#define S_SEND_ARENA_RESPONSE         13
/* all done with initalizing, needs to send arena response */

#define S_PLAYING                     14
/* player is playing in an arena. typically the longest stage */

#define S_LEAVING_ARENA               15
/* player has left arena, callbacks need to be called
 * will return to S_LOGGEDIN after this */

#define S_LEAVING_ZONE                16
/* player is leaving zone, call disconnecting callbacks, go to TIMEWAIT
 * after this */

#define S_TIMEWAIT                    17
/* time-wait state for network to flush outgoing packets from the buffer */

#define S_TIMEWAIT2                   18
/* second part of time-wait state. goes to S_FREE after this */



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


/* constants for targets and pids */
#define PID_INTERNAL (-1)

#define TARGET_NONE (-1)
#define TARGET_ARENA (-2)
#define TARGET_FREQ (-3)
#define TARGET_ZONE (-4)


/* symbolic constant for freq assignemnt
 * FIXME: this probably shouldn't exist */
#define BADFREQ (-423)


/* useful typedefs */
typedef unsigned char byte;

#include "packets/sizes.h"

#include "packets/types.h"

#include "packets/ppk.h"

#include "packets/pdata.h"

#include "packets/simple.h"

/* FIXME: eventually, playerdata should look like this
typedef struct PlayerData
{
	int status, arena, oplevel;
	char name[24], squad[24];
	i16 xres, yres;

	struct SentPlayerData sent;
} PlayerData;
*/

#endif

