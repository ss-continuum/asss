
#PROF_FLAGS = -pg

LDFLAGS = -fPIC $(PROF_FLAGS)

OPT_CFLAGS = -O2 -fomit-frame-pointer -Wall -fpedantic
DEBUG_CFLAGS = -g -Wall -fpedantic

CFLAGS = -D_REENTRANT -D_GNU_SOURCE -fPIC $(DEBUG_CFLAGS) $(PROF_FLAGS)

ASSSLIBS = -ldl -lz -lpthread

SCRIPTS = ../scripts

ALL_STUFF = asss $(EXTMODS)


# the idea here is to have all the modules necessary for core
# functionality and player logins to be in the main executable.
INTMODS = config.o app.o   \
          player.o         \
          core.o           \
          logman.o         \
          mainloop.o       \
          net.o            \
          nullenc.o        \
          encrypt1.o       \
          arenaman.o       \
          mapdata.o        \
          mapnewsdl.o      \
          clientset.o      \
          capman.o         \
          security.o

# these are things that the main executable needs to function.
EXTRAOBJS = module.o       \
            util.o         \
            pathutil.o     \
            region.o

# these are the other modules. their contents are below.
EXTMODS = loggers.so       \
          game.so          \
          command.so       \
          external.so      \
          scoring.so       \
          admin.so         \
          funky.so



all: pre install

pre: sparse.inc letters.inc

loggers.so: log_file.o log_console.o log_sysop.o

game.so: game.o game_timer.o chat.o flags.o balls.o fm_normal.o banners.o
	$(CC) $(LDFLAGS) -shared -o $@ $^ -lm

command.so: cmdman.o playercmd.o watchdamage.o

external.so: billcore.o directory.o

scoring.so: persist.o stats.o points_kill.o points_flag.o points_goal.o
	$(CC) $(LDFLAGS) -shared -o $@ $^ -ldb1

admin.so: filetrans.o

funky.so: autowarp.o bricklayer.o freqowners.o arenaperm.o auth_prefix.o \
	fake.o autoturret.o objects.o


asss: main.o $(INTMODS) $(EXTRAOBJS)
	$(CC) $(LDFLAGS) -rdynamic -o $@ $^ $(ASSSLIBS)


sparse.inc: $(SCRIPTS)/gensparse.py $(SCRIPTS)/sparse_params.py
	$(SCRIPTS)/gensparse.py $@ $(SCRIPTS)/sparse_params.py

letters.inc: $(SCRIPTS)/processfont.py $(SCRIPTS)/banner.font
	$(SCRIPTS)/processfont.py $(SCRIPTS)/banner.font > $@

arpc_stubs.c: $(SCRIPTS)/arpc.py *.h
	$(SCRIPTS)/arpc.py makestubs > $@


Makefile.deps: # *.c *.h
	-$(CC) -MM *.c > $@


include Makefile.deps


%.so:
	$(CC) $(LDFLAGS) -shared -o $@ $^

install: $(ALL_STUFF)
	-cp -f $(ALL_STUFF) ../bin

clean:
	-rm -f asss *.o *.so *.inc core Makefile.deps

.PHONY: clean install all tags

