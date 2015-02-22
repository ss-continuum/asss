
/* dist: public */

#ifndef __FG_TURF_H
#define __FG_TURF_H

/* run when a turf flag is tagged and changes ownership. note that
 * CB_FLAGONMAP will also run (since the flag state has changed), but
 * this provides more information (the tagging player and the previous
 * freq). this callback is generated by the fg_turf module, while
 * CB_FLAGONMAP is generated by the flagcore module.
 * @threading called from main
 */
#define CB_TURFTAG "turftag"
typedef void (*TurfTagFunc)(Arena *a, Player *p, int fid, int oldfreq, int newfreq);
/* pycb: arena_not_none, player_not_none, int, int, int */

#endif

