
ASSSHOME = ..
SCRIPTS = $(ASSSHOME)/scripts

# change this to the directory of your bdb 4.x install
DB_HOME = /opt/db-4.0.14
DB_INC = $(DB_HOME)/include
DB_LIB = $(DB_HOME)/lib
DB_CFLAGS = -I$(DB_INC)
DB_LDFLAGS = -L$(DB_LIB) -Wl,-rpath,$(DB_LIB) -ldb-4

# change this to the directory of your mysql install
MYSQL_HOME = /opt/mysql
MYSQL_INC = $(MYSQL_HOME)/include
MYSQL_LIB = $(MYSQL_HOME)/lib
MYSQL_CFLAGS = -I$(MYSQL_INC)
MYSQL_LDFLAGS = -L$(MYSQL_LIB) -Wl,-rpath,$(MYSQL_LIB) -lmysqlclient_r

LDFLAGS = -fPIC $(PROF_FLAGS)
#PROF_FLAGS = -pg

#OPT_CFLAGS = -O2 -fomit-frame-pointer -Wall -fpedantic
DEBUG_CFLAGS = -g -Wall -fpedantic
ALL_CFLAGS = -D_REENTRANT -D_GNU_SOURCE -fPIC $(DB_CFLAGS) $(MYSQL_CFLAGS)

CFLAGS = $(ALL_CFLAGS) $(DEBUG_CFLAGS) $(PROF_FLAGS) $(OPT_CFLAGS)

CC = gcc -pipe

ALL_STUFF = asss         \
            dbtool       \
            core.so      \
            loggers.so   \
            game.so      \
            command.so   \
            external.so  \
            scoring.so   \
            admin.so     \
            database.so  \
            turf.so      \
            funky.so



# comment out 'install' here to not replace binaries by default
all: $(ALL_STUFF) install

opt: clean
	$(MAKE) "CFLAGS=$(ALL_CFLAGS) $(OPT_CFLAGS) $(PROF_FLAGS)"

asss: main.o app.o module.o util.o pathutil.o protutil.o region.o
	$(CC) $(LDFLAGS) -rdynamic -o $@ $^ -ldl -lpthread

dbtool: dbtool.o statcodes.o util.c
	$(CC) $(LDFLAGS) -DNOMPQUEUE -o $@ $^ $(DB_LDFLAGS)

core.so: config.o player.o core.o logman.o mainloop.o net.o \
	nullenc.o encrypt1.o arenaman.o mapdata.o mapnewsdl.o clientset.o \
	capman.o lagdata.o lagaction.o
	$(CC) $(LDFLAGS) -shared -o $@ $^ -lz

loggers.so: log_file.o log_console.o log_sysop.o

game.so: game.o game_timer.o chat.o flags.o balls.o fm_normal.o \
	banners.o objects.o messages.o koth.o
	$(CC) $(LDFLAGS) -shared -o $@ $^ -lm

command.so: cmdman.o playercmd.o admincmd.o watchdamage.o buy.o help.o \
	cfghelp.o

external.so: directory.o billing.o billing_ssc.o

scoring.so: persist.o stats.o statcodes.o points_kill.o points_flag.o \
	points_goal.o jackpot.o periodic.o points_periodic.o basicstats.o
	$(CC) $(LDFLAGS) -shared -o $@ $^ $(DB_LDFLAGS)
	#$(CC) $(LDFLAGS) -shared -o $@ $^ $(DB_LIB)/libdb.a

admin.so: filetrans.o quickfix.o

database.so: mysql.o aliasdb.o
	$(CC) $(LDFLAGS) -shared -o $@ $^ $(MYSQL_LDFLAGS) -lz

turf.so: turf_reward.o points_turf_reward.o turf_stats.o

funky.so: autowarp.o bricklayer.o freqowners.o arenaperm.o auth_prefix.o \
	fake.o autoturret.o chatnet.o md5.o auth_file.o ap_multipub.o sendfile.o



sparse.inc: $(SCRIPTS)/gensparse.py $(SCRIPTS)/sparse_params.py
	$(SCRIPTS)/gensparse.py $@ $(SCRIPTS)/sparse_params.py

mapdata.o: sparse.inc

letters.inc: $(SCRIPTS)/processfont.py $(SCRIPTS)/banner.font
	$(SCRIPTS)/processfont.py $(SCRIPTS)/banner.font > $@

bricklayer.o: letters.inc

cfghelp.inc: $(SCRIPTS)/extract-cfg-docs.py
	cat *.c *.def | $(SCRIPTS)/extract-cfg-docs.py -c > $@

cfghelp.o: cfghelp.inc


Makefile.deps: sparse.inc letters.inc cfghelp.inc # *.c *.h
	-$(CC) $(ALL_CFLAGS) -MM *.c > $@


include Makefile.deps


%.so:
	$(CC) $(LDFLAGS) -shared -o $@ $^

install: $(ALL_STUFF)
	if [ ! -d $(ASSSHOME)/bin ]; then mkdir $(ASSSHOME)/bin; fi
	cp -f $(ALL_STUFF) $(ASSSHOME)/bin

clean:
	-rm -f asss dbtool *.o *.so *.inc core Makefile.deps


.PHONY: clean install all

