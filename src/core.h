
/* dist: public */

#ifndef __CORE_H
#define __CORE_H


#include "packets/login.h"


/* authentication return codes */
#define AUTH_OK             0x00   /* success */
#define AUTH_NEWNAME        0x01   /* fail */
#define AUTH_BADPASSWORD    0x02   /* fail */
#define AUTH_ARENAFULL      0x03   /* fail */
#define AUTH_LOCKEDOUT      0x04   /* fail */
#define AUTH_NOPERMISSION   0x05   /* fail */
#define AUTH_SPECONLY       0x06   /* success */
#define AUTH_TOOMANYPOINTS  0x07   /* fail */
#define AUTH_TOOSLOW        0x08   /* fail */
#define AUTH_NOPERMISSION2  0x09   /* fail */
#define AUTH_NONEWCONN      0x0A   /* fail */
#define AUTH_BADNAME        0x0B   /* fail */
#define AUTH_OFFENSIVENAME  0x0C   /* fail */
#define AUTH_NOSCORES       0x0D   /* success */
#define AUTH_SERVERBUSY     0x0E   /* fail */
#define AUTH_TOOLOWUSAGE    0x0F   /* fail */
#define AUTH_NONAME         0x10   /* fail */
#define AUTH_TOOMANYDEMO    0x11   /* fail */
#define AUTH_NODEMO         0x12   /* fail */
#define AUTH_CUSTOMTEXT     0x13   /* fail */      /* contonly */

#define AUTH_IS_OK(a) \
	((a) == AUTH_OK || (a) == AUTH_SPECONLY || (a) == AUTH_NOSCORES)


typedef struct AuthData
{
	int demodata;
	int code;
	int authenticated;
	char name[24];
	char sendname[20];
	char squad[24];
	/* code must be set to AUTH_CUSTOMTEXT to use this */
	char customtext[256];
} AuthData;


/* playeraction stuff */

enum
{
	/* these first two actions involve no arena, so callbacks must be
	 * registered with ALLARENAS to get them. */
	PA_CONNECT,
	PA_DISCONNECT,
	/* this is called at some unknown point in player processing that
	 * happens to be as early as possible. it can be used for dangerous
	 * stuff like, say, redirecting people to different arenas. */
	PA_PREENTERARENA,
	/* these two do involve arenas, so callbacks can be registered
	 * either globally or for a specific arena. */
	PA_ENTERARENA,
	PA_LEAVEARENA
};

#define CB_PLAYERACTION "playeraction"
typedef void (*PlayerActionFunc)(Player *p, int action, Arena *arena);
/* pycb: player, int, arena */


/* freq management
 * when a player's ship/freq need to be changed for any reason, one of
 * these functions will be called. it gets the pid that we're dealing
 * with, the current ship, and the current freq (or -1 if the player has
 * no freq yet). this function may modify one or both of the ship and
 * freq variables. the caller will take care of generating the correct
 * types of events. these functions don't have to bother locking the
 * player. */

#define I_FREQMAN "freqman-1"

typedef struct Ifreqman
{
	INTERFACE_HEAD_DECL
	/* pyint: use, impl */
	void (*InitialFreq)(Player *p, int *ship, int *freq);
	/* pyint: player, int inout, int inout -> void */
	void (*ShipChange)(Player *p, int *ship, int *freq);
	/* pyint: player, int inout, int inout -> void */
	void (*FreqChange)(Player *p, int *ship, int *freq);
	/* pyint: player, int inout, int inout -> void */
} Ifreqman;


/*
 * the core module will call the authenticating module with the login
 * packet that was sent. the authenticator must call Done with the pid
 * it was given and a pointer to an AuthData structure (may be located
 * on the stack)
 *
 */

#define I_AUTH "auth-1"

typedef struct Iauth
{
	INTERFACE_HEAD_DECL

	void (*Authenticate)(Player *p, struct LoginPacket *lp, int lplen,
			void (*Done)(Player *p, AuthData *data));
	/* aprc: null */
} Iauth;


#endif

