
#ifndef __BALLS_H
#define __BALLS_H

/* Iballs
 * this module will handle all ball-related network communication.
 */


typedef enum
{
	BALL_NONE,    /* the ball doesn't exist */
	BALL_ONMAP,   /* the ball is on the map or has been fired */
	BALL_CARRIED  /* the ball is being carried */
} ballstate_t;


/* called when a player picks up a ball */
#define CALLBACK_BALLPICKUP ("ballpickup")
typedef void (*BallPickupFunc)(int arena, int pid, int bid);

/* called when a player fires a ball */
#define CALLBACK_BALLFIRE ("ballfire")
typedef void (*BallFireFunc)(int arena, int pid, int bid);

/* called when a player scores a goal */
#define CALLBACK_GOAL ("goal")
typedef void (*GoalFunc)(int arena, int pid, int bid);


struct BallData
{
	ballstate_t state; /* the state of this ball */
	int x, y, xspeed, yspeed; /* the coordinates of the ball */
	int carrier; /* the pid that is carrying or last touched the ball */
	u32 time; /* the time that the ball was last fired (will be 0
	             for balls being held) */
};

struct ArenaBallData
{
	int ballcount;
	/* the number of balls currently in play. 0 if the arena has no ball
	 * game. */
	struct BallData *balls;
	/* points to an array of at least ballcount structs */
};


typedef struct Iballs
{
	void (*SetBallCount)(int arena, int ballcount);
	/* sets the number of balls in the arena. if the new count is higher
	 * than the current one, new balls are spawned. if it's lower, the
	 * dead balls are "phased" in the upper left corner. */

	void (*PlaceBall)(int arena, int bid, struct BallData *newpos);
	/* sets the parameters of the ball to those in the given BallData
	 * struct */

	void (*BallVictory)(int arena, int freq, int points);
	/* ends the ball game (freq=-1 to reset balls with no winner) */

	void (*LockBallStatus)(int arena);
	void (*UnlockBallStatus)(int arena);
	/* since the following array is global data, access must be
	 * controlled by a mutex. */

	struct ArenaBallData *balldata; /* indexed by arena */
} Iballs;


#endif

