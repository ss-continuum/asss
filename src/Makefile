
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
BINDIR = $(ASSSHOME)/bin
SCRIPTS = $(ASSSHOME)/scripts

INSTALL = install
PYTHON = python

# default options:
debug = yes
opt = no
prof = no
link_db_statically = no

# comment any of these lines if you're missing some libraries:
#
# if you're missing berkeley db:
have_bdb = yes
#
# if you're missing mysql:
have_mysql = yes
#
# if you're missing python:
have_python = yes

# change this to the directory of your bdb 4.x install.
# freebsd users probably want /usr/local for all of the _HOME variables.
DB_HOME = /opt/db-4.0.14
# these probably won't have to change:
DB_INC = $(DB_HOME)/include
DB_LIB = $(DB_HOME)/lib
DB_CFLAGS = -I$(DB_INC)
ifneq ($(link_db_statically),yes)
# you might have to change -ldb-4 to -ldb4 in the next line:
DB_LDFLAGS = -L$(DB_LIB) -Wl,-rpath,$(DB_LIB) -ldb-4
else
DB_LDFLAGS = $(DB_LIB)/libdb.a
endif

# change this to the directory of your mysql install.
MYSQL_HOME = /opt/mysql
# these probably won't have to change
MYSQL_INC = $(MYSQL_HOME)/include
MYSQL_LIB = $(MYSQL_HOME)/lib
MYSQL_CFLAGS = -I$(MYSQL_INC)
MYSQL_LDFLAGS = -L$(MYSQL_LIB) -Wl,-rpath,$(MYSQL_LIB) -lmysqlclient_r

# change this to the location of your python 2.2 or higher install.
PYTHON_HOME = /usr
# change this to the major/minor version number of your python install.
PYTHON_VERSION = 2.3
# these probably won't have to change
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
ZLIB_LIB = -lz
PTHREAD_LIB = -lpthread
DASH_F_PIC = -fPIC
EXPORT_SYMBOLS = -rdynamic
SO = so
SYS_NAME := ok
endif
ifeq ($(SYS_NAME),FreeBSD)
UTIL_LIB = -lutil
ZLIB_LIB = -lz
DASH_F_PIC = -fPIC
DASH_PTHREAD = -pthread
EXPORT_SYMBOLS = -rdynamic
SO = so
SYS_NAME := ok
endif
ifeq ($(findstring CYGWIN,$(SYS_NAME)),CYGWIN)
PTHREAD_LIB = -lpthread
EXE = .exe
SO = so
SYS_NAME := ok
endif
ifeq ($(findstring MINGW,$(SYS_NAME)),MINGW)
run_dlltool = yes
PTHREAD_LIB = -L$(ASSSHOME)/bin -lpthreadGC -lwsock32
ZLIB_LIB = -L$(ASSSHOME)/bin -lzdll
SO_LDFLAGS = import.imp $(PTHREAD_LIB)
EXPORT_SYMBOLS = export.exp
EXE = .exe
SO = dll
W32COMPAT = win32compat.o
SYS_NAME := ok
endif
ifneq ($(SYS_NAME),ok)
$(error "Unknown operating system, you'll have to edit the makefile yourself")
endif

# apply build options
ifeq ($(debug),yes)
DEBUG_CFLAGS = -g -Wall
endif

ifeq ($(prof),yes)
PROF_FLAGS = -pg
endif

ifeq ($(opt),yes)
OPT_CFLAGS = -O2 -Wall
endif

# for older gcc, remove the -std option
CC = gcc -std=gnu99 -pipe
STD_CFLAGS = -I. -D_REENTRANT -D_GNU_SOURCE $(DASH_F_PIC)
CFLAGS = $(STD_CFLAGS) $(DEBUG_CFLAGS) $(PROF_FLAGS) $(OPT_CFLAGS)
LDFLAGS = $(DASH_PTHREAD) $(DASH_F_PIC) $(PROF_FLAGS)


# the standalone binaries to build
BINARIES := asss$(EXE)
ifeq ($(have_bdb),yes)
BINARIES += dbtool$(EXE)
endif

# dynamic libraries to build
DLS := funky.$(SO)
ifeq ($(have_bdb),yes)
DLS += scoring.$(SO)
endif
ifeq ($(have_mysql),yes)
DLS += database.$(SO)
endif
ifeq ($(have_python),yes)
DLS += pymod.$(SO)
endif

ALL_STUFF = $(BINARIES) $(DLS) backtrace

UTIL_OBJS = util.o pathutil.o protutil.o rwlock.o app.o md5.o $(W32COMPAT)

INTERNAL_MODULES = config.o prng.o md5.o player.o core.o logman.o \
	mainloop.o net.o enc_null.o enc_vie.o arenaman.o mapdata.o \
	mapnewsdl.o clientset.o capman.o lagdata.o lagaction.o unixsignal.o \
	deadlock.o \
	log_file.o log_console.o log_sysop.o log_staff.o \
	game.o game_timer.o chat.o flags.o balls.o fm_normal.o \
	banners.o bricks.o objects.o messages.o koth.o fm_lockspec.o \
	cmdman.o playercmd.o admincmd.o watchdamage.o buy.o help.o \
	cfghelp.o \
	directory.o billing.o billing_ssc.o \
	filetrans.o quickfix.o \
	freqowners.o arenaperm.o auth_prefix.o fake.o chatnet.o auth_file.o \
	ap_multipub.o sendfile.o auth_ban.o


all: install

asss$(EXE): main.o module.o cmod.o $(UTIL_OBJS) $(INTERNAL_MODULES)
ifdef run_dlltool
	dlltool.exe --export-all-symbols -D $@ -e export.exp -l import.imp $^
endif
	$(CC) $(LDFLAGS) $(EXPORT_SYMBOLS) -o $@ $^ $(DL_LIB) $(PTHREAD_LIB) $(ZLIB_LIB) -lm

dbtool$(EXE): dbtool.o statcodes.o util.c
	$(CC) $(LDFLAGS) -DNODQ -DNOTREAP -DNOSTRINGCHUNK -DNOMPQUEUE -DNOMMAP -o $@ $^ $(DB_LDFLAGS)

scoring.$(SO): persist.o stats.o statcodes.o points_kill.o points_flag.o \
	points_goal.o jackpot.o periodic.o points_periodic.o basicstats.o
scoring.$(SO): LIBS=$(DB_LDFLAGS)

database.$(SO): mysql.o aliasdb.o
database.$(SO): LIBS=$(MYSQL_LDFLAGS) $(ZLIB_LIB)

pymod.$(SO): pymod.o
pymod.$(SO): LIBS=$(PYTHON_LDFLAGS)

include */*.mk

funky.$(SO): bricklayer.o autowarp.o autoturret.o record.o $(EXTRA_FUNKY_OBJS)
funky.$(SO): LIBS=$(ZLIB_LIB) -lm


sparse.inc: $(SCRIPTS)/gensparse.py $(SCRIPTS)/sparse_params.py
	$(PYTHON) $(SCRIPTS)/gensparse.py $@ $(SCRIPTS)/sparse_params.py

letters.inc: $(SCRIPTS)/processfont.py $(SCRIPTS)/banner.font
	$(PYTHON) $(SCRIPTS)/processfont.py $(SCRIPTS)/banner.font $@

cfghelp.inc: $(SCRIPTS)/extract-cfg-docs.py
	$(PYTHON) $(SCRIPTS)/extract-cfg-docs.py -c $@ *.c */*.c *.def

py_constants.inc py_callbacks.inc py_interfaces.inc: $(SCRIPTS)/pymod-process.py *.h */*.h
	$(PYTHON) $(SCRIPTS)/pymod-process.py *.h */*.h


# c files that need special options
persist.o: CFLAGS += $(DB_CFLAGS)

dbtool.o: CFLAGS += $(DB_CFLAGS)

mysql.o: CFLAGS += $(MYSQL_CFLAGS)

pymod.o: CFLAGS += $(PYTHON_CFLAGS)


%.$(SO):
	$(CC) $(LDFLAGS) -shared -o $@ $^ $(SO_LDFLAGS) $(LIBS)

install: $(ALL_STUFF)
	$(INSTALL) -d $(BINDIR)
	$(INSTALL) $(ALL_STUFF) $(BINDIR)

clean: cleanbin
	-rm -f *.inc core Makefile.deps

cleanbin:
	-rm -f asss$(EXE) dbtool$(EXE) *.o */*.o *.$(SO) */*.$(SO)

.PHONY: clean cleanbin install all


Makefile.deps: sparse.inc letters.inc cfghelp.inc py_constants.inc \
	py_callbacks.inc py_interfaces.inc # *.c */*.c *.h */*.h
	-$(CC) $(STD_CFLAGS) $(DB_CFLAGS) $(MYSQL_CFLAGS) $(PYTHON_CFLAGS) -MM *.c */*.c > $@

include Makefile.deps

# dist: public

