
#ifndef __CORE_H
#define __CORE_H

/* see below for interface documentation */


#include "packets/logon.h"

#include "packets/mapfname.h"

/* FIXME: move these somewhere else, probably */

typedef struct AuthData
{
	int demodata;
	byte code;
	char name[24];
	char squad[24];
	int killpoints, flagpoints;
	int wins, losses, gameswon;
} AuthData;



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
 * the core module will call the authenticating module with the logon
 * packet that was sent. the authenticator can do whatever it wants with
 * it, but it should probably call SendLogonResponse to send a response
 * back to the client.
 *
 */

typedef struct Iauth
{
	int (*Authenticate)(int pid, struct LogonPacket *lp,
			void (*SendLogonResponse)(int pid, AuthData *data));
} Iauth;


#endif

