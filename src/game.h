
/* dist: public */

#ifndef __GAME_H
#define __GAME_H

/* these callbacks will be called whenever a kill occurs */
#define CB_KILL ("kill")
typedef void (*KillFunc)(Arena *arena, Player *killer, Player *killed, int bounty, int flags);


/* this will be called when a player changes his freq (but stays in the
 * same ship */
#define CB_FREQCHANGE ("freqchange")
typedef void (*FreqChangeFunc)(Player *p, int newfreq);


/* this will be called when a player changes ship */
#define CB_SHIPCHANGE ("shipchange")
typedef void (*ShipChangeFunc)(Player *p, int newship, int newfreq);


/* this is called when the game timer expires */
#define CB_TIMESUP ("timesup")
typedef void (*GameTimerFunc)(Arena *arena);


#define I_GAME "game-3"

typedef struct Igame
{
	INTERFACE_HEAD_DECL

	void (*SetFreq)(Player *p, int freq);
	void (*SetShip)(Player *p, int ship);
	void (*SetFreqAndShip)(Player *p, int ship, int freq);
	void (*DropBrick)(Arena *arena, int freq, int x1, int y1, int x2, int y2, unsigned time);
	void (*WarpTo)(const Target *target, int x, int y);
	void (*GivePrize)(const Target *target, int type, int count);
} Igame;


#endif

