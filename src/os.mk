
# this file contains stuff to adjust the behavior of the makefile based
# on the OS we're running on.

SYS_NAME := $(shell uname -s)

ifeq ($(SYS_NAME),Linux)
UTIL_LIB = -lutil
DL_LIB = -ldl
ZLIB_LIB = -lz
PTHREAD_LIB = -lpthread
DASH_F_PIC = -fPIC
EXPORT_SYMBOLS = -rdynamic
SO = so
unixsignal = unixsignal
SYS_NAME := ok
endif
ifeq ($(SYS_NAME),FreeBSD)
UTIL_LIB = -lutil
ZLIB_LIB = -lz
DASH_F_PIC = -fPIC
DASH_PTHREAD = -pthread
EXPORT_SYMBOLS = -rdynamic
SO = so
unixsignal = unixsignal
SYS_NAME := ok
endif
ifeq ($(findstring CYGWIN,$(SYS_NAME)),CYGWIN)
$(warning WARNING: cygwin support hasn't been tested recently. it's probably broken.)
PTHREAD_LIB = -lpthread
EXE = .exe
SO = so
SYS_NAME := ok
endif
ifeq ($(findstring MINGW,$(SYS_NAME)),MINGW)

# set up paths to point to windeps
WINDEPS := ../windeps

INSTALL := $(WINDEPS)/install.exe
PYTHON := $(WINDEPS)/python.exe
symlink_bins := no

include $(WINDEPS)/system-windeps.mk

run_dlltool = yes
PTHREAD_LIB = -L$(WINDEPS) -lpthreadGC2 -lwsock32
ZLIB_LIB = -L$(WINDEPS) -lzdll
SO_LDFLAGS = $(call tobuild, import.imp) $(PTHREAD_LIB)
EXPORT_SYMBOLS = $(call tobuild, export.exp)
EXE = .exe
SO = dll
W32COMPAT = win32compat
EXTRA_INCLUDE_DIRS = -I$(WINDEPS)
EXTRA_INSTALL_FILES = $(WINDEPS)/zlib1.dll $(WINDEPS)/pthreadGC2.dll \
	$(WINDEPS)/python24.dll $(WINDEPS)/libdb4.dll

SYS_NAME := ok
endif
ifneq ($(SYS_NAME),ok)
$(error "Unknown operating system, you'll have to edit the makefile yourself")
endif

