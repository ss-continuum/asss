
CC = gcc -D_REENTRANT
LDFLAGS = -fPIC


OPT_CFLAGS = -O2 -fomit-frame-pointer -Wall -fpedantic
DEBUG_CFLAGS = -g -Wall -fpedantic

CFLAGS = -fPIC $(DEBUG_CFLAGS)

ASSSLIBS = -ldl -lz -lpthread

SCHEMEROOT = /usr/local/plt/collects/mzscheme
SCHEMEINC = $(SCHEMEROOT)/include
SCHEMELIB = $(SCHEMEROOT)/lib/i386-linux




INTMODS = config.o    \
          core.o      \
          logman.o    \
          mainloop.o  \
          net.o       \
          encrypt1.o  \
          arenaman.o  \
		  mapnewsdl.o

EXTMODS = loggers.so  \
          game.so     \
          command.so  \
          billing.so  \
		  scheme.so



all: asss $(EXTMODS) elderd install



loggers.so: log_file.o log_console.o

game.so: game.o chat.o

command.so: cmdman.o playercmd.o

billing.so: billcore.o

scheme.so: scheme.o ooputils.o



asss: main.o module.o util.o $(INTMODS)
	$(CC) -rdynamic -o $@ $^ $(ASSSLIBS)


elderd: elderd.c util-elderd.o ooputils.o
	$(CC) $(OPT_CFLAGS) -L$(SCHEMELIB) -I $(SCHEMEINC) -o $@ $^ -lmzscheme -lgc -ldl


util-elderd.o: util.c
	$(CC) $(OPT_CFLAGS) -DNOTHREAD -DUSE_GC -DMALLOC=GC_malloc -c -o $@ $^


Makefile.deps: *.c *.h
	$(CC) -I $(SCHEMEINC) -MM *.c > $@

include Makefile.deps



%.so:
	$(CC) $(LDFLAGS) -shared -o $@ $^

install:
	-cp -f asss $(EXTMODS) elderd ../bin
	@echo -e "\nWoot! It compiled!"

clean:
	-rm -f asss *.o *.so elderd elderd.log core

tags:
	ctags *

.PHONY: clean install all tags

