
#ifndef __CORE_H
#define __CORE_H


#include "packets/login.h"


typedef struct AuthData
{
	int demodata;
	byte code;
	char name[24];
	char squad[24];
} AuthData;


/* playeraction stuff */

#define CB_PLAYERACTION ("playeraction")

#define PA_CONNECT        1
#define PA_DISCONNECT     2
/* these first two actions involve no arena, so callbacks must be
 * registered with ALLARENAS to get them. */
#define PA_PREENTERARENA  3
/* this is called at some unknown point in player processing that
 * happens to be as early as possible. */
#define PA_ENTERARENA     4
#define PA_LEAVEARENA     5
/* these two do involve arenas, so callbacks can be registered either
 * globally or for a specific arena. */

typedef void (*PlayerActionFunc)(int pid, int action, int arena);


/* freq management
 * when a player's ship/freq need to be changed for any reason, one of
 * these functions will be called. it gets the pid that we're dealing
 * with, a request type specifying what type of request was made, the
 * current ship, and the current freq (or -1 if the player has no freq
 * yet). this function may modify one or both of the ship and freq
 * variables. the caller will take care of generating the correct types
 * of events. I can't imagine this callback being called from a place
 * where the player mutex is not held, so callbacks of this type don't
 * have to bother locking the player. */

#define CB_FREQMANAGER "freqman"
typedef void (*FreqManager)(int pid, int request, int *ship, int *freq);

enum
{
	REQUEST_INITIAL, /* used for initial freq assignment */
	REQUEST_SHIP,    /* the player requested a ship change */
	REQUEST_FREQ     /* the player requested a freq change */
};



/*
 * the core module will call the authenticating module with the login
 * packet that was sent. the authenticator must call Done with the pid
 * it was given and a pointer to an AuthData structure (may be located
 * on the stack)
 *
 */

typedef struct Iauth
{
	void (*Authenticate)(int pid, struct LoginPacket *lp,
			void (*Done)(int pid, AuthData *data));
} Iauth;


#endif

