
#define REWARD_DISABLED    0

// simple periodic scoring (when you want stats or access to the additional flag info)
#define REWARD_PERIODIC    1

// each team gets a fixed # of points based on 1st, 2nd, 3rd, ... place
#define REWARD_FIXED_PTS   2

// standard weighted scoring method
#define REWARD_STD         3

// standard reward + collection of arenas are scored together simultaneously as one
// note: when teams from arenas match their data is combined
// (ex: arena 'public 0' and 'public 1' both have freq 0)
// probable use: provide portals between arenas to transport player to other arena
#define REWARD_STD_MULTI   4


#define TR_WEIGHT_TIME    0
#define TR_WEIGHT_DINGS   1

