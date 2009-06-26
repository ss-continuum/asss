
/* dist: public */

#ifndef __BALLS_H
#define __BALLS_H

#define MAXBALLS 8

/* Iballs
 * this module will handle all ball-related network communication.
 */


typedef enum
{
	/* pyconst: enum, "BALL_*" */
	BALL_NONE,    /* the ball doesn't exist */
	BALL_ONMAP,   /* the ball is on the map or has been fired */
	BALL_CARRIED, /* the ball is being carried */
	BALL_WAITING  /* the ball is waiting to be spawned again */
} ballstate_t;


/* called when a player picks up a ball */
#define CB_BALLPICKUP "ballpickup"
typedef void (*BallPickupFunc)(Arena *arena, Player *p, int bid);
/* pycb: arena, player, int */

/* called when a player fires a ball */
#define CB_BALLFIRE "ballfire"
typedef void (*BallFireFunc)(Arena *arena, Player *p, int bid);
/* pycb: arena, player, int */

/* called when a player scores a goal */
#define CB_GOAL "goal"
typedef void (*GoalFunc)(Arena *arena, Player *p, int bid, int x, int y);
/* pycb: arena, player, int, int, int */


struct BallData
{
	/* pytype: struct, struct BallData, balldata */

	/* the state of this ball */
	int state;

	/* the coordinates of the ball */
	int x, y, xspeed, yspeed; 

	/* the player that is carrying or last touched the ball */
	Player *carrier;

	/* freq of carrier */
	int freq;

	/* the time that the ball was last fired (will be 0 for
	 * balls being held). for BALL_WAITING, this time is the
	 * time when the ball will be re-spawned. */
	ticks_t time;

	/* the time the server last got an update on ball data.
	 * it might differ from the 'time' field due to lag. */
	ticks_t last_update;
};

typedef struct ArenaBallData
{
	/* the number of balls currently in play. 0 if the arena has no ball game. */
	int ballcount;

	/* points to an array of at least ballcount structs */
	struct BallData *balls;
} ArenaBallData;


#define I_BALLS "balls-4"

typedef struct Iballs
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	void (*SetBallCount)(Arena *arena, int ballcount);
	/* sets the number of balls in the arena. if the new count is higher
	 * than the current one, new balls are spawned. if it's lower, the
	 * dead balls are "phased" in the upper left corner. */
	/* pyint: arena, int -> void  */

	void (*PlaceBall)(Arena *arena, int bid, struct BallData *newpos);
	/* sets the parameters of the ball to those in the given BallData
	 * struct */
	/* pyint: arena, int, balldata -> void */

	void (*EndGame)(Arena *arena);
	/* ends the ball game */
	/* pyint: arena -> void  */

	void (*SpawnBall)(Arena *arena, int bid);
	/* respawns the specified ball. no effect on balls that don't exist. */
	/* pyint: arena, int -> void */

	ArenaBallData * (*GetBallData)(Arena *arena);
	void (*ReleaseBallData)(Arena *arena);
	/* always release the ball data when you're done using it */
} Iballs;


#endif

