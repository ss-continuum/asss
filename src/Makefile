
CC = gcc
LDFLAGS = -fPIC


OPT_CFLAGS = -fPIC -O2 -fomit-frame-pointer -Wall -fpedantic
DEBUG_CFLAGS = -fPIC -g -Wall -fpedantic

CFLAGS = $(DEBUG_CFLAGS)

ASSSLIBS = -ldl -lz -lpthread




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
          billing.so



all: asss $(EXTMODS) install



loggers.so: log_file.o log_console.o

game.so: game.o chat.o

command.so: cmdman.o playercmd.o

billing.so: billcore.o



asss: main.o module.o util.o $(INTMODS)
	$(CC) -rdynamic -o $@ $^ $(ASSSLIBS)



Makefile.deps: *.c *.h
	$(CC) -MM *.c > $@

include Makefile.deps



%.so:
	$(CC) $(LDFLAGS) -shared -o $@ $^

install:
	-cp -f asss $(EXTMODS) ../bin
	@echo -e "\nWoot! It compiled!"

clean:
	-rm -f Makefile.deps asss *.o *.so core

tags:
	ctags *

.PHONY: clean install all tags

