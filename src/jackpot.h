
#ifndef __JACKPOT_H
#define __JACKPOT_H

/* the jackpot is stored as per-arena, per-game, persistant data. this
 * means it will survive across arena destroy/creates, and even across
 * crashes. it also means it will automatically be reset at the end of
 * every "game", defined by calling persist->EndInterval(arena,
 * INTERVAL_GAME);, and the final jackpot before the end of the game
 * will be saved in the old game's data.
 */


#define I_JACKPOT "jackpot-1"

typedef struct Ijackpot
{
	INTERFACE_HEAD_DECL

	void (*ResetJP)(int arena);
	void (*AddJP)(int arena, int pts);
	int (*GetJP)(int arena);
} Ijackpot;

#endif

