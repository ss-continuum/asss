
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
ASSSHOME := ..
BINDIR := $(ASSSHOME)/bin
BUILDDIR := $(ASSSHOME)/build
SCRIPTS := $(ASSSHOME)/scripts
PYMOD_HEADERS := *.h */*.h

INSTALL = install
LN = ln
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
STD_CFLAGS = -I. -I$(BUILDDIR) -D_REENTRANT -D_GNU_SOURCE $(DASH_F_PIC)
CFLAGS = $(STD_CFLAGS) $(DEBUG_CFLAGS) $(PROF_FLAGS) $(OPT_CFLAGS)
LDFLAGS = $(DASH_PTHREAD) $(DASH_F_PIC) $(PROF_FLAGS)


# the standalone binaries to build
BINARIES = asss$(EXE)
ifeq ($(have_bdb),yes)
BINARIES += dbtool$(EXE)
endif

# the dynamic libraries to build
DLS = $(funky_DL)
ifeq ($(have_bdb),yes)
DLS += $(maybe_scoring_DL)
endif
ifeq ($(have_mysql),yes)
DLS += $(maybe_database_DL)
endif
ifeq ($(have_python),yes)
DLS += $(maybe_pymod_DL)
endif


# some macros to help with putting output in $(BUILDDIR)
tobuild = $(addprefix $(BUILDDIR)/,$(1))
tobuildo = $(addprefix $(BUILDDIR)/,$(addsuffix .o,$(1)))
builddir = $(call tobuild,.stamp)

# now we get to the object files
UTIL_OBJS = util pathutil protutil rwlock app md5 $(W32COMPAT)

INTERNAL_MODULES = config prng md5 player core logman \
	mainloop net enc_null enc_vie arenaman mapdata \
	mapnewsdl clientset capman lagdata lagaction \
	bw_default bw_nolimit \
	deadlock idle \
	log_file log_console log_sysop log_staff \
	game game_timer chat flagcore balls fm_normal \
	banners bricks objects messages koth fm_lockspec \
	cmdman playercmd admincmd watchdamage buy help \
	cfghelp \
	directory billing billing_ssc \
	filetrans quickfix \
	freqowners arenaperm auth_prefix fake chatnet auth_file \
	ap_multipub sendfile auth_ban \
	unixsignal

# this is the primary target
all: install

# this builds the main asss binary
$(call tobuild, asss$(EXE)): $(call tobuildo, main module cmod) \
		$(call tobuildo, $(UTIL_OBJS) $(INTERNAL_MODULES))
ifdef run_dlltool
	dlltool.exe --export-all-symbols -D $@ -e $(call tobuild, export.exp) -l $(call tobuild, import.imp) $^
endif
	$(CC) $(LDFLAGS) $(EXPORT_SYMBOLS) -o $@ $^ $(DL_LIB) $(PTHREAD_LIB) $(ZLIB_LIB) -lm

# this builds the dbtool binary
$(call tobuild, dbtool$(EXE)): $(call tobuildo, dbtool statcodes) util.c
	$(CC) $(CFLAGS) $(LDFLAGS) -DNODQ -DNOTREAP -DNOSTRINGCHUNK -DNOMPQUEUE -DNOMMAP -o $@ $^ $(DB_LDFLAGS)


# this template makes it easy to build new dynamic libraries
define dl_template
$$(call tobuild, $(1).$(SO)): $$(call tobuildo,$$($(1)_mods))
	$(CC) $(LDFLAGS) -shared -o $$@ $$^ $(SO_LDFLAGS) $$($(1)_libs)
$(2)$(1)_DL := $$(call tobuild, $(1).$(SO))
DLS += $$($(1)_DL)
endef

scoring_mods = \
	persist stats statcodes points_kill points_flag \
	points_goal jackpot persist points_periodic basicstats
scoring_libs = $(DB_LDFLAGS)

database_mods = mysql aliasdb
database_libs = $(MYSQL_LDFLAGS) $(ZLIB_LIB)

pymod_mods = pymod
pymod_libs = $(PYTHON_LDFLAGS)

include */*.mk

funky_mods = bricklayer autowarp autoturret record sgcompat $(EXTRA_FUNKY_OBJS)
funky_libs = $(ZLIB_LIB) -lm

# generate rules for our dynamic libraries
$(eval $(call dl_template,funky))
$(eval $(call dl_template,scoring,maybe_))
$(eval $(call dl_template,database,maybe_))
$(eval $(call dl_template,pymod,maybe_))


# generated files
$(call tobuild, sparse.inc): $(builddir) $(SCRIPTS)/gensparse.py $(SCRIPTS)/sparse_params.py
	$(PYTHON) $(SCRIPTS)/gensparse.py $@ $(SCRIPTS)/sparse_params.py

$(call tobuild, letters.inc): $(builddir) $(SCRIPTS)/processfont.py $(SCRIPTS)/banner.font
	$(PYTHON) $(SCRIPTS)/processfont.py $(SCRIPTS)/banner.font $@

$(call tobuild, cfghelp.inc): $(builddir) $(SCRIPTS)/extract-cfg-docs.py
	$(PYTHON) $(SCRIPTS)/extract-cfg-docs.py -c $@ *.c */*.c *.py */*.py *.def

# also builds py_callbacks.inc, py_interfaces.inc, and py_types.inc
$(call tobuild, py_constants.inc): $(SCRIPTS)/pymod-process.py $(PYMOD_HEADERS)
	$(PYTHON) $(SCRIPTS)/pymod-process.py $(dir $@) $(PYMOD_HEADERS)


# c files that need special options
$(call tobuild, persist.o): CFLAGS += $(DB_CFLAGS)

$(call tobuild, dbtool.o): CFLAGS += $(DB_CFLAGS)

$(call tobuild, mysql.o): CFLAGS += $(MYSQL_CFLAGS)

$(call tobuild, pymod.o): CFLAGS += $(PYTHON_CFLAGS)

%.o:
	$(CC) $(CFLAGS) -c -o $@ $<

install: $(BINDIR)/.stamp install2

install2: $(call tobuild, $(BINARIES)) $(DLS) backtrace
	$(LN) -sf $(foreach f,$^,$(shell readlink -f $(f))) $(BINDIR)
	$(LN) -sf $(foreach f,$(wildcard *.py),$(shell readlink -f $(f))) $(BINDIR)

clean:
	-rm -f $(BUILDDIR)/*

.PHONY: clean install install2 all


# a rule for creating directories
%/.stamp:
	$(INSTALL) -d $(dir $@)
	> $@

$(call tobuild, Makefile.deps): $(builddir) \
	$(call tobuild, sparse.inc letters.inc cfghelp.inc py_constants.inc) \
	# *.c */*.c *.h */*.h
	-$(CC) $(STD_CFLAGS) $(DB_CFLAGS) $(MYSQL_CFLAGS) $(PYTHON_CFLAGS) -MM *.c */*.c | \
		sed 's+^[^ \t]+$$(BUILDDIR)/&+' > $@

include $(call tobuild, Makefile.deps)

# dist: public

