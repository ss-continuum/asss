
/* dist: public */

#ifndef __TURF_REWARD_H
#define __TURF_REWARD_H

/*
 * Iturfrewardpoints - score calculation interface for turf_reward
 * other modules can define other scoring algorithms using data from
 * the turf_reward data structure by registering with this interface
 */

/*
 * Iturfreward - interface for turf_reward
 * used by points_turf_reward for multiarena scoring locking/unlocking
 */


/* prototypes */
struct TurfArena;
struct Iturfrewardpoints;


// called for all turf flag tags
#define CB_TURFTAG ("turftag")
typedef void (*TurfTagFunc)(Arena *arena, Player *p, int fid);

// called when a flag is 'recovered' (note: CB_TURFTAG will still be called)
// possible use would be to have a module that manipulates lvz objects telling player that the flag tagged was recovered
#define CB_TURFRECOVER ("turfrecover")
typedef void (*TurfRecoverFunc)(Arena *arena, int fid, int pid, int freq, int dings, int weight, int recovered);

// called when a flag is 'lost' (note: CB_TURFTAG will still be called)
// possible use would be to have a module that manipulates lvz objects telling players that a flag was lost
#define CB_TURFLOST ("turflost")
typedef void (*TurfLostFunc)(Arena *arena, int fid, int pid, int freq, int dings, int weight, int recovered);


// this is a special callback - turf_arena is LOCKED when this is called
// called AFTER players are awarded points (good time for history stuff / stats output)
#define CB_TURFPOSTREWARD ("turfpostreward")
typedef void (*TurfPostRewardFunc)(Arena *arena, struct TurfArena *ta);


/*
// called during a flag game victory
#define CB_TURFVICTORY ("turfvictory")
typedef void (*TurfVictoryFunc) (Arena *arena)
*/


// for linked list for data on teams that have a chance to 'recover'
struct OldNode
{
	int lastOwned;   // how many dings ago the flag was owned

	int freq;        // previous team that owned the flag
	int dings;       // previous # of dings
	int weight;      // previous weight of flag
	int taggerPID;   // pid of player that owned the flag last
	int recovered;   // number of times was recovered
	ticks_t tagTC;   // time flag was originally tagged in ticks
	ticks_t lostTC;  // time flag was lost in ticks
};

// to hold extra flag data for turf flags
struct TurfFlag
{
	int freq;        // freq of player that last tagged flag
	int dings;       // number of dings the flag has been owned for
	int weight;      // weight of the flag (how much it's worth)
	int taggerPID;   // id of player that tagged the flag
	                 // note: player may have been on another team when tag occured or may have even left the game
	int recovered;   // number of times flag was recovered
	ticks_t tagTC;   // time flag was originally tagged in ticks
	ticks_t lastTC;  // time flag was last tagged in ticks

	LinkedList old;  // linked list of OldNodes storing data of flag's previous owners who have a chance to 'recover' it
};

struct FreqInfo
{
	int freq;                       // freq number
	
	int numFlags;                   // number of flags freq owns
	double percentFlags;            // percent of the flags owned

	long int numWeights;            // sum of weights for owned flags
	double percentWeights;          // percent of the total weights

	unsigned int numTags;           // # of flag tags
	unsigned int numRecovers;       // # of flag recoveries
	unsigned int numLost;           // # of flags lost

	int numPlayers;                 // # of players on the freq
	double perCapita;               // weights per player on freq
	double percent;                 // percent of jackpot to recieve
	unsigned int numPoints;         // number of points to award to freq
};

struct TurfArena
{
	// cfg settings for turf reward
	int reward_style;               // change reward algorithms
	void *multi_arena_id;             // when using a multi arena algorithm, arenas with matching id's are scored together
	int min_players_on_freq;        // min # of players needed on a freq for that freq to recieve reward pts
	int min_players_in_arena;       // min # of players needed in the arena for anyone to recieve reward pts
	int min_teams;                  // min # of teams needed for anyone to recieve reward pts
	int min_flags;                  // min # of flags needed to be owned by freq in order to recieve reward pts
	double min_percent_flags;       // min % of flags needed to be owned by freq in order to recieve reward pts
	int min_weights;                // min # of weights needed to be owned by freq in order to recieve reward pts
	double min_percent_weights;     // min % of weights needed to be owned by freq in order to recieve reward pts
	double min_percent;             // min percentage of jackpot needed to recieve an award
	int jackpot_modifier;           // modifies the jackpot based on how many points per player playing
	int max_points;                 // maximum # of points to award a single person

	int recovery_cutoff;            // recovery cutoff style to be used
	int recover_dings;
	int recover_time;
	int recover_max;

	int weight_calc;
	int set_weights;                // number of weights that were set from cfg
	int *weights;                   // array of weights from cfg

	// int min_kills_arena;         // todo: minimum # of kills needed for anyone to recieve rewards
	// int min_kills_freq;          // todo: minimum # of kills needed by a freq for that freq to recieve rewards
	// int min_tags_arena;          // todo: minimum # of tags needed in arena for anyone to recieve rewards
	// int min_tags_freq;           // todo: minimum # of tags needed by a freq for that freq to recieve rewards

	// data for timer
	ticks_t dingTime;               // time of last ding
	int timer_initial;              // initial timer delay
	int timer_interval;             // interval for timer to repeat
	struct Iturfrewardpoints *trp;  // turf reward scoring interface

	// reward data
	int numFlags;                   // number of flags on the map
	int numPlayers;                 // number of people playing (not including spectators)
	int numTeams;                   // number of teams (not including ones with < MinPlayersOnFreq)
	long int numWeights;            // the complete number of flag weights
	unsigned long int numPoints;    // number of points to split up
	double sumPerCapitas;           // sum of all teams percapitas

	unsigned int numTags;           // number of flag tags during reward interval
	unsigned int numLost;           // number of flag losses during reward interval
	unsigned int numRecovers;       // number of flag recoveries during reward interval
	//unsigned int numKills;        // todo: number of kills during reward interval

	struct TurfFlag *flags;         // pointer to array of turf flags
	LinkedList freqs;               // linked list of FreqInfo
};

typedef enum
{
	/* single arena codes */
	TR_AWARD_UPDATE,             // do award and update
	TR_AWARD_ONLY,               // do award only
	TR_UPDATE_ONLY,              // do update only
	TR_NO_AWARD_NO_UPDATE,       // don't do award or update
	TR_FAIL_REQUIREMENTS,        // arena failed minimum requirements, update only
	TR_FAIL_CALCULATIONS,        // error while doing calculations, no award or update
	
	/* multi arena codes */
	TR_AWARD_UPDATE_MULTI,
	TR_AWARD_ONLY_MULTI,
	TR_UPDATE_ONLY_MULTI,
	TR_NO_AWARD_NO_UPDATE_MULTI,
	TR_FAIL_REQUIREMENTS_MULTI
} trstate_t;


#define I_TURFREWARD_POINTS "turfreward-points"
typedef struct Iturfrewardpoints
{
	INTERFACE_HEAD_DECL
	trstate_t (*CalcReward)(Arena *arena, struct TurfArena *tr);
	/* This will be called by the turf_reward module for each arena that
	 * exists when points should be awarded. It should figure out and
	 * fill in the many stats for each freq. The ta->freqs linked list
	 * already has as a node: every freq that owns flags OR has players.
	 * The ta->freqs linked list is what turf_reward searches through
	 * when awarding points.  If the module that registered with this
	 * interface did not fill in data for that freq anyone on that team
	 * will NOT recieve any points.  Ideally the module that registers
	 * this interface will fill in numPoints for ALL the teams.
	 * Obviously, to not award a freq points, set numPoints=0.  The
	 * module has access to ALL arena's data for the arena through *tr.
	 * The arena is already locked when this function is is called.
	 *
	 * Note: for multi-arena scoring, only the arena that called the
	 * timer is assured to have the freqs linked list initialized.  This
	 * means you have to go through the flags array and the playerdata
	 * array for every arena with the same multi arena id in order to
	 * figure out what freqs exist.
	 *
	 * params:
	 *     arena - specifies which arena it is scoring for
	 *     *tr   - points to the TurfReward data for the specific arena
	 *
	 * The return code tells turf_reward what to do after the function
	 * is called See enum declaration of trstate_t for more info on each
	 * code
	 */
} Iturfrewardpoints;


#define I_TURFREWARD "turfreward-1"
typedef struct Iturfreward
{
	INTERFACE_HEAD_DECL

	void (*ResetFlagGame)(Arena *arena);
	/* a utility function to reset all flag data INCLUDING flag data in
	 * the flags module */

	void (*ResetTimer)(Arena *arena);
	/* a utility function to reset the ding timer for an arena */

	void (*DoReward)(Arena *arena);
	/* a utility function to force a ding to occur immedately */

	struct TurfArena * (*GetTurfData)(Arena *arena);
	void (*ReleaseTurfData)(Arena *arena);
	/* gets turf data for an arena. always release it when you're done */
} Iturfreward;


#endif

