
#include "asss.h"
#include "encrypt.h"

/* structs */

typedef struct EncData
{
	int key, status;
	char enctable[520];
} EncData;


/* prototypes */

local void ConnInit(struct sockaddr_in *sin, byte *pkt, int len);

local void Init(int pid, int k);
local int Encrypt(int, byte *, int);
local int Decrypt(int, byte *, int);
local void Void(int);


/* globals */

local EncData enc[MAXPLAYERS];
local pthread_mutex_t statmtx;

local Inet *net;

local Iencrypt _int =
{
	INTERFACE_HEAD_INIT(NULL, "vieenc")
	Encrypt, Decrypt, Void
};


EXPORT int MM_encrypt1(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
		net = mm->GetInterface(I_NET, ALLARENAS);
		mm->RegCallback(CB_CONNINIT, ConnInit, ALLARENAS);
		pthread_mutex_init(&statmtx, NULL);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CB_CONNINIT, ConnInit, ALLARENAS);
		mm->ReleaseInterface(net);
		/* don't destroy statmtx here, because we may be asked to
		 * encrypt a few more packets. yes, it's ugly. */
		return MM_OK;
	}
	return MM_FAIL;
}

void ConnInit(struct sockaddr_in *sin, byte *pkt, int len)
{
	int key, pid;

	/* make sure the packet fits */
	if (len != 8 || pkt[0] != 0x00 || pkt[1] != 0x01 ||
			pkt[6] != 0x01 || pkt[7] != 0x00)
		return;

	/* ok, it fits. get connection. */
	pid = net->NewConnection(T_VIE, sin, &_int);

	if (pid == -1)
	{
		/* no slots left? */
		byte pkt[2] = { 0x00, 0x07 };
		net->ReallyRawSend(sin, (byte*)&pkt, 2);
		return;
	}

	key = *(int*)(pkt+2);
	key = -key;

	{
		/* respond */
		struct
		{
			u8 t1, t2;
			int key;
		}
		pkt = { 0x00, 0x02, key };
		net->ReallyRawSend(sin, (byte*)&pkt, sizeof(pkt));
	}

	/* init encryption tables */
	Init(pid, key);
}


void Init(int pid, int k)
{
	int t, loop;
	short *mytable = (short *) enc[pid].enctable;

	if (k == 0) return;

	pthread_mutex_lock(&statmtx);
	enc->key = k;
	pthread_mutex_unlock(&statmtx);

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


int Encrypt(int pid, byte *data, int len)
{
	int *mytable = (int *) enc[pid].enctable, *mydata;
	int work, loop, until;

	if (data[0] == 0)
	{
		mydata = (int*)(data + 2);
		until = (len-2)/4 + 1;
	}
	else
	{
		mydata = (int*)(data + 1);
		until = (len-1)/4 + 1;
	}

	pthread_mutex_lock(&statmtx);
	work = enc[pid].key;
	pthread_mutex_unlock(&statmtx);

	if (work == 0) return len;

	for (loop = 0; loop < until; loop++)
	{
		work = mydata[loop] ^ (mytable[loop] ^ work);
		mydata[loop] = work;
	}
	return len;
}


int Decrypt(int pid, byte *data, int len)
{
	int *mytable = (int *) enc[pid].enctable, *mydata;
	int work, loop, until, esi, edx;

	if (data[0] == 0)
	{
		mydata = (int*)(data + 2);
		until = (len-2)/4 + 1;
	}
	else
	{
		mydata = (int*)(data + 1);
		until = (len-1)/4 + 1;
	}

	pthread_mutex_lock(&statmtx);
	work = enc[pid].key;
	pthread_mutex_unlock(&statmtx);

	if (work == 0) return len;

	for (loop = 0; loop < until; loop++)
	{
		esi = mytable[loop];
		edx = mydata[loop];
		esi ^= work;
		esi ^= edx;
		mydata[loop] = esi;
		work = edx;
	}
	return len;
}


void Void(int pid)
{
	pthread_mutex_lock(&statmtx);
	enc[pid].key = 0;
	pthread_mutex_unlock(&statmtx);
}

