
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

#endif

