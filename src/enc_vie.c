
/* dist: public */

#include "asss.h"
#include "encrypt.h"

/* structs */

typedef struct EncData
{
	int key;
	char table[520];
} EncData;


/* prototypes */

local void ConnInit(struct sockaddr_in *sin, byte *pkt, int len);

local void Init(int pid, int k);
local int Encrypt(int, byte *, int);
local int Decrypt(int, byte *, int);
local void Void(int);
local void Initiate(int);
local int HandleResponse(int pid, byte *pkt, int len);


/* globals */

local EncData *enc[MAXPLAYERS+1]; /* plus one for the biller */
local pthread_mutex_t mtx;

local Inet *net;

local Iencrypt _int =
{
	INTERFACE_HEAD_INIT("__unused__", "vieenc")
	Encrypt, Decrypt, Void, Initiate, HandleResponse
};


EXPORT int MM_encrypt1(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
		net = mm->GetInterface(I_NET, ALLARENAS);
		mm->RegCallback(CB_CONNINIT, ConnInit, ALLARENAS);
		pthread_mutex_init(&mtx, NULL);
		mm->RegInterface(&_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;
		mm->UnregCallback(CB_CONNINIT, ConnInit, ALLARENAS);
		mm->ReleaseInterface(net);
		/* don't destroy mtx here, because we may be asked to
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
	short *mytable;
	EncData *ed;
	
	pthread_mutex_lock(&mtx);
	if (!enc[pid])
		enc[pid] = amalloc(sizeof(*ed));
	ed = enc[pid];
	pthread_mutex_unlock(&mtx);

	ed->key = k;

	if (k == 0) return;

	mytable = (short*)(ed->table);

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
	int *mytable = NULL, *mydata;
	int work = 0, loop, until;

	pthread_mutex_lock(&mtx);
	if (enc[pid])
	{
		work = enc[pid]->key;
		mytable = (int*)enc[pid]->table;
	}
	pthread_mutex_unlock(&mtx);

	if (work == 0 || mytable == NULL) return len;

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

	for (loop = 0; loop < until; loop++)
	{
		work = mydata[loop] ^ (mytable[loop] ^ work);
		mydata[loop] = work;
	}
	return len;
}


int Decrypt(int pid, byte *data, int len)
{
	int *mytable = NULL, *mydata;
	int work = 0, loop, until, esi, edx;

	pthread_mutex_lock(&mtx);
	if (enc[pid])
	{
		work = enc[pid]->key;
		mytable = (int*)enc[pid]->table;
	}
	pthread_mutex_unlock(&mtx);

	if (work == 0 || mytable == NULL) return len;

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
	pthread_mutex_lock(&mtx);
	afree(enc[pid]);
	enc[pid] = NULL;
	pthread_mutex_unlock(&mtx);
}


void Initiate(int pid)
{
	struct
	{
		u8 t1, t2;
		int key;
	}
	pkt = { 0x00, 0x01, GTC() };

	if (pkt.key > 0) pkt.key = -pkt.key;
	net->SendToOne(pid, (byte*)&pkt, 6, NET_UNRELIABLE | NET_PRI_P5);

	pthread_mutex_lock(&mtx);
	if (!enc[pid])
		enc[pid] = amalloc(sizeof(EncData));
	enc[pid]->key = pkt.key;
	pthread_mutex_unlock(&mtx);
}

int HandleResponse(int pid, byte *pkt, int len)
{
	int mykey, rkey = *(int*)(pkt+2);
	EncData *ed;

	pthread_mutex_lock(&mtx);
	ed = enc[pid];
	pthread_mutex_unlock(&mtx);

	if (!ed) return FALSE;

	mykey = ed->key;

	if (mykey == rkey)
		/* no encryption */
		Init(pid, 0);
	else
		/* regular encryption */
		Init(pid, rkey);

	return TRUE;
}

