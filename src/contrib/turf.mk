
# dist: public

ALL_STUFF += contrib/turf.so

contrib/turf.so: contrib/turf_reward.o contrib/points_turf_reward.o \
	contrib/turf_stats.o $(DL_UTIL_OBJS)

