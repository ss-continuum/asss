
#ifndef __PACKETS_TIMESYNC_H
#define __PACKETS_TIMESYNC_H

/* timesync.h - time sync packets */

struct TimeSyncS2C
{
	i8 t1,t2;
	i32 clienttime,servertime;
};

struct TimeSyncC2S
{
	i8 t1,t2;
	i32 time,pktsent,pktrecvd;
};

#endif

