
#PROF_FLAGS = -pg

LDFLAGS = -fPIC $(PROF_FLAGS)

OPT_CFLAGS = -O2 -fomit-frame-pointer -Wall -fpedantic
DEBUG_CFLAGS = -g -Wall -fpedantic

CFLAGS = $(BUILDNO) -D_REENTRANT -D_GNU_SOURCE -fPIC $(DEBUG_CFLAGS) $(PROF_FLAGS)

ASSSLIBS = -ldl -lz -lpthread

SCHEMEROOT = /home/david/scheme/plt/collects/mzscheme
SCHEMEINC = $(SCHEMEROOT)/include
SCHEMELIB = $(SCHEMEROOT)/lib/i386-linux

INCLUDE_ELDERD =
#INCLUDE_ELDERD = elderd

ALL_STUFF = asss $(EXTMODS) $(INCLUDE_ELDERD)


INTMODS = config.o    \
          pathutil.o  \
          player.o    \
          core.o      \
          logman.o    \
          mainloop.o  \
          net.o       \
          encrypt1.o  \
          arenaman.o  \
          player.o    \
          mapnewsdl.o \
          clientset.o

EXTMODS = loggers.so  \
          game.so     \
          command.so  \
          billing.so  \
          scoring.so  \
          scheme.so



all: install


loggers.so: log_file.o log_console.o

game.so: game.o chat.o flags.o
	$(CC) $(LDFLAGS) -shared -o $@ $^ -lm

command.so: cmdman.o playercmd.o

billing.so: billcore.o

scoring.so: persist.o stats.o points_kill.o
	$(CC) $(LDFLAGS) -shared -o $@ $^ -ldb1

scheme.so: scheme.o ooputils.o



asss: main.o module.o util.o $(INTMODS)
	$(CC) $(LDFLAGS) -rdynamic -o $@ $^ $(ASSSLIBS)


elderd: elderd.c util-elderd.o ooputils.o
	$(CC) $(CFLAGS) -L$(SCHEMELIB) -I $(SCHEMEINC) -o $@ $^ -lmzscheme -lgc -ldl


util-elderd.o: util.c
	$(CC) $(CFLAGS) -DNOTHREAD -DUSE_GC -DMALLOC=GC_malloc -c -o $@ $^


Makefile.deps: *.c *.h
	$(CC) -I $(SCHEMEINC) -MM *.c > $@

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
	-rm -f asss *.o *.so elderd elderd.log core

tags:
	flist -d *.c
	flist -h *.c

.PHONY: clean install all tags

