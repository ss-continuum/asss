
#PROF_FLAGS = -pg

LDFLAGS = -fPIC $(PROF_FLAGS)

OPT_CFLAGS = -O2 -fomit-frame-pointer -Wall -fpedantic
DEBUG_CFLAGS = -g -Wall -fpedantic

CFLAGS = $(BUILDNO) -D_REENTRANT -D_GNU_SOURCE -fPIC $(DEBUG_CFLAGS) $(PROF_FLAGS)

ASSSLIBS = -ldl -lz -lpthread

SCRIPTS = ../scripts

ALL_STUFF = asss $(EXTMODS) $(INCLUDE_ELDERD)


INTMODS = config.o    \
          player.o    \
          core.o      \
          logman.o    \
          mainloop.o  \
          net.o       \
          encrypt1.o  \
          arenaman.o  \
          mapdata.o   \
          mapnewsdl.o \
          clientset.o \
          capman.o

EXTRAOBJS = module.o  \
            util.o    \
            pathutil.o\
            region.o

EXTMODS = loggers.so  \
          game.so     \
          command.so  \
          billing.so  \
          scoring.so  \
          scheme.so   \
          funky.so



all: pre install

pre: sparse.inc letters.inc buildnumber

loggers.so: log_file.o log_console.o log_sysop.o

game.so: game.o chat.o flags.o balls.o fm_normal.o
	$(CC) $(LDFLAGS) -shared -o $@ $^ -lm

command.so: cmdman.o playercmd.o

billing.so: billcore.o

scoring.so: persist.o stats.o points_kill.o points_flag.o points_goal.o
	$(CC) $(LDFLAGS) -shared -o $@ $^ -ldb1


tinyscheme/scheme.o:
	cd tinyscheme && $(MAKE) scheme.o

scheme.so: tscheme.o tinyscheme/scheme.o
	$(CC) $(LDFLAGS) -shared -o $@ $^ -lm

funky.so: autowarp.o bricklayer.o freqowners.o


asss: main.o $(INTMODS) $(EXTRAOBJS)
	$(CC) $(LDFLAGS) -rdynamic -o $@ $^ $(ASSSLIBS)


sparse.inc: $(SCRIPTS)/gensparse.py $(SCRIPTS)/sparse_params.py
	$(SCRIPTS)/gensparse.py $@ $(SCRIPTS)/sparse_params.py

letters.inc: $(SCRIPTS)/processfont.py $(SCRIPTS)/banner.font
	$(SCRIPTS)/processfont.py $(SCRIPTS)/banner.font > $@


Makefile.deps: # *.c *.h
	$(CC) -MM *.c > $@


include Makefile.deps

buildnumber: *.h
	expr `cat buildnumber` + 1 > b.tmp
	mv b.tmp buildnumber

Makefile: buildnumber

BUILDNO := -DBUILDNUMBER=$(shell cat buildnumber)



%.so:
	$(CC) $(LDFLAGS) -shared -o $@ $^

install: $(ALL_STUFF)
	-cp -f $(ALL_STUFF) ../bin

clean:
	-rm -f asss *.o *.so core

tags:
	flist -d *.c
	flist -h *.c

.PHONY: clean install all tags

