
#
# this is the makefile for asss. it works on linux, freebsd, and cygwin.
# some parts requires tweaking by the user to specify locations of
# libraries and things.
#
# just running "make" will build asss with the default options
# (debug=yes, optmization=no, profiling=no).
#
# if you want an optimized build, use "make opt=yes".
# if you want a profiled build, use "make prof=yes".
# if you want a _non_debug build, use "make debug=no".
# if you want to link bdb statically, use "make link_db_statically=yes".
#


# this should point to the root directory of the asss tree.
ASSSHOME = ..
SCRIPTS = $(ASSSHOME)/scripts

# default options:
debug = yes
opt = no
prof = no
link_db_statically = no

# uncomment any of these lines if you're missing some libraries:
#
# if you're missing berkeley db:
#no_bdb = yes
#
# if you're missing mysql:
#no_mysql = yes
#
# if you're missing python:
#no_python = yes

# change this to the directory of your bdb 4.x install
DB_HOME = /opt/db-4.0.14
# these shouldn't have to change
DB_INC = $(DB_HOME)/include
DB_LIB = $(DB_HOME)/lib
DB_CFLAGS = -I$(DB_INC)
# you might have to change -ldb-4 to -ldb4 in the next line:
DB_LDFLAGS = -L$(DB_LIB) -Wl,-rpath,$(DB_LIB) -ldb-4

# change this to the directory of your mysql install
MYSQL_HOME = /opt/mysql
# these shouldn't have to change
MYSQL_INC = $(MYSQL_HOME)/include
MYSQL_LIB = $(MYSQL_HOME)/lib
MYSQL_CFLAGS = -I$(MYSQL_INC)
MYSQL_LDFLAGS = -L$(MYSQL_LIB) -Wl,-rpath,$(MYSQL_LIB) -lmysqlclient_r

# change this to the location of your python 2.2 or higher install
PYTHON_HOME = /opt/python-2.2.2
# change this to the major/minor version number of your python install
PYTHON_VERSION = 2.2
# these shouldn't have to change
PYTHON_INC = $(PYTHON_HOME)/include/python$(PYTHON_VERSION)
PYTHON_LIB = $(PYTHON_HOME)/lib/python$(PYTHON_VERSION)
PYTHON_CFLAGS = -I$(PYTHON_INC)
PYTHON_LDFLAGS = -L$(PYTHON_LIB)/config -lpython$(PYTHON_VERSION) $(UTIL_LIB) -lm


# this stuff adjusts the makefile depending on the operating system
# we're on.

SYS_NAME := $(shell uname -s)

ifeq ($(SYS_NAME),Linux)
UTIL_LIB = -lutil
DL_LIB = -ldl
PTHREAD_LIB = -lpthread
DASH_F_PIC = -fPIC
DASH_RDYNAMIC = -rdynamic
SYS_NAME := ok
endif
ifeq ($(SYS_NAME),FreeBSD)
UTIL_LIB = -lutil
DASH_F_PIC = -fPIC
DASH_PTHREAD = -pthread
DASH_RDYNAMIC = -rdynamic
SYS_NAME := ok
endif
ifeq ($(findstring CYGWIN,$(SYS_NAME)),CYGWIN)
PTHREAD_LIB = -lpthread
EXE = .exe
DL_UTIL_OBJS = $(UTIL_OBJS)
SYS_NAME := ok
endif
ifneq ($(SYS_NAME),ok)
$(error Unknown operating system, you'll have to edit the makefile yourself)
endif

# apply build options
ifeq ($(debug),yes)
DEBUG_CFLAGS = -g -Wall -fpedantic
endif

ifeq ($(prof),yes)
PROF_FLAGS = -pg
endif

ifeq ($(opt),yes)
OPT_CFLAGS = -O2 -fomit-frame-pointer -Wall -fpedantic -DNDEBUG
endif

CC = gcc -pipe
STD_CFLAGS = -D_REENTRANT -D_GNU_SOURCE $(DASH_F_PIC)
CFLAGS = $(STD_CFLAGS) $(DEBUG_CFLAGS) $(PROF_FLAGS) $(OPT_CFLAGS)
LDFLAGS = $(DASH_PTHREAD) $(DASH_F_PIC) $(PROF_FLAGS)


# the standalone binaries to build
BINARIES := asss$(EXE) dbtool$(EXE)

# dynamic libraries to build
DLS := core.so loggers.so game.so command.so external.so admin.so funky.so turf.so
ifndef no_bdb
DLS += scoring.so
endif
ifndef no_mysql
DLS += database.so
endif
ifndef no_python
DLS += pymod.so
endif

ALL_STUFF := $(BINARIES) $(DLS)

UTIL_OBJS = util.o pathutil.o protutil.o region.o rwlock.o app.o


all: install

asss$(EXE): main.o module.o cmod.o $(UTIL_OBJS)
	$(CC) $(LDFLAGS) $(DASH_RDYNAMIC) -o $@ $^ $(DL_LIB) $(PTHREAD_LIB)

dbtool$(EXE): dbtool.o statcodes.o util.c
	$(CC) $(LDFLAGS) -DNOMPQUEUE -o $@ $^ $(DB_LDFLAGS)

core.so: config.o player.o core.o logman.o mainloop.o net.o \
	nullenc.o encrypt1.o arenaman.o mapdata.o mapnewsdl.o clientset.o \
	capman.o lagdata.o lagaction.o $(DL_UTIL_OBJS)
	$(CC) $(LDFLAGS) -shared -o $@ $^ -lz

loggers.so: log_file.o log_console.o log_sysop.o

game.so: game.o game_timer.o chat.o flags.o balls.o fm_normal.o \
	banners.o objects.o messages.o koth.o fm_lockspec.o $(DL_UTIL_OBJS)
	$(CC) $(LDFLAGS) -shared -o $@ $^ -lm

command.so: cmdman.o playercmd.o admincmd.o watchdamage.o buy.o help.o \
	cfghelp.o $(DL_UTIL_OBJS)

external.so: directory.o billing.o billing_ssc.o

scoring.so: persist.o stats.o statcodes.o points_kill.o points_flag.o \
	points_goal.o jackpot.o periodic.o points_periodic.o basicstats.o \
	$(DL_UTIL_OBJS)
ifneq ($(link_db_statically),yes)
	$(CC) $(LDFLAGS) -shared -o $@ $^ $(DB_LDFLAGS)
else
	$(CC) $(LDFLAGS) -shared -o $@ $^ $(DB_LIB)/libdb.a
endif

admin.so: filetrans.o quickfix.o $(DL_UTIL_OBJS)

database.so: mysql.o aliasdb.o $(DL_UTIL_OBJS)
	$(CC) $(LDFLAGS) -shared -o $@ $^ $(MYSQL_LDFLAGS) -lz

turf.so: turf_reward.o points_turf_reward.o turf_stats.o $(DL_UTIL_OBJS)

pymod.so: pymod.o $(DL_UTIL_OBJS)
	$(CC) $(LDFLAGS) -shared -o $@ $^ $(PYTHON_LDFLAGS)

funky.so: autowarp.o bricklayer.o freqowners.o arenaperm.o auth_prefix.o \
	fake.o autoturret.o chatnet.o md5.o auth_file.o ap_multipub.o sendfile.o \
	record.o $(DL_UTIL_OBJS)
	$(CC) $(LDFLAGS) -shared -o $@ $^ -lz -lm



sparse.inc: $(SCRIPTS)/gensparse.py $(SCRIPTS)/sparse_params.py
	$(SCRIPTS)/gensparse.py $@ $(SCRIPTS)/sparse_params.py

letters.inc: $(SCRIPTS)/processfont.py $(SCRIPTS)/banner.font
	$(SCRIPTS)/processfont.py $(SCRIPTS)/banner.font > $@

cfghelp.inc: $(SCRIPTS)/extract-cfg-docs.py
	cat *.c *.def | $(SCRIPTS)/extract-cfg-docs.py -c > $@

py_constants.inc py_callbacks.inc py_interfaces.inc: $(SCRIPTS)/pymod-process.py *.h
	cat *.h | $(SCRIPTS)/pymod-process.py


# c files that need special options
persist.o: CFLAGS += $(DB_CFLAGS)

dbtool.o: CFLAGS += $(DB_CFLAGS)

mysql.o: CFLAGS += $(MYSQL_CFLAGS)

pymod.o: CFLAGS += $(PYTHON_CFLAGS)


%.so:
	$(CC) $(LDFLAGS) -shared -o $@ $^

install: $(ALL_STUFF)
	install -d $(ASSSHOME)/bin
	install $(ALL_STUFF) $(ASSSHOME)/bin

clean:
	-rm -f asss$(EXE) dbtool$(EXE) *.o *.so *.inc core Makefile.deps


.PHONY: clean install all


Makefile.deps: sparse.inc letters.inc cfghelp.inc py_constants.inc \
	py_callbacks.inc py_interfaces.inc # *.c *.h
	-$(CC) $(STD_CFLAGS) $(DB_CFLAGS) $(MYSQL_CFLAGS) $(PYTHON_CFLAGS) -MM *.c > $@

include Makefile.deps

