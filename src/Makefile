

ASSSHOME = ..
SCRIPTS = $(ASSSHOME)/scripts

# change this to the directory of your bdb 4.x install
DBHOME = /opt/db-4.0.14
DBINC = -I$(DBHOME)/include
DBLIB = $(DBHOME)/lib

# change this to the directory of your mysql install
MYSQLHOME = /opt/mysql
MYSQLINC = -I$(MYSQLHOME)/include
MYSQLLIB = $(MYSQLHOME)/lib

LDFLAGS = -fPIC $(PROF_FLAGS)
#PROF_FLAGS = -pg

OPT_CFLAGS = -O2 -fomit-frame-pointer -Wall -fpedantic
DEBUG_CFLAGS = -g -Wall -fpedantic
ALL_CFLAGS = -D_REENTRANT -D_GNU_SOURCE -fPIC $(DBINC) $(MYSQLINC)

CFLAGS = $(ALL_CFLAGS) $(DEBUG_CFLAGS) $(PROF_FLAGS) # $(OPT_CFLAGS)

ALL_STUFF = asss         \
            core.so      \
            loggers.so   \
            game.so      \
            command.so   \
            external.so  \
            scoring.so   \
            admin.so     \
            database.so  \
            funky.so



# comment out 'install' here to not replace binaries by default
all: $(ALL_STUFF) install

opt: clean
	$(MAKE) "CFLAGS=$(ALL_CFLAGS) $(OPT_CFLAGS) $(PROF_FLAGS)"

asss: main.o app.o module.o util.o pathutil.o region.o
	$(CC) $(LDFLAGS) -rdynamic -o $@ $^ -ldl -lpthread

core.so: config.o player.o core.o logman.o mainloop.o net.o \
	nullenc.o encrypt1.o arenaman.o mapdata.o mapnewsdl.o clientset.o \
	capman.o security.o lagdata.o lagaction.o
	$(CC) $(LDFLAGS) -shared -o $@ $^ -lz

loggers.so: log_file.o log_console.o log_sysop.o

game.so: game.o game_timer.o chat.o flags.o balls.o fm_normal.o \
	banners.o objects.o messages.o
	$(CC) $(LDFLAGS) -shared -o $@ $^ -lm

command.so: cmdman.o playercmd.o watchdamage.o buy.o

external.so: billcore.o directory.o

scoring.so: persist.o stats.o statcodes.o points_kill.o points_flag.o \
	points_goal.o jackpot.o periodic.o points_periodic.o basicstats.o
	$(CC) $(LDFLAGS) -shared -o $@ $^ -L$(DBLIB) -Wl,-rpath,$(DBLIB) -ldb-4

admin.so: filetrans.o

database.so: mysql.o aliasdb.o
	$(CC) $(LDFLAGS) -shared -o $@ $^ -L$(MYSQLLIB) -lmysqlclient_r -lz

funky.so: autowarp.o bricklayer.o freqowners.o arenaperm.o auth_prefix.o \
	fake.o autoturret.o chatnet.o md5.o auth_file.o



sparse.inc: $(SCRIPTS)/gensparse.py $(SCRIPTS)/sparse_params.py
	$(SCRIPTS)/gensparse.py $@ $(SCRIPTS)/sparse_params.py

letters.inc: $(SCRIPTS)/processfont.py $(SCRIPTS)/banner.font
	$(SCRIPTS)/processfont.py $(SCRIPTS)/banner.font > $@

arpc_stubs.c: $(SCRIPTS)/arpc.py *.h
	$(SCRIPTS)/arpc.py makestubs > $@


Makefile.deps: sparse.inc letters.inc # *.c *.h
	-$(CC) $(ALL_CFLAGS) -MM *.c > $@


include Makefile.deps


%.so:
	$(CC) $(LDFLAGS) -shared -o $@ $^

install: $(ALL_STUFF)
	if [ ! -d $(ASSSHOME)/bin ]; then mkdir $(ASSSHOME)/bin; fi
	cp -f $(ALL_STUFF) $(ASSSHOME)/bin

clean:
	-rm -f asss *.o *.so *.inc core Makefile.deps

.PHONY: clean install all

