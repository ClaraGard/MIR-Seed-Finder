# MIR seed finder — build separately from the cubiomes library.
# From cubiomes/: make -C MIR_seed_filter -j8
# From MIR_seed_filter/: make -j8

CPPFLAGS += -I.. -I../loot/cjson
CFLAGS += -O3 -std=c11 -Wall -Wextra -fwrapv

ifeq ($(OS),Windows_NT)
EXE := seedfilter3.exe
STATS_EXE := seedstats.exe
PLATFORM_LIBS := -lbcrypt
else
EXE := seedfilter3
STATS_EXE := seedstats
PLATFORM_LIBS :=
endif

SOURCES := seedfilter3.c biomeareachecker.c structurechecker.c criterialoader.c vault_loot_filter.c
STATS_SOURCES := seed_stats.c biomeareachecker.c structurechecker.c criterialoader.c vault_loot_filter.c
HEADERS := biomeareachecker.h structurechecker.h criterialoader.h vault_loot_filter.h

.PHONY: all clean
all: $(EXE)

../libcubiomes.a:
	$(MAKE) -C .. -f makefile release

$(EXE): $(SOURCES) $(HEADERS) ../libcubiomes.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) ../libcubiomes.a -lm $(PLATFORM_LIBS) -o $@

$(STATS_EXE): $(STATS_SOURCES) $(HEADERS) ../libcubiomes.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(STATS_SOURCES) ../libcubiomes.a -lm $(PLATFORM_LIBS) -o $@

clean:
	$(RM) $(EXE) $(STATS_EXE)
