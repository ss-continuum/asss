
/* dist: public */

#ifndef __FAKE_H
#define __FAKE_H

#include "net.h" /* for PacketFunc */


#define I_FAKE "fake-1"

typedef struct Ifake
{
	INTERFACE_HEAD_DECL

	Player * (*CreateFakePlayer)(const char *name, Arena *arena, int ship, int freq);
	int (*EndFaked)(Player *p);
} Ifake;


#endif

