
// dist: public

#define REWARD_DISABLED  0

// simple periodic scoring (when you want stats or access to the additional flag info)
#define REWARD_PERIODIC  1

// each team gets a fixed # of points based on 1st, 2nd, 3rd, ... place
#define REWARD_FIXED_PTS 2

// standard weighted scoring method
#define REWARD_STD       3

// standard reward + collection of arenas are scored together simultaneously as one
// note: when teams from arenas match their data is combined
// (ex: arena 'public 0' and 'public 1' both have freq 0)
// probable use: provide portals between arenas to transport player to other arena
#define REWARD_STD_MULTI 4


#define TR_WEIGHT_DINGS 0 // weight calculation based on dings
#define TR_WEIGHT_TIME  1 // weight calculation based on time


#define TR_RECOVERY_DINGS          0 // recovery cutoff based on RecoverDings
#define TR_RECOVERY_TIME           1 // recovery cutoff based on RecoverTime
#define TR_RECOVERY_DINGS_AND_TIME 2 // recovery cutoff based on both RecoverDings and RecoverTime

