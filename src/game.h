
#ifndef __GAME_H
#define __GAME_H

/* these callbacks will be called whenever a kill occurs */
#define CB_KILL ("kill")
typedef void (*KillFunc)(int arena, int killer, int killed, int bounty, int flags);


/* this will be called when a player changes his freq (but stays in the
 * same ship */
#define CB_FREQCHANGE ("freqchange")
typedef void (*FreqChangeFunc)(int pid, int newfreq);


/* this will be called when a player changes ship */
#define CB_SHIPCHANGE ("shipchange")
typedef void (*ShipChangeFunc)(int pid, int newship, int newfreq);

#define CB_TIMESUP ("timesup")
typedef void (*GameTimerFunc)(int arena);


#define I_GAME "game-2"

typedef struct Igame
{
	INTERFACE_HEAD_DECL

	void (*SetFreq)(int pid, int freq);
	/* arpc: void(int, int) */
	void (*SetShip)(int pid, int ship);
	/* arpc: void(int, int) */
	void (*SetFreqAndShip)(int pid, int ship, int freq);
	/* arpc: void(int, int, int) */
	void (*DropBrick)(int arena, int freq, int x1, int y1, int x2, int y2);
	/* arpc: void(int, int, int, int, int, int) */
	void (*WarpTo)(const Target *target, int x, int y);
	/* arpc: void(int, int, int) */
	void (*GivePrize)(const Target *target, int type, int count);
	/* arpc: void(int, int, int) */
} Igame;


#endif

