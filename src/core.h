
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

#define CALLBACK_PLAYERACTION ("playeraction")

#define PA_CONNECT     1
#define PA_DISCONNECT  2
/* these first two actions involve no arena, so callbacks must be
 * registered with ALLARENAS to get them. */
#define PA_ENTERARENA  3
#define PA_LEAVEARENA  4
/* these two do involve arenas, so callbacks can be registered either
 * globally or for a specific arena. */

typedef void (*PlayerActionFunc)(int pid, int action, int arena);


/* INTERFACES */

/*
 * this is used by modules who want to replace the freq assignment
 * method. for example, if you want to run a clockwork chaos-type game
 * (each ship type on its own freq) or some strange star warzone
 * settings, you can use this. the interface might change because I
 * don't really like it this way.
 *
 */

typedef struct Iassignfreq
{
	int (*AssignFreq)(int pid, int requested, byte shiptype);
} Iassignfreq;



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

