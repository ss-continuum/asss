

#include "asss.h"

#define MYTYPE 1


/* structs */

typedef struct EncData
{
	int key, status;
	char enctable[520];
} EncData;


/* prototypes */

local int Respond(int);
local void Init(int, int);
local void Encrypt(int, char *, int);
local void Decrypt(int, char *, int);
local void Void(int);


/* globals */

local EncData enc[MAXPLAYERS];
local Mutex statmtx;

local Iencrypt _int =
{
	INTERFACE_HEAD_INIT("encrypt-1")
	Respond, Init, Encrypt, Decrypt, Void
};



EXPORT int MM_encrypt1(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
		InitMutex(&statmtx);
		mm->RegInterface("encrypt\x01", &_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface("encrypt\x01", &_int, ALLARENAS))
			return MM_FAIL;
		return MM_OK;
	}
	else if (action == MM_CHECKBUILD)
		return BUILDNUMBER;
	return MM_FAIL;
}


int Respond(int key)
{
	return -key;
}


void Init(int pid, int k)
{
	int t, loop;
	short *mytable = (short *) enc[pid].enctable;

	if (k == 0) return;

	LockMutex(&statmtx);
	enc->key = k;
	UnlockMutex(&statmtx);

	for (loop = 0; loop < 0x104; loop++)
	{
#ifndef WIN32
		asm ( "imul %%ecx" : "=d" (t) : "a" (k), "c" (0x834E0B5F) );
#else
		_asm
		{
			mov eax,k
			mov ecx,0x834E0B5F
			imul ecx
			mov t,edx
		};
#endif
		t = (t + k) >> 16;
		t += t >> 31;
		t = ((((((t * 9) << 3) - t) * 5) << 1) - t) << 2;
		k = (((k % 0x1F31D) * 0x41A7) - t) + 0x7B;
		if (!k || (k & 0x80000000)) k += 0x7FFFFFFF;
		mytable[loop] = (short)k;
	}
}


void Encrypt(int pid, char *data, int len)
{
	int *mytable = (int *) enc[pid].enctable, *mydata = (int *) data;
	int work, loop, until = (len/4)+1;

	LockMutex(&statmtx);
	work = enc[pid].key;
	UnlockMutex(&statmtx);

	if (work == 0) return;

	for (loop = 0; loop < until; loop++)
	{
		work = mydata[loop] ^ (mytable[loop] ^ work);
		mydata[loop] = work;
	}
}


void Decrypt(int pid, char *data, int len)
{
	int *mytable = (int *) enc[pid].enctable, *mydata = (int *) data;
	int work, loop, until = (len/4)+1, esi, edx;

	LockMutex(&statmtx);
	work = enc[pid].key;
	UnlockMutex(&statmtx);

	if (work == 0) return;

	for (loop = 0; loop < until; loop++)
	{
		esi = mytable[loop];
		edx = mydata[loop];
		esi ^= work;
		esi ^= edx;
		mydata[loop] = esi;
		work = edx;
	}

}


void Void(int pid)
{
	LockMutex(&statmtx);
	enc[pid].key = 0;
	UnlockMutex(&statmtx);
}

