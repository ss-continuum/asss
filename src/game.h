
#ifndef __GAME_H
#define __GAME_H

/* these callbacks will be called whenever a kill occurs */

#define CALLBACK_KILL ("kill")

typedef void (*KillFunc)(int arena, int killer, int killed, int bounty, int flags);
/* flags will be zero for now */

#endif

