

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

local Iencrypt _int = {
	Respond, Init, Encrypt, Decrypt, Void
};



int MM_encrypt1(int action, Imodman *mm)
{
	if (action == MM_LOAD)
	{
		InitMutex(&statmtx);
		mm->RegInterface(I_ENCRYPTBASE + MYTYPE, &_int);
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterface(I_ENCRYPTBASE + MYTYPE, &_int);
	}
	else if (action == MM_DESCRIBE)
	{
		mm->desc = "encrypt1 - standard encryption used in 1.34 and below";
	}
	return MM_OK;
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

	for (loop = 0; loop < 0x104; loop++)
	{
		asm ( "imul %%ecx" : "=d" (t) : "a" (k), "c" (0x834E0B5F) );
		t = (t + k) >> 16;
		t += t >> 31;
		t = ((((((t * 9) << 3) - t) * 5) << 1) - t) << 2;
		k = (((k % 0x1F31D) * 0x41A7) - t) + 0x7B;
		if (!k || (k & 0x80000000)) k += 0x7FFFFFFF;
		mytable[loop] = (short)k;
	}
	enc->key = k;
}


void Encrypt(int pid, char *data, int len)
{
	int *mytable = (int *) enc[pid].enctable, *mydata = (int *) data;
	int work = enc[pid].key, loop, until = (len/4)+1;

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
	int work = enc[pid].key, loop, until = (len/4)+1, esi, edx;

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
	enc[pid].key = 0;
}

