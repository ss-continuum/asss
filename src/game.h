
/* dist: public */

#ifndef __GAME_H
#define __GAME_H

/* these callbacks will be called whenever a kill occurs */
#define CB_KILL "kill"
typedef void (*KillFunc)(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts);
/* FIXMEpycb: arena, player, player, int, int, int inout */


/* this will be called when a player changes his freq (but stays in the
 * same ship */
#define CB_FREQCHANGE "freqchange"
typedef void (*FreqChangeFunc)(Player *p, int newfreq);
/* pycb: player, int */


/* this will be called when a player changes ship */
#define CB_SHIPCHANGE "shipchange"
typedef void (*ShipChangeFunc)(Player *p, int newship, int newfreq);
/* pycb: player, int, int */


/* this is called when the game timer expires */
#define CB_TIMESUP "timesup"
typedef void (*GameTimerFunc)(Arena *arena);
/* pycb: arena */


/* this is called when someone enters or leaves a safe zone. x and y are
 * in pixels. entering is true if entering, false if exiting. */
#define CB_SAFEZONE "safezone"
typedef void (*SafeZoneFunc)(Player *p, int x, int y, int entering);
/* pycb: player, int, int, int */

/* these should be mostly self-explanatory. */

#define I_GAME "game-6"

typedef struct Igame
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	void (*SetFreq)(Player *p, int freq);
	/* pyint: player, int -> void */
	void (*SetShip)(Player *p, int ship);
	/* pyint: player, int -> void */
	void (*SetFreqAndShip)(Player *p, int ship, int freq);
	/* pyint: player, int, int -> void */
	void (*WarpTo)(const Target *target, int x, int y);
	/* pyint: target, int, int -> void */
	void (*GivePrize)(const Target *target, int type, int count);
	/* pyint: target, int, int -> void */

	void (*Lock)(const Target *t, int notify, int spec, int timeout);
	/* pyint: target, int, int, int -> void */
	void (*Unlock)(const Target *t, int notify);
	/* pyint: target, int -> void */
	void (*LockArena)(Arena *a, int notify, int onlyarenastate, int initial, int spec);
	/* pyint: arena, int, int, int, int -> void */
	void (*UnlockArena)(Arena *a, int notify, int onlyarenastate);
	/* pyint: arena, int, int -> void */

	void (*FakePosition)(Player *p, struct C2SPosition *pos, int len);
	void (*FakeKill)(Player *killer, Player *killed, int pts, int flags);
} Igame;


#endif

