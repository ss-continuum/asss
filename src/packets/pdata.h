
/* dist: public */

#ifndef __PACKETS_PDATA_H
#define __PACKETS_PDATA_H

/* pdata.h - player data packet */

typedef struct PlayerData
{
	u8 pktype;
	u8 ship;
	u8 acceptaudio;
	char name[20];
	char squad[20];
	i32 killpoints;
	i32 flagpoints;
	i16 pid;
	i16 freq;
	i16 wins;
	i16 losses;
	i16 attachedto;
	i16 flagscarried;
	u8 miscbits;
} PlayerData;


/* flag bits for the miscbits field */

/* whether the player has a crown */
#define F_HAS_CROWN 0x01
#define SET_HAS_CROWN(pid) (p->pkt.miscbits |= F_HAS_CROWN)
#define UNSET_HAS_CROWN(pid) (p->pkt.miscbits &= ~F_HAS_CROWN)

/* whether clients should send data for damage done to this player */
#define F_SEND_DAMAGE 0x02
#define SET_SEND_DAMAGE(pid) (p->pkt.miscbits |= F_SEND_DAMAGE)
#define UNSET_SEND_DAMAGE(pid) (p->pkt.miscbits &= ~F_SEND_DAMAGE)


#endif


