
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

local void ConnInit(struct sockaddr_in *sin, byte *pkt, int len, void *v);

local void Init(Player *p, int k);
local int Encrypt(Player *p, byte *, int);
local int Decrypt(Player *p, byte *, int);
local void Void(Player *p);

/* globals */

local int enckey;
local pthread_mutex_t mtx;

local Inet *net;
local Iplayerdata *pd;

local Iencrypt _int =
{
	INTERFACE_HEAD_INIT("__unused__", "vieenc")
	Encrypt, Decrypt, Void,
};


EXPORT int MM_encrypt1(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		net = mm->GetInterface(I_NET, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		if (!net || !pd) return MM_FAIL;
		enckey = pd->AllocatePlayerData(sizeof(EncData*));
		if (enckey == -1) return MM_FAIL;
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
		pd->FreePlayerData(enckey);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(pd);
		pthread_mutex_destroy(&mtx);
		return MM_OK;
	}
	return MM_FAIL;
}


void ConnInit(struct sockaddr_in *sin, byte *pkt, int len, void *v)
{
	int key;
	Player *p;

	/* make sure the packet fits */
	if (len != 8 || pkt[0] != 0x00 || pkt[1] != 0x01 ||
			pkt[6] != 0x01 || pkt[7] != 0x00)
		return;

	/* ok, it fits. get connection. */
	p = net->NewConnection(T_VIE, sin, &_int, v);

	if (!p)
	{
		/* no slots left? */
		byte pkt[2] = { 0x00, 0x07 };
		net->ReallyRawSend(sin, (byte*)&pkt, 2, v);
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
		net->ReallyRawSend(sin, (byte*)&pkt, sizeof(pkt), v);
	}

	/* init encryption tables */
	Init(p, key);
}


local void do_init(EncData *ed, int k)
{
	int t, loop;
	short *mytable;

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

void Init(Player *p, int k)
{
	EncData *ed, **p_ed = PPDATA(p, enckey);
	
	pthread_mutex_lock(&mtx);
	if (!(ed = *p_ed)) ed = *p_ed = amalloc(sizeof(*ed));
	pthread_mutex_unlock(&mtx);

	do_init(ed, k);
}


local int do_enc(EncData *ed, byte *data, int len)
{
	int work = ed->key, *mytable = (int*)ed->table;
	int loop, until, *mydata;
	
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

int Encrypt(Player *p, byte *data, int len)
{
	EncData *ed, **p_ed = PPDATA(p, enckey);
	pthread_mutex_lock(&mtx);
	ed = *p_ed;
	pthread_mutex_unlock(&mtx);
	return ed ? do_enc(ed, data, len) : len;
}


int do_dec(EncData *ed, byte *data, int len)
{
	int work = ed->key, *mytable = (int*)ed->table;
	int *mydata, loop, until;

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
		int tmp = mydata[loop];
		mydata[loop] = mytable[loop] ^ work ^ tmp;
		work = tmp;
	}
	return len;
}

int Decrypt(Player *p, byte *data, int len)
{
	EncData *ed, **p_ed = PPDATA(p, enckey);
	pthread_mutex_lock(&mtx);
	ed = *p_ed;
	pthread_mutex_unlock(&mtx);
	return ed ? do_dec(ed, data, len) : len;
}


void Void(Player *p)
{
	EncData *ed, **p_ed = PPDATA(p, enckey);
	pthread_mutex_lock(&mtx);
	ed = *p_ed;
	afree(ed);
	*p_ed = NULL;
	pthread_mutex_unlock(&mtx);
}

