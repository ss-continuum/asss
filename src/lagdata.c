
/* dist: public */

#include <string.h>

#include "asss.h"

#define MAX_PING 10000
#define PLOSS_MIN_PACKETS 20

local pthread_mutex_t mtx;
#define LOCK() pthread_mutex_lock(&mtx)
#define UNLOCK() pthread_mutex_unlock(&mtx)

#ifdef CFG_PEDANTIC_LOCKING
#define PEDANTIC_LOCK() LOCK()
#define PEDANTIC_UNLOCK() UNLOCK()
#else
#define PEDANTIC_LOCK()
#define PEDANTIC_UNLOCK()
#endif


/* we're going to keep track of ppk pings and rel delay pings
 * separately, because in some sense they're different protocols.
 *
 * pings will go in buckets so we can make histograms later. the buckets
 * will be 20 ms wide, and go from 0 up to 500 ms, so there will be 25
 * buckets.
 */

#ifdef CFG_LAG_BUCKETS
#define MAX_BUCKET CFG_LAG_BUCKETS
#else
#define MAX_BUCKET 25
#endif

#ifdef CFG_LAG_BUCKET_WIDTH
#define BUCKET_WIDTH CFG_LAG_BUCKET_WIDTH
#else
#define BUCKET_WIDTH 20
#endif

#define MS_TO_BUCKET(ms) \
( ((ms) < 0) ? 0 : ( ((ms) < (MAX_BUCKET * BUCKET_WIDTH)) ? ((ms) / BUCKET_WIDTH) : MAX_BUCKET ) )

struct PingData
{
	int buckets[MAX_BUCKET];
	/* these are all in milliseconds */
	int current, avg, max, min;
};

typedef struct
{
	struct PingData pping; /* position packet ping */
	struct PingData rping; /* reliable ping */
	struct ClientLatencyData cping; /* client-reported ping */
	struct ClientPLossData ploss; /* basic ploss info */
	struct ReliableLagData reldata; /* reliable layer data */
	unsigned int wpnsent, wpnrcvd;
} LagData;


/* an array of pointers to lagdata */
local LagData *data[MAXPLAYERS];



local void clear_lagdata(LagData *ld)
{
	memset(ld, 0, sizeof(*ld));
}

local LagData * new_lagdata()
{
	LagData *ld = amalloc(sizeof(*ld));
	clear_lagdata(ld);
	return ld;
}

local void free_lagdata(LagData *ld)
{
	afree(ld);
}


local void add_ping(struct PingData *pd, int ping)
{
	/* prevent horribly incorrect pings from messing up stats */
	if (ping > MAX_PING)
		ping = MAX_PING;

	pd->current = ping;
	pd->buckets[MS_TO_BUCKET(ping)]++;
	pd->avg = (pd->avg * 9 + ping) / 10;
	if (ping < pd->min)
		pd->min = ping;
	if (ping > pd->max)
		pd->max = ping;
}


local void Position(int pid, int ping, unsigned int wpnsent)
{
	PEDANTIC_LOCK();
	if (data[pid])
	{
		add_ping(&data[pid]->pping, ping);
		data[pid]->wpnsent = wpnsent;
	}
	PEDANTIC_UNLOCK();
}


local void RelDelay(int pid, int ping)
{
	PEDANTIC_LOCK();
	if (data[pid])
		add_ping(&data[pid]->rping, ping/2);
	PEDANTIC_UNLOCK();
}


local void ClientLatency(int pid, struct ClientLatencyData *d)
{
	PEDANTIC_LOCK();
	if (data[pid])
	{
		data[pid]->cping = *d;
		data[pid]->wpnrcvd = d->weaponcount;
	}
	PEDANTIC_UNLOCK();
}


local void ClientPLoss(int pid, struct ClientPLossData *d)
{
	PEDANTIC_LOCK();
	if (data[pid])
		data[pid]->ploss = *d;
	PEDANTIC_UNLOCK();
}


local void RelStats(int pid, struct ReliableLagData *d)
{
	PEDANTIC_LOCK();
	if (data[pid])
		data[pid]->reldata = *d;
	PEDANTIC_UNLOCK();
}



local void QueryPPing(int pid, struct PingSummary *p)
{
	PEDANTIC_LOCK();
	if (data[pid])
	{
		struct PingData *pd = &data[pid]->pping;
		p->cur = pd->current;
		p->avg = pd->avg;
		p->min = pd->min;
		p->max = pd->max;
	}
	else
		memset(p, 0, sizeof(*p));
	PEDANTIC_UNLOCK();
}


local void QueryCPing(int pid, struct PingSummary *p)
{
	PEDANTIC_LOCK();
	if (data[pid])
	{
		p->cur = data[pid]->cping.lastping;
		p->avg = data[pid]->cping.averageping;
		p->min = data[pid]->cping.lowestping;
		p->max = data[pid]->cping.highestping;
		/* special stuff for client ping: */
		p->s2cslowtotal = data[pid]->cping.s2cslowtotal;
		p->s2cfasttotal = data[pid]->cping.s2cfasttotal;
		p->s2cslowcurrent = data[pid]->cping.s2cslowcurrent;
		p->s2cfastcurrent = data[pid]->cping.s2cfastcurrent;
	}
	else
		memset(p, 0, sizeof(*p));
	PEDANTIC_UNLOCK();
}


local void QueryRPing(int pid, struct PingSummary *p)
{
	PEDANTIC_LOCK();
	if (data[pid])
	{
		struct PingData *pd = &data[pid]->rping;
		p->cur = pd->current;
		p->avg = pd->avg;
		p->min = pd->min;
		p->max = pd->max;
	}
	else
		memset(p, 0, sizeof(*p));
	PEDANTIC_UNLOCK();
}



local void QueryPLoss(int pid, struct PLossSummary *d)
{
	PEDANTIC_LOCK();
	if (data[pid])
	{
		int s, r;
		s = data[pid]->ploss.s_pktsent;
		r = data[pid]->ploss.c_pktrcvd;
		d->s2c = s > PLOSS_MIN_PACKETS ? (double)(s - r) / (double)s : 0.0;

		s = data[pid]->ploss.c_pktsent;
		r = data[pid]->ploss.s_pktrcvd;
		d->c2s = s > PLOSS_MIN_PACKETS ? (double)(s - r) / (double)s : 0.0;

		s = data[pid]->wpnsent;
		r = data[pid]->wpnrcvd;
		d->s2cwpn = s > PLOSS_MIN_PACKETS ? (double)(s - r) / (double)s : 0.0;
	}
	else
		memset(d, 0, sizeof(*d));
	PEDANTIC_UNLOCK();
}


local void QueryRelLag(int pid, struct ReliableLagData *d)
{
	PEDANTIC_LOCK();
	if (data[pid])
		*d = data[pid]->reldata;
	else
		memset(d, 0, sizeof(*d));
	PEDANTIC_UNLOCK();
}


local void do_hist(
		int pid,
		struct PingData *pd,
		void (*callback)(int pid, int bucket, int count, int maxcount, void *clos),
		void *clos)
{
	int i, max = 0;

	for (i = 0; i < MAX_BUCKET; i++)
		if (pd->buckets[i] > max)
			max = pd->buckets[i];

	for (i = 0; i < MAX_BUCKET; i++)
		callback(pid, i, pd->buckets[i], max, clos);
}

local void DoPHistogram(int pid,
		void (*callback)(int pid, int bucket, int count, int maxcount, void *clos),
		void *clos)
{
	PEDANTIC_LOCK();
	if (data[pid])
		do_hist(pid, &data[pid]->pping, callback, clos);
	PEDANTIC_UNLOCK();
}

local void DoRHistogram(int pid,
		void (*callback)(int pid, int bucket, int count, int maxcount, void *clos),
		void *clos)
{
	PEDANTIC_LOCK();
	if (data[pid])
		do_hist(pid, &data[pid]->rping, callback, clos);
	PEDANTIC_UNLOCK();
}


local void paction(int pid, int action, int arena)
{
	if (action == PA_CONNECT)
	{
		LOCK();

		if (data[pid])
		{
			/* lm->LogP(L_WARN, "lagdata", pid, "LagData struct already exists"); */
			free_lagdata(data[pid]);
		}

		data[pid] = new_lagdata();

		UNLOCK();
	}
	else if (action == PA_DISCONNECT)
	{
		LOCK();

		if (!data[pid])
		{
			/* lm->LogP(L_WARN, "lagdata", pid, "No LagData struct exists"); */
		}
		else
			free_lagdata(data[pid]);

		data[pid] = NULL;

		UNLOCK();
	}
}



local Ilagcollect lcint =
{
	INTERFACE_HEAD_INIT(I_LAGCOLLECT, "lagdata")
	Position, RelDelay, ClientLatency,
	ClientPLoss, RelStats
};


local Ilagquery lqint =
{
	INTERFACE_HEAD_INIT(I_LAGQUERY, "lagdata")
	QueryPPing, QueryCPing, QueryRPing,
	QueryPLoss, QueryRelLag,
	DoPHistogram, DoRHistogram
};


EXPORT int MM_lagdata(int action, Imodman *mm, int arena)
{
	if (action == MM_LOAD)
	{
		pthread_mutex_init(&mtx, NULL);

		mm->RegCallback(CB_PLAYERACTION, paction, ALLARENAS);

		mm->RegInterface(&lcint, ALLARENAS);
		mm->RegInterface(&lqint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&lcint, ALLARENAS))
			return MM_FAIL;
		if (mm->UnregInterface(&lqint, ALLARENAS))
			return MM_FAIL;
		mm->UnregCallback(CB_PLAYERACTION, paction, ALLARENAS);
		pthread_mutex_destroy(&mtx);
		return MM_OK;
	}
	return MM_FAIL;
}

