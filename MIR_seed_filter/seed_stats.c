/*
 * MIR seedstats — independent selectivity measurement for MC 26.3.
 * Reuses exactly the same criterion loaders/checkers as seedfilter3.c.
 *
 * Build with MIR_seed_filter/Makefile (seedstats target).
 * One special checker groupIndex (-2) disables early rejection ONLY for
 * this executable. See the tiny change in criterialoader.h.
 */
#include "finders.h"
#include "generator.h"
#include "biomes.h"
#include "noise.h"
#include "util.h"
#include "biomeareachecker.h"
#include "criterialoader.h"
#include "vault_loot_filter.h"
#include "structurechecker.h"
#include "rng.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#include <bcrypt.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/random.h>
#endif

#define MC MC_26_3
#define MIN_SPAWN_SURFACE_Y 64
#define STATS_INDEPENDENT_GROUP_INDEX (-2)

typedef struct {
    uint64_t passed;
    uint64_t failed;
    uint64_t errors;
} Counts;

typedef struct {
    char magic[8];
    uint64_t samples;
    uint64_t criteria;
    uint64_t groups;
} WorkerHeader;

typedef struct {
    size_t biome, distance, amount, vault, spawn, groups, overall, total;
} Layout;

static Layout make_layout(const CriteriaConfig *c)
{
    Layout l;
    l.biome = 0;
    l.distance = c->biomes.count;
    l.amount = l.distance + c->structuresDistance.count;
    l.vault = l.amount + c->structuresAmount.count;
    l.spawn = l.vault + c->vaultLoot.count;
    l.groups = l.spawn + 1;
    l.overall = l.groups + c->groupCount;
    l.total = l.overall + 1;
    return l;
}

static int parse_u64(const char *s, uint64_t *out)
{
    char *end;
    unsigned long long v;
    if (!s || !*s || *s == '-') return 0;
    errno = 0;
    v = strtoull(s, &end, 0);
    if (errno || end == s || *end || v > UINT64_MAX) return 0;
    *out = (uint64_t)v;
    return 1;
}

static int parse_bool(const char *s, int *out)
{
    if (!strcmp(s, "true") || !strcmp(s, "1")) { *out = 1; return 1; }
    if (!strcmp(s, "false") || !strcmp(s, "0")) { *out = 0; return 1; }
    return 0;
}

static int os_random_bytes(void *buf, size_t n)
{
#ifdef _WIN32
    return n <= ULONG_MAX && BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)n,
                         BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    unsigned char *p = buf;
    while (n) {
        ssize_t got = getrandom(p, n, 0);
        if (got < 0) { if (errno == EINTR) continue; return 0; }
        if (got == 0) return 0;
        p += (size_t)got;
        n -= (size_t)got;
    }
    return 1;
#endif
}

static void record(Counts *c, int status)
{
    if (status > 0) c->passed++;
    else if (status == 0) c->failed++;
    else c->errors++;
}

/* Convert checkers' statuses to 1=pass, 0=fail, -1=error/unevaluated. */
static int biome_status(BiomeCheckStatus s)
{
    if (s == BIOME_CHECK_PASSED) return 1;
    if (s == BIOME_CHECK_FAILED) return 0;
    return -1;
}
static int structure_status(StructureCheckStatus s)
{
    if (s == STRUCTURE_CHECK_PASSED) return 1;
    if (s == STRUCTURE_CHECK_FAILED) return 0;
    return -1;
}

/* Same hardcoded spawn-Y check as seedfilter3.c, without short-circuiting. */
static int check_spawn_y(Generator *ow, uint64_t seed)
{
    Pos spawn = getSpawn(ow);
    float approxY = -1.0f;
    SurfaceNoise noise;
    initSurfaceNoise(&noise, DIM_OVERWORLD, seed);
    if (mapApproxHeight(&approxY, NULL, ow, &noise,
                        spawn.x >> 2, spawn.z >> 2, 1, 1) != 0 ||
        !isfinite(approxY))
        return -1;
    return approxY >= MIN_SPAWN_SURFACE_Y;
}

static int *prepare_independent_checks(CriteriaConfig *c, Layout l)
{
    int *memberGroups = malloc((l.spawn ? l.spawn : 1) * sizeof(*memberGroups));
    if (!memberGroups) return NULL;
    for (size_t i = 0; i < c->biomes.count; i++) {
        memberGroups[l.biome + i] = c->biomes.items[i].groupIndex;
        c->biomes.items[i].groupIndex = STATS_INDEPENDENT_GROUP_INDEX;
    }
    for (size_t i = 0; i < c->structuresDistance.count; i++) {
        memberGroups[l.distance + i] = c->structuresDistance.items[i].groupIndex;
        c->structuresDistance.items[i].groupIndex = STATS_INDEPENDENT_GROUP_INDEX;
    }
    for (size_t i = 0; i < c->structuresAmount.count; i++) {
        memberGroups[l.amount + i] = c->structuresAmount.items[i].groupIndex;
        c->structuresAmount.items[i].groupIndex = STATS_INDEPENDENT_GROUP_INDEX;
    }
    for (size_t i = 0; i < c->vaultLoot.count; i++) {
        memberGroups[l.vault + i] = c->vaultLoot.items[i].groupIndex;
        c->vaultLoot.items[i].groupIndex = STATS_INDEPENDENT_GROUP_INDEX;
    }
    return memberGroups;
}

static void process_sample(uint64_t seed, int smallBiomes, CriteriaConfig *c,
                           const VaultLootEngine *engine, Layout l,
                           const int *memberGroups, Counts *totals,
                           int8_t *status, int *groupPasses, int *groupErrors)
{
    /* All rules are evaluated regardless of other rules' outcomes. */
    for (size_t i = 0; i < c->vaultLoot.count; i++) {
        status[l.vault + i] = (int8_t)vault_loot_filter_match(
            engine, seed, &c->vaultLoot.items[i].rule);
    }

    Generator ow, nether, end;
    setupGenerator(&ow, MC, 0);
    setupGenerator(&nether, MC, 0);
    setupGenerator(&end, MC, 0);
    ow.smallBiomes = smallBiomes;
    applySeed(&ow, DIM_OVERWORLD, seed);
    applySeed(&nether, DIM_NETHER, seed);
    applySeed(&end, DIM_END, seed);

    StructureResult d = checkStructureDistanceRequirements(
        &c->structuresDistance, MC, seed, &ow, &nether, &end, NULL, 0);
    for (size_t i = 0; i < c->structuresDistance.count; i++)
        status[l.distance + i] = d.checks ? (int8_t)structure_status(d.checks[i].status) : -1;
    freeStructureResult(&d);

    int spawnStatus = check_spawn_y(&ow, seed);

    BiomeResult b = checkBiomeRequirements(
        &c->biomes, &ow, &nether, &end, seed, 16, NULL, 0);
    for (size_t i = 0; i < c->biomes.count; i++)
        status[l.biome + i] = b.checks ? (int8_t)biome_status(b.checks[i].status) : -1;
    freeBiomeResult(&b);

    StructureResult a = checkStructureAmountRequirements(
        &c->structuresAmount, MC, seed, &ow, &nether, &end, NULL, 0);
    for (size_t i = 0; i < c->structuresAmount.count; i++)
        status[l.amount + i] = a.checks ? (int8_t)structure_status(a.checks[i].status) : -1;
    freeStructureResult(&a);

    memset(groupPasses, 0, (c->groupCount ? c->groupCount : 1) * sizeof(*groupPasses));
    memset(groupErrors, 0, (c->groupCount ? c->groupCount : 1) * sizeof(*groupErrors));
    int anyFailed = 0, anyError = 0;
    for (size_t i = 0; i < l.spawn; i++) {
        int s = status[i];
        record(&totals[i], s);
        int gi = memberGroups[i];
        if (gi >= 0 && (size_t)gi < c->groupCount) {
            if (s > 0) groupPasses[gi]++;
            else if (s < 0) groupErrors[gi]++;
        } else {
            if (s == 0) anyFailed = 1;
            else if (s < 0) anyError = 1;
        }
    }
    record(&totals[l.spawn], spawnStatus);
    if (spawnStatus == 0) anyFailed = 1;
    else if (spawnStatus < 0) anyError = 1;

    for (size_t i = 0; i < c->groupCount; i++) {
        int needed = c->groups[i].minCount;
        int groupStatus;
        if (groupPasses[i] >= needed) groupStatus = 1;
        else if (groupPasses[i] + groupErrors[i] >= needed) groupStatus = -1;
        else groupStatus = 0;
        record(&totals[l.groups + i], groupStatus);
        if (groupStatus == 0) anyFailed = 1;
        else if (groupStatus < 0) anyError = 1;
    }
    record(&totals[l.overall], anyFailed ? 0 : anyError ? -1 : 1);
}

static int worker(uint64_t samples, const char *json, const char *tmp, int smallBiomes)
{
    CriteriaConfig c = {0};
    char err[512];
    if (!loadCriteriaConfig(json, &c, err, sizeof(err))) {
        fprintf(stderr, "Could not load criteria: %s\n", err);
        return 2;
    }
    Layout l = make_layout(&c);
    int *groupsByCriterion = prepare_independent_checks(&c, l);
    Counts *counts = calloc(l.total, sizeof(*counts));
    int8_t *status = malloc((l.spawn ? l.spawn : 1) * sizeof(*status));
    int *groupPass = calloc(c.groupCount ? c.groupCount : 1, sizeof(*groupPass));
    int *groupErr = calloc(c.groupCount ? c.groupCount : 1, sizeof(*groupErr));
    int ok = groupsByCriterion && counts && status && groupPass && groupErr;
    VaultLootEngine engine = {0};
    if (ok && c.vaultLoot.count && !vault_loot_engine_init(&engine)) {
        fprintf(stderr, "Could not initialize vault loot tables.\n");
        ok = 0;
    }
    Xoroshiro rng;
    uint64_t randomSeed = 0;
    if (ok && !os_random_bytes(&randomSeed, sizeof(randomSeed))) {
        fprintf(stderr, "Could not initialize random seed generator.\n");
        ok = 0;
    }
    if (ok) {
        xSetSeed(&rng, randomSeed);
        for (uint64_t i = 0; i < samples; i++) {
            process_sample(xNextLong(&rng), smallBiomes, &c, &engine, l,
                           groupsByCriterion, counts, status, groupPass, groupErr);
            if ((i + 1) % 100000 == 0)
                fprintf(stderr, "Worker sampled %" PRIu64 " seeds\n", i + 1);
        }
        FILE *f = fopen(tmp, "wb");
        if (!f) { perror("Opening worker stats"); ok = 0; }
        else {
            WorkerHeader h = {{'M','I','R','S','T','A','T','1'}, samples,
                              (uint64_t)l.spawn, (uint64_t)c.groupCount};
            if (fwrite(&h, sizeof(h), 1, f) != 1 ||
                fwrite(counts, sizeof(*counts), l.total, f) != l.total)
                ok = 0;
            if (fclose(f)) ok = 0;
        }
    }
    free(groupErr);
    free(groupPass);
    free(status);
    free(counts);
    free(groupsByCriterion);
    freeCriteriaConfig(&c);
    return ok ? 0 : 2;
}

static void csv_field(FILE *f, const char *s)
{
    fputc('"', f);
    if (s) for (; *s; ++s) {
        if (*s == '"') fputc('"', f);
        fputc(*s, f);
    }
    fputc('"', f);
}

typedef struct {
    const char *type;
    const char *name;
    const char *group;
    int minimum;                 /* only meaningful for group summary */
    size_t position;
    int difficulty;
} Row;

static const Counts *sortCounts;
static int compare_rows(const void *aa, const void *bb)
{
    const Row *a = aa, *b = bb;
    const Counts *ca = &sortCounts[a->position], *cb = &sortCounts[b->position];
    uint64_t na = ca->passed + ca->failed;
    uint64_t nb = cb->passed + cb->failed;
    double pa = na ? (double)ca->passed / (double)na : 2.0;
    double pb = nb ? (double)cb->passed / (double)nb : 2.0;
    if (pa < pb) return -1;
    if (pa > pb) return 1;
    return strcmp(a->name, b->name);
}

static void write_row(FILE *f, const Row *row, const Counts *s,
                      uint64_t samples, size_t rank)
{
    uint64_t valid = s->passed + s->failed;
    fprintf(f, "%zu,", rank);
    csv_field(f, row->type); fputc(',', f);
    csv_field(f, row->name); fputc(',', f);
    csv_field(f, row->group); fputc(',', f);
    if (row->minimum > 0) fprintf(f, "%d", row->minimum);
    fputc(',', f);
    if (strcmp(row->type, "group") && strcmp(row->type, "spawn_height") &&
        strcmp(row->type, "overall"))
        fprintf(f, "%d", row->difficulty);
    fprintf(f, ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",",
            samples, s->passed, s->failed, s->errors);
    if (valid) fprintf(f, "%.9f", 100.0 * (double)s->passed / (double)valid);
    fputc(',', f);
    if (valid) fprintf(f, "%.9f", 100.0 * (double)s->failed / (double)valid);
    fputc(',', f);
    if (s->passed) fprintf(f, "%.3f", (double)valid / (double)s->passed);
    fputc('\n', f);
}

static int save_csv(const char *path, const CriteriaConfig *c, Layout l,
                    const Counts *counts, uint64_t samples)
{
    Row *rows = calloc(l.overall ? l.overall : 1, sizeof(*rows));
    if (!rows) return 0;
    size_t n = 0;
    for (size_t i = 0; i < c->biomes.count; i++) {
        const BiomeCriterion *x = &c->biomes.items[i];
        rows[n++] = (Row){"biome", x->name, x->group, 0, l.biome + i, x->difficulty};
    }
    for (size_t i = 0; i < c->structuresDistance.count; i++) {
        const StructureDistanceCriterion *x = &c->structuresDistance.items[i];
        rows[n++] = (Row){"structure_distance", x->name, x->group, 0, l.distance + i, x->difficulty};
    }
    for (size_t i = 0; i < c->structuresAmount.count; i++) {
        const StructureAmountCriterion *x = &c->structuresAmount.items[i];
        rows[n++] = (Row){"structure_amount", x->name, x->group, 0, l.amount + i, x->difficulty};
    }
    for (size_t i = 0; i < c->vaultLoot.count; i++) {
        const VaultLootCriterion *x = &c->vaultLoot.items[i];
        rows[n++] = (Row){"vault_loot", x->name, x->group, 0, l.vault + i, x->difficulty};
    }
    rows[n++] = (Row){"spawn_height", "spawn_surface_y_at_least_64", NULL, 0, l.spawn, 0};
    for (size_t i = 0; i < c->groupCount; i++) {
        const CriterionGroup *g = &c->groups[i];
        rows[n++] = (Row){"group", g->name, NULL, g->minCount, l.groups + i, 0};
    }
    sortCounts = counts;
    qsort(rows, n, sizeof(*rows), compare_rows);
    FILE *f = fopen(path, "w");
    if (!f) { perror("Opening CSV"); free(rows); return 0; }
    fprintf(f, "rank,type,name,group,group_min_count,configured_difficulty,sampled_seeds,passed,failed,errors,pass_rate_percent,reject_rate_percent,estimated_one_in\n");
    for (size_t i = 0; i < n; i++) write_row(f, &rows[i], &counts[rows[i].position], samples, i + 1);
    Row overall = {"overall", "all_mandatory_and_groups_and_spawn", NULL, 0, l.overall, 0};
    write_row(f, &overall, &counts[l.overall], samples, 0);
    int ok = !ferror(f);
    if (fclose(f)) ok = 0;
    free(rows);
    return ok;
}

static int merge_stats(const char *path, Counts *all, Layout l,
                       uint64_t expectedSamples, uint64_t groups)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror("Reading worker stats"); return 0; }
    WorkerHeader h;
    int ok = fread(&h, sizeof(h), 1, f) == 1 &&
             memcmp(h.magic, "MIRSTAT1", 8) == 0 &&
             h.criteria == l.spawn && h.groups == groups &&
             h.samples == expectedSamples;
    Counts *partial = calloc(l.total, sizeof(*partial));
    if (!partial) ok = 0;
    if (ok && fread(partial, sizeof(*partial), l.total, f) != l.total) ok = 0;
    if (ok) {
        for (size_t i = 0; i < l.total; i++) {
            all[i].passed += partial[i].passed;
            all[i].failed += partial[i].failed;
            all[i].errors += partial[i].errors;
        }
    }
    free(partial);
    fclose(f);
    return ok;
}

int main(int argc, char **argv)
{
    if (argc == 6 && !strcmp(argv[1], "--worker")) {
        uint64_t samples;
        int small;
        if (!parse_u64(argv[2], &samples) || !samples ||
            !parse_bool(argv[5], &small)) return 2;
        return worker(samples, argv[3], argv[4], small);
    }
    if (argc != 5 && argc != 6) {
        fprintf(stderr, "Usage: %s <number-of-seeds> <criteria.json> <output.csv> <processes> [true|false]\n", argv[0]);
        return 1;
    }
    uint64_t samples, processes;
    int small = 0;
    if (!parse_u64(argv[1], &samples) || samples == 0 ||
        !parse_u64(argv[4], &processes) || processes == 0 ||
        processes > INT_MAX || (argc == 6 && !parse_bool(argv[5], &small))) {
        fprintf(stderr, "Invalid number-of-seeds, processes, or small-biomes option.\n");
        return 1;
    }
    CriteriaConfig c = {0};
    char error[512];
    if (!loadCriteriaConfig(argv[2], &c, error, sizeof(error))) {
        fprintf(stderr, "Could not load criteria: %s\n", error);
        return 1;
    }
    Layout l = make_layout(&c);
    Counts *total = calloc(l.total, sizeof(*total));
    size_t workers = (size_t)(processes < samples ? processes : samples);
    char **tempPaths = calloc(workers, sizeof(*tempPaths));
#ifdef _WIN32
    intptr_t *pids = calloc(workers, sizeof(*pids));
#else
    pid_t *pids = calloc(workers, sizeof(*pids));
#endif
    if (!total || !tempPaths || !pids) {
        fprintf(stderr, "Out of memory.\n");
        free(total); free(tempPaths); free(pids); freeCriteriaConfig(&c);
        return 1;
    }
    int failed = 0;
    size_t launched = 0;
    uint64_t base = samples / workers, remainder = samples % workers;
    fprintf(stderr, "Sampling %" PRIu64 " seeds across %zu workers; %zu criteria + %zu groups + spawn check.\n",
            samples, workers, l.spawn, c.groupCount);
    for (size_t i = 0; i < workers; i++) {
        uint64_t chunk = base + (i < remainder ? 1 : 0);
        size_t len = strlen(argv[3]) + 64;
        tempPaths[i] = malloc(len);
        if (!tempPaths[i]) { failed = 1; break; }
        snprintf(tempPaths[i], len, "%s.worker.%zu.tmp", argv[3], i);
        char chunkBuf[32], smallBuf[8];
        snprintf(chunkBuf, sizeof(chunkBuf), "%" PRIu64, chunk);
        snprintf(smallBuf, sizeof(smallBuf), "%d", small);
        const char *child[] = {argv[0], "--worker", chunkBuf, argv[2],
                               tempPaths[i], smallBuf, NULL};
#ifdef _WIN32
        intptr_t pid = _spawnvp(_P_NOWAIT, argv[0], child);
        if (pid == -1) { perror("Starting worker"); failed = 1; break; }
        pids[i] = pid;
#else
        pid_t pid = fork();
        if (pid == 0) {
            execvp(argv[0], (char *const *)child);
            perror("Starting worker");
            _exit(127);
        }
        if (pid < 0) { perror("fork"); failed = 1; break; }
        pids[i] = pid;
#endif
        launched++;
    }
    for (size_t i = 0; i < launched; i++) {
#ifdef _WIN32
        int exitcode = -1;
        if (_cwait(&exitcode, pids[i], 0) == -1 || exitcode != 0) failed = 1;
#else
        int exitcode = 0;
        if (waitpid(pids[i], &exitcode, 0) < 0 || !WIFEXITED(exitcode) || WEXITSTATUS(exitcode) != 0)
            failed = 1;
#endif
    }
    if (!failed) {
        for (size_t i = 0; i < workers; i++) {
            uint64_t chunk = base + (i < remainder ? 1 : 0);
            if (!merge_stats(tempPaths[i], total, l, chunk, c.groupCount))
                failed = 1;
        }
    }
    if (!failed && !save_csv(argv[3], &c, l, total, samples)) failed = 1;
    if (!failed) fprintf(stderr, "Saved independent criterion statistics to %s\n", argv[3]);
    for (size_t i = 0; i < workers; i++) {
        if (tempPaths[i]) { remove(tempPaths[i]); free(tempPaths[i]); }
    }
    free(pids); free(tempPaths); free(total); freeCriteriaConfig(&c);
    return failed ? 2 : 0;
}
