# Minimal GNU make build of libcubiomes.a for cubiomes-viewer (qmake PRE_LINK).
# Usage: make -f makefile [release|debug|native] CC=... CFLAGS=...
# Sources mirror CMakeLists.txt (everything except tests); no CMake needed.

ARFLAGS = cr
override CFLAGS += -Wall -Wextra -fwrapv

ifeq ($(OS),Windows_NT)
	override CFLAGS += -D_WIN32
else
	override CFLAGS += -fPIC
endif

SRCS := $(filter-out tests.c,$(wildcard *.c)) \
	$(wildcard features/*.c) \
	$(wildcard loot/*.c) \
	$(wildcard loot/cjson/*.c) \
	$(wildcard loot/loot_tables/*.c) \
	$(wildcard loot/loot_tables/*/*.c) \
	$(wildcard loot/loot_tables/*/*/*.c)
OBJS := $(SRCS:.c=.o)

.PHONY : all debug release native libcubiomes clean

all: release

debug: CFLAGS += -DDEBUG -O0 -ggdb3
debug: libcubiomes
release: CFLAGS += -O3
release: libcubiomes
native: CFLAGS += -O3 -march=native -ffast-math
native: libcubiomes

libcubiomes: libcubiomes.a

# object list goes through a response file: the Windows command line limit is too short for ~100 objects
libcubiomes.a: $(OBJS)
	$(file >cubiomes.objs,$(OBJS))
	$(AR) $(ARFLAGS) $@ @cubiomes.objs

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	$(RM) $(OBJS) libcubiomes.a cubiomes.objs
