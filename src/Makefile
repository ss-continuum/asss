
LDFLAGS = -fPIC

OPT_CFLAGS = -O2 -fomit-frame-pointer -Wall -fpedantic
DEBUG_CFLAGS = -g -Wall -fpedantic

CFLAGS = -D_REENTRANT -D_GNU_SOURCE -fPIC $(DEBUG_CFLAGS)

ASSSLIBS = -ldl -lz -lpthread

SCHEMEROOT = /home/david/scheme/plt/collects/mzscheme
SCHEMEINC = $(SCHEMEROOT)/include
SCHEMELIB = $(SCHEMEROOT)/lib/i386-linux

INCLUDE_ELDERD =
#INCLUDE_ELDERD = elderd

ALL_STUFF = asss $(EXTMODS) $(INCLUDE_ELDERD)


INTMODS = config.o    \
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
          scheme.so



all: install


loggers.so: log_file.o log_console.o

game.so: game.o chat.o

command.so: cmdman.o playercmd.o

billing.so: billcore.o

scheme.so: scheme.o ooputils.o



asss: main.o module.o util.o pathutil.o $(INTMODS)
	$(CC) $(LDFLAGS) -rdynamic -o $@ $^ $(ASSSLIBS)


elderd: elderd.c util-elderd.o ooputils.o
	$(CC) $(CFLAGS) -L$(SCHEMELIB) -I $(SCHEMEINC) -o $@ $^ -lmzscheme -lgc -ldl


util-elderd.o: util.c
	$(CC) $(CFLAGS) -DNOTHREAD -DUSE_GC -DMALLOC=GC_malloc -c -o $@ $^


Makefile.deps: *.c *.h
	$(CC) -I $(SCHEMEINC) -MM *.c > $@

include Makefile.deps



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

