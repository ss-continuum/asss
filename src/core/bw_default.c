
/* dist: public */

#include <string.h>
#include <stdio.h>
#include <math.h>

#include "asss.h"
#include "bwlimit.h"

struct BWLimit
{
	/* sincetime is in millis, not ticks */
	ticks_t sincetime;
	int limit;
	int bytes_since[BW_PRIS];
	int hitlimit;
};

/* the limits on the bandwidth limit */
local int limit_low, limit_high;

/* this array represent the percentage of traffic that is allowed to
 * be at or lower than each priority level. */
local int pri_limits[BW_PRIS];

/* we need to know how many packets the client is able to buffer */
local int client_can_buffer;

/* various other settings */
local int limitscale;
local int use_hitlimit;


local BWLimit * New()
{
	BWLimit *bw = amalloc(sizeof(*bw));
	bw->limit = limit_low; /* start slow */
	bw->hitlimit = 0;
	bw->sincetime = current_millis();
	return bw;
}


local void Free(BWLimit *bw)
{
	afree(bw);
}


local void Iter(BWLimit *bw, ticks_t now)
{
	const int granularity = 8;
	int pri, slices = 0;
	while (TICK_DIFF(now, bw->sincetime) > (1000/granularity))
	{
		slices++;
		bw->sincetime = TICK_MAKE(bw->sincetime + (1000/granularity));
	}
	for (pri = 0; pri < BW_PRIS; pri++)
	{
		bw->bytes_since[pri] -= slices * (bw->limit * pri_limits[pri] / 100) / granularity;
		if (bw->bytes_since[pri] < 0)
			bw->bytes_since[pri] = 0;
	}
}


local int Check(BWLimit *bw, int bytes, int pri)
{
	int bs_copy[BW_PRIS];
	int p;
	memcpy(bs_copy, bw->bytes_since, sizeof(bs_copy));
	for (p = pri; p >= 0; p--)
	{
		int slack = pri_limits[p] * bw->limit / 100 - bs_copy[p];
		if (slack >= bytes)
		{
			bs_copy[p] += bytes;
			memcpy(bw->bytes_since, bs_copy, sizeof(bs_copy));
			return TRUE;
		}
		else if (slack > 0)
		{
			bs_copy[p] += slack;
			bytes -= slack;
		}
	}
	bw->hitlimit = TRUE;
	return FALSE;
}


local void AdjustForAck(BWLimit *bw)
{
	if (use_hitlimit && bw->hitlimit)
	{
		bw->limit += 4 * limitscale * limitscale / bw->limit;
		bw->hitlimit = 0;
	}
	else
		bw->limit += limitscale * limitscale / bw->limit;

	CLIP(bw->limit, limit_low, limit_high);
}


local void AdjustForRetry(BWLimit *bw)
{
	bw->limit += sqrt(bw->limit * bw->limit - 4 * limitscale * limitscale);
	bw->limit /= 2;
	CLIP(bw->limit, limit_low, limit_high);
}


local int GetCanBufferPackets(BWLimit *bw)
{
	int cansend = bw->limit / 512;
	CLIP(cansend, 1, client_can_buffer);
	return cansend;
}

local void GetInfo(BWLimit *bw, char *buf, int buflen)
{
	snprintf(buf, buflen, "%d bytes/s", bw->limit);
}


local struct Ibwlimit bwint =
{
	INTERFACE_HEAD_INIT(I_BWLIMIT, "bw-default")
	New, Free,
	Iter, Check,
	AdjustForAck, AdjustForRetry,
	GetCanBufferPackets, GetInfo
};

EXPORT int MM_bw_default(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		Iconfig * cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		if (!cfg)
			return MM_FAIL;

		limit_low = cfg->GetInt(GLOBAL, "Net", "LimitMinimum", 2500);
		limit_high = cfg->GetInt(GLOBAL, "Net", "LimitMaximum", 102400);
		client_can_buffer = cfg->GetInt(GLOBAL, "Net", "SendAtOnce", 30);
		limitscale = cfg->GetInt(GLOBAL, "Net", "LimitScale", MAXPACKET * 1);
		use_hitlimit = cfg->GetInt(GLOBAL, "Net", "UseHitLimit", 0);
		/* these may need some tweaking */
		pri_limits[0] = cfg->GetInt(GLOBAL, "Net", "PriLimit0", 20); /* low pri unrel */
		pri_limits[1] = cfg->GetInt(GLOBAL, "Net", "PriLimit1", 40); /* reg pri unrel */
		pri_limits[2] = cfg->GetInt(GLOBAL, "Net", "PriLimit2", 20); /* high pri unrel */
		pri_limits[3] = cfg->GetInt(GLOBAL, "Net", "PriLimit3", 15); /* rel */
		pri_limits[4] = cfg->GetInt(GLOBAL, "Net", "PriLimit4",  5); /* ack */

		mm->ReleaseInterface(cfg);

		mm->RegInterface(&bwint, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterface(&bwint, ALLARENAS);
		return MM_OK;
	}
	return MM_FAIL;
}

