
#ifndef __PACKETS_SCOREUPD_H
#define __PACKETS_SCOREUPD_H

struct ScorePacket
{
	i8 type;
	i16 pid;
	i32 killpoints, flagpoints;
	i16 kills, deaths;
};

#endif

