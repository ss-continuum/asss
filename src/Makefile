
CC = gcc
CFLAGS = -fPIC -O2 -fomit-frame-pointer -Wall -fpedantic
LDFLAGS = -fPIC

ASSSLIBS = -ldl -lz




INTMODS = config.o core.o logman.o mainloop.o net.o encrypt1.o

EXTMODS = loggers.so game.so command.so billing.so

UTIL = util.o -lpthread



all: asss $(EXTMODS) install

debug: CFLAGS = -fPIC -g
debug: clean all


loggers.so: log_file.o log_console.o

game.so: game.o chat.o

command.so: cmdman.o playercmd.o

billing.so: billcore.o


asss: main.o module.o $(UTIL) $(INTMODS)
	$(CC) -rdynamic -o $@ $^ $(ASSSLIBS)



Makefile.deps: *.c *.h
	$(CC) -MM *.c > $@

include Makefile.deps



%.so:
	$(CC) $(LDFLAGS) -shared -o $@ $^ $(UTIL)

install:
#	-strip asss $(EXTMODS)
	-cp -f asss $(EXTMODS) ../bin
	@echo -e "\nWoot! It compiled!"

clean:
	-rm -f Makefile.deps asss *.o *.so core

tags:
	ctags *

.PHONY: clean install all tags

