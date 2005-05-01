
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

