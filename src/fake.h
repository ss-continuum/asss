
#ifndef __FAKE_H
#define __FAKE_H


typedef struct Ifake
{
	INTERFACE_HEAD_DECL

	int (*CreateFakePlayer)(const char *name, int arena, int ship, int freq);
	int (*EndFaked)(int pid);
	void (*ProcessPacket)(int pid, byte *data, int length);
} Ifake;


#endif
