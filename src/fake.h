
#ifndef __FAKE_H
#define __FAKE_H

#include "net.h" /* for PacketFunc */


#define I_FAKE "fake-1"

typedef struct Ifake
{
	INTERFACE_HEAD_DECL

	int (*CreateFakePlayer)(const char *name, int arena, int ship, int freq);
	int (*EndFaked)(int pid);
} Ifake;


#endif

