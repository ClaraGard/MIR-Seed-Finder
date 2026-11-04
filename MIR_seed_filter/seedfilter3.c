
#include "finders.h"
#include "generator.h"
#include "biomes.h"
#include "noise.h"
#include "util.h"
#include "biomeareachecker.h"
#include "criterialoader.h"
#include "vault_loot_filter.h"
#include "loot/items.h"
#include "loot/loot_functions.h"
#include "structurechecker.h"
#include "rng.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#include <bcrypt.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/random.h>
#endif

#define MC MC_26_3
/* Mandatory for every tested seed; intentionally NOT configurable by JSON. */
#define MIN_SPAWN_SURFACE_Y 64

static int64_t seed_as_i64(uint64_t bits)
{
    if (bits <= INT64_MAX)
        return (int64_t)bits;

    return INT64_MIN +
        (int64_t)(bits - (UINT64_C(1) << 63));
}

static int os_random_bytes(void *buffer, size_t length)
{
#ifdef _WIN32
    if (length > ULONG_MAX)
        return 0;

    return BCryptGenRandom(
        NULL,
        (PUCHAR)buffer,
        (ULONG)length,
        BCRYPT_USE_SYSTEM_PREFERRED_RNG
    ) == 0;
#else
    unsigned char *p = (unsigned char *)buffer;

    while (length > 0) {
        ssize_t n = getrandom(p, length, 0);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return 0;
        }

        if (n == 0)
            return 0;

        p += (size_t)n;
        length -= (size_t)n;
    }

    return 1;
#endif
}

/* Initialize each worker's PRNG from the OS random number generator. */
static int init_rng(Xoroshiro *rng)
{
    uint64_t seed;

    if (!os_random_bytes(&seed, sizeof(seed)))
        return 0;

    xSetSeed(rng, seed);
    return 1;
}


static void write_biome_results(
    FILE *out, uint64_t seed, const char *label, const BiomeResult *result)
{
    fprintf(out,
        "%s seed=%" PRId64
        " passed=%zu failed=%zu evaluated=%zu total=%zu\n",
        label, seed_as_i64(seed), result->passedCount, result->failedCount,
        result->evaluatedCount, result->checkCount);

    for (size_t i = 0; i < result->checkCount; i++) {
        const BiomeCheckResult *check = &result->checks[i];
        const char *status;

        switch (check->status) {
        case BIOME_CHECK_PASSED:
            status = "PASS";
            break;
        case BIOME_CHECK_FAILED:
            status = "FAIL";
            break;
        case BIOME_CHECK_ERROR:
            status = "ERROR";
            break;
        default:
            status = "NOT EVALUATED";
            break;
        }

        /* Area checks exit as soon as a bound is met or impossible, so
         * they do not compute an exact area. Avoid printing a fake value. */
        fprintf(out, "  [%s] status=%s\n",
            check->name ? check->name : "(unnamed)", status);
    }

    fputc('\n', out);
}

/* Writes position/distance or count results for diagnostics and matches. */
static void write_structure_results(FILE *out, uint64_t seed,
                                    const char *label,
                                    const StructureResult *result,
                                    int isAmount)
{
    fprintf(out, "%s seed=%" PRId64
                 " passed=%zu failed=%zu evaluated=%zu total=%zu\n",
            label, seed_as_i64(seed), result->passedCount, result->failedCount,
            result->evaluatedCount, result->checkCount);
    for (size_t i = 0; i < result->checkCount; i++) {
        const StructureCheckEntry *c = &result->checks[i];
        const char *status = c->status == STRUCTURE_CHECK_PASSED ? "PASS" :
                             c->status == STRUCTURE_CHECK_FAILED ? "FAIL" :
                             c->status == STRUCTURE_CHECK_ERROR ? "ERROR" : "NOT EVALUATED";
        if (c->status == STRUCTURE_CHECK_NOT_EVALUATED) {
            fprintf(out, "  [%s] status=%s\n",
                    c->name ? c->name : "(unnamed)", status);
        } else if (isAmount) {
            fprintf(out, "  [%s] count=%s%zu status=%s\n",
                    c->name ? c->name : "(unnamed)",
                    c->countExact ? "" : ">=", c->count, status);
        } else if (c->distance >= 0) {
            fprintf(out, "  [%s] pos=(%d,%d) distance=%.2f status=%s\n",
                    c->name ? c->name : "(unnamed)",
                    c->position.x, c->position.z, c->distance, status);
        } else {
            fprintf(out, "  [%s] no structure in checked radius status=%s\n",
                    c->name ? c->name : "(unnamed)", status);
        }
    }
    fputc('\n', out);
}

/* Detailed vault output is computed only for FOUND seeds so normal searches
 * avoid allocating per-window counters for millions of rejected seeds.
 * A rule is evaluated from its sequence start, just as it is during filtering.
 */
static int write_vault_results(FILE *out, uint64_t seed,
                               const CriteriaConfig *config,
                               const VaultLootEngine *engine,
                               const unsigned char *statuses)
{
    size_t passed = 0, failed = 0;
    for (size_t i = 0; i < config->vaultLoot.count; ++i) {
        passed += statuses[i] == 1;
        failed += statuses[i] == 2;
    }
    fprintf(out, "VAULT_LOOT seed=%" PRId64
                 " passed=%zu failed=%zu evaluated=%zu total=%zu\n",
            seed_as_i64(seed), passed, failed,
            passed + failed, config->vaultLoot.count);

    for (size_t i = 0; i < config->vaultLoot.count; ++i) {
        const VaultLootCriterion *criterion = &config->vaultLoot.items[i];
        const VaultLootRule *rule = &criterion->rule;
        if (statuses[i] != 1) {
            fprintf(out, "  [%s] status=%s\n", criterion->name,
                    statuses[i] == 2 ? "FAIL (group alternative)" : "NOT EVALUATED (group satisfied)");
            continue;
        }
        if (rule->repetitions <= 0 ||
            (size_t)rule->repetitions > SIZE_MAX / sizeof(int64_t))
            return 0;

        int64_t *counts = malloc((size_t)rule->repetitions * sizeof(*counts));
        if (!counts)
            return 0;
        int result = vault_loot_filter_window_counts(
            engine, seed, rule, counts, (size_t)rule->repetitions);
        if (result != 1) {
            free(counts);
            return 0; /* A prior match should always produce the same result. */
        }

        const char *target_name = rule->target_kind == VAULT_LOOT_TARGET_ENCHANTMENT
            ? get_enchantment_name((enum Enchantment)rule->target_id)
            : global_id2item_name(rule->target_id, MC);
        fprintf(out, "  [%s] type=%s target=%s:%s min_count=%d "
                     "vaults_per_window=%d repetitions=%d status=PASS\n",
                criterion->name ? criterion->name : "(unnamed)",
                rule->vault_kind == VAULT_LOOT_OMINOUS ? "ominous" : "normal",
                rule->target_kind == VAULT_LOOT_TARGET_ENCHANTMENT ? "enchantment" : "item",
                target_name ? target_name : "(unknown)",
                rule->min_count, rule->vaults_per_window, rule->repetitions);

        for (int w = 0; w < rule->repetitions; ++w) {
            int64_t from = (int64_t)w * rule->vaults_per_window + 1;
            int64_t to = from + rule->vaults_per_window - 1;
            fprintf(out, "    openings=%" PRId64 "-%" PRId64
                         " count=%" PRId64 " required>=%d status=PASS\n",
                    from, to, counts[w], rule->min_count);
        }
        free(counts);
    }
    fputc('\n', out);
    return !ferror(out);
}

/* Vaults first: reject nonmatching seeds before generating biome/structure maps. */
static int test_seed(uint64_t seed, int smallBiomes,
                     const CriteriaConfig *config,
                     const VaultLootEngine *vaultEngine,
                     size_t biomethreshold, FILE *out)
{
    /* One group counter per named group, on the stack. No per-seed heap
     * allocations or string lookups, regardless of cross-phase membership. */
    CriterionGroupProgress groupProgress[config->groupCount ? config->groupCount : 1];
    for (size_t i = 0; i < config->groupCount; ++i) {
        groupProgress[i].passed = 0;
        groupProgress[i].remaining = config->groups[i].memberCount;
    }
    CriterionGroupRuntime groups = {
        config->groups, groupProgress, config->groupCount
    };

    /* Vault members can be optional when they belong to mixed groups.
     * 0=skipped, 1=pass, 2=fail. Only write diagnostics for FOUND seeds. */
    unsigned char vaultStatuses[config->vaultLoot.count ? config->vaultLoot.count : 1];
    memset(vaultStatuses, 0, sizeof(vaultStatuses));
    for (size_t i = 0; i < config->vaultLoot.count; ++i) {
        const VaultLootCriterion *criterion = &config->vaultLoot.items[i];
        if (!criterionGroupShouldCheck(&groups, criterion->groupIndex))
            continue;
        int match = vault_loot_filter_match(vaultEngine, seed, &criterion->rule);
        if (match < 0) {
            fprintf(stderr, "Vault loot criterion error for seed %" PRIu64 "\n", seed);
            return -1;
        }
        vaultStatuses[i] = match ? 1 : 2;
        if (!criterionGroupRecord(&groups, criterion->groupIndex, match))
            return 0;
    }

    Generator ow, nether, end;
    setupGenerator(&ow, MC, 0);
    setupGenerator(&nether, MC, 0);
    setupGenerator(&end, MC, 0);
    ow.smallBiomes = smallBiomes;
    applySeed(&ow, DIM_OVERWORLD, seed);
    applySeed(&nether, DIM_NETHER, seed);
    applySeed(&end, DIM_END, seed);

    StructureResult distances = checkStructureDistanceRequirements(
        &config->structuresDistance, MC, seed,
        &ow, &nether, &end, &groups, 0);
    if (distances.hasError) {
        fprintf(stderr, "Structure distance criteria error for seed %" PRIu64 "\n", seed);
        freeStructureResult(&distances);
        return 0;
    }
    if (!distances.allPassed) {
        freeStructureResult(&distances);
        return 0;
    }

    BiomeResult biomes = checkBiomeRequirements(
        &config->biomes, &ow, &nether, &end,
        seed, 16, &groups, 0);
    if (biomes.hasError) {
        fprintf(stderr, "Biome criteria error for seed %" PRIu64 "\n", seed);
        freeBiomeResult(&biomes);
        freeStructureResult(&distances);
        return 0;
    }
    if (!biomes.allPassed) {
        if (biomes.passedCount >= biomethreshold)
            write_biome_results(out, seed, "BIOME_DEBUG", &biomes);
        freeBiomeResult(&biomes);
        freeStructureResult(&distances);
        return 0;
    }

    /* Mandatory spawn altitude prefilter, after structure distance checks.
     * Cubiomes gives an approximate surface Y, not exact Minecraft blocks. */
    Pos spawn = getSpawn(&ow);
    float approxSpawnY = -1.0f;
    SurfaceNoise noise;
    initSurfaceNoise(&noise, DIM_OVERWORLD, seed);
    if (mapApproxHeight(&approxSpawnY, NULL, &ow, &noise,
                        spawn.x >> 2, spawn.z >> 2, 1, 1) != 0 ||
        !isfinite(approxSpawnY) ||
        approxSpawnY < MIN_SPAWN_SURFACE_Y) {
        freeStructureResult(&distances);
        return 0;
    }

    StructureResult amounts = checkStructureAmountRequirements(
        &config->structuresAmount, MC, seed,
        &ow, &nether, &end, &groups, 0);
    if (amounts.hasError) {
        fprintf(stderr, "Structure amount criteria error for seed %" PRIu64 "\n", seed);
        freeStructureResult(&amounts);
        freeBiomeResult(&biomes);
        freeStructureResult(&distances);
        return 0;
    }
    if (!amounts.allPassed) {
        if (biomes.passedCount >= biomethreshold)
            write_structure_results(out, seed, "STRUCTURE_AMOUNT_DEBUG", &amounts, 1);
        freeStructureResult(&amounts);
        freeBiomeResult(&biomes);
        freeStructureResult(&distances);
        return 0;
    }

    if (!criterionGroupsSatisfied(&groups)) {
        freeStructureResult(&amounts);
        freeBiomeResult(&biomes);
        freeStructureResult(&distances);
        return 0;
    }

    fprintf(out, "FOUND seed=%" PRId64 "\n", seed_as_i64(seed));
    for (size_t i = 0; i < config->groupCount; ++i)
        fprintf(out, "GROUP [%s] passed=%d required=%d members=%d status=PASS\n",
                config->groups[i].name, groupProgress[i].passed,
                config->groups[i].minCount, config->groups[i].memberCount);
    fprintf(out, "SPAWN_HEIGHT pos=(%d,%d) approx_y=%.2f min_y=%d status=PASS\n",
            spawn.x, spawn.z, (double)approxSpawnY, MIN_SPAWN_SURFACE_Y);
    if (config->vaultLoot.count > 0 &&
        !write_vault_results(out, seed, config, vaultEngine, vaultStatuses)) {
        fprintf(stderr, "Vault loot reporting error for seed %" PRIu64 "\n", seed);
        freeStructureResult(&amounts);
        freeBiomeResult(&biomes);
        freeStructureResult(&distances);
        return -1;
    }
    write_structure_results(out, seed, "STRUCTURES_DISTANCE", &distances, 0);
    write_biome_results(out, seed, "BIOMES", &biomes);
    write_structure_results(out, seed, "STRUCTURES_AMOUNT", &amounts, 1);
    fprintf(out, "--------------------------------------\n");
    fflush(out);
    freeStructureResult(&amounts);
    freeBiomeResult(&biomes);
    freeStructureResult(&distances);
    return 1;
}

static int parse_u64(const char *s, uint64_t *value)
{
    char *end = NULL;

    if (!s || !*s || *s == '-')
        return 0;

    errno = 0;
    unsigned long long v = strtoull(s, &end, 0);

    if (errno || !end || *end != '\0')
        return 0;

    *value = (uint64_t)v;
    return 1;
}

static int parse_small_biomes(const char *s, int *value)
{
    if (!strcmp(s, "true") || !strcmp(s, "1")) {
        *value = 1;
        return 1;
    }

    if (!strcmp(s, "false") || !strcmp(s, "0")) {
        *value = 0;
        return 1;
    }

    return 0;
}

/*
 * Internal worker mode:
* executable --worker COUNT CRITERIA THRESHOLD TEMP_FILE SMALL_BIOMES 
*/
static int run_worker(int argc, char **argv)
{
    if (argc != 7)
        return 2;

    uint64_t count, threshold64;
    int smallBiomes;

    if (!parse_u64(argv[2], &count) || count == 0 ||
        !parse_u64(argv[4], &threshold64) ||
        threshold64 > SIZE_MAX ||
        !parse_small_biomes(argv[6], &smallBiomes)) {
        fprintf(stderr, "Invalid worker arguments.\n");
        return 2;
    }

    CriteriaConfig criteria = {0};
    char error[512];

    if (!loadCriteriaConfig(argv[3], &criteria, error, sizeof(error))) {
        fprintf(stderr, "Could not load criteria: %s\n", error);
        return 2;
    }

    /* Worker processes have independent address spaces. Initialize the
     * loot-table templates once per worker, not once per seed. */
    VaultLootEngine vaultEngine = {0};
    if (criteria.vaultLoot.count > 0 && !vault_loot_engine_init(&vaultEngine)) {
        fprintf(stderr, "Could not initialize vault loot tables.\n");
        freeCriteriaConfig(&criteria);
        return 2;
    }

    FILE *out = fopen(argv[5], "w");

    if (!out) {
        perror("Could not open worker output");
        freeCriteriaConfig(&criteria);
        return 2;
    }

    Xoroshiro rng;

    if (!init_rng(&rng)) {
        fprintf(stderr, "Failed to initialize worker RNG.\n");
        fclose(out);
        freeCriteriaConfig(&criteria);
        return 2;
    }

    for (uint64_t i = 0; i < count; i++) {
        uint64_t seed = xNextLong(&rng);

        int matched = test_seed(seed, smallBiomes, &criteria,
                                &vaultEngine, (size_t)threshold64, out);
        if (matched < 0) {
            /* An invalid vault evaluation is a worker failure, not a miss. */
            fclose(out);
            freeCriteriaConfig(&criteria);
            return 2;
        }

        if ((i + 1) % 100000 == 0) {
            fprintf(stderr,
                "Worker checked %" PRIu64 " seeds\n",
                i + 1);
        }
    }

    int failed = ferror(out);

    if (fclose(out) != 0)
        failed = 1;

    freeCriteriaConfig(&criteria);
    return failed ? 2 : 0;
}

static int merge_files(
    const char *output_path, char **temp_paths, int workers)
{
    FILE *out = fopen(output_path, "wb");

    if (!out) {
        perror("Could not open final output file");
        return 0;
    }

    int ok = 1;
    char buffer[65536];

    for (int i = 0; i < workers; i++) {
        FILE *in = fopen(temp_paths[i], "rb");

        if (!in) {
            perror("Could not open worker temporary file");
            ok = 0;
            continue;
        }

        size_t n;
        while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0) {
            if (fwrite(buffer, 1, n, out) != n) {
                ok = 0;
                break;
            }
        }

        if (ferror(in))
            ok = 0;

        fclose(in);
    }

    if (fclose(out) != 0)
        ok = 0;

    return ok;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--worker"))
        return run_worker(argc, argv);

    if (argc < 6 || argc > 7) {
        fprintf(stderr,
            "Usage: %s <number-of-seeds> <criteria.json> "
            "<biomethreshold> <output-file> "
            "<processes> [true|false]\n",
            argv[0]);
        return 1;
    }

    uint64_t count, threshold64, processes64;

    if (!parse_u64(argv[1], &count) || count == 0 ||
        !parse_u64(argv[3], &threshold64) ||
        !parse_u64(argv[5], &processes64) ||
        processes64 == 0 || processes64 > INT_MAX) {
        fprintf(stderr, "Invalid numeric argument.\n");
        return 1;
    }

    if (threshold64 > SIZE_MAX) {
        fprintf(stderr, "biomethreshold is too large.\n");
        return 1;
    }

    int smallBiomes = 0;

    if (argc == 7 && !parse_small_biomes(argv[6], &smallBiomes)) {
        fprintf(stderr, "Biome option must be true, false, 1, or 0.\n");
        return 1;
    }

    int workers = processes64 < count
        ? (int)processes64 : (int)(count < INT_MAX ? count : INT_MAX);

    if ((uint64_t)workers > count)
        workers = (int)count;

    char **temp_paths = calloc((size_t)workers, sizeof(char *));
    if (!temp_paths) {
        perror("calloc");
        return 1;
    }

    size_t path_len = strlen(argv[4]) + 48;

    for (int i = 0; i < workers; i++) {
        temp_paths[i] = malloc(path_len);

        if (!temp_paths[i]) {
            perror("malloc");
            for (int j = 0; j < i; j++)
                free(temp_paths[j]);
            free(temp_paths);
            return 1;
        }

        snprintf(temp_paths[i], path_len,
                 "%s.worker.%d.tmp", argv[4], i);
    }

    uint64_t base = count / (uint64_t)workers;
    uint64_t remainder = count % (uint64_t)workers;

#ifdef _WIN32
    intptr_t *pids = calloc((size_t)workers, sizeof(intptr_t));
#else
    pid_t *pids = calloc((size_t)workers, sizeof(pid_t));
#endif

    if (!pids) {
        perror("calloc");
        for (int i = 0; i < workers; i++)
            free(temp_paths[i]);
        free(temp_paths);
        return 1;
    }

    int launched = 0;
    int failed = 0;

    fprintf(stderr,
        "Searching %" PRIu64 " seeds with %d worker processes.\n",
        count, workers);

    for (int i = 0; i < workers; i++) {
        uint64_t chunk = base + ((uint64_t)i < remainder ? 1 : 0);

        char count_str[32], threshold_str[32];
        char small_str[8];

        snprintf(count_str, sizeof(count_str), "%" PRIu64, chunk);
        snprintf(threshold_str, sizeof(threshold_str),
                "%" PRIu64, threshold64);
        snprintf(small_str, sizeof(small_str), "%d", smallBiomes);

        char *child_argv[] = {
            argv[0], "--worker", count_str,
            argv[2], threshold_str, temp_paths[i], small_str, NULL
        };

#ifdef _WIN32
        intptr_t pid = _spawnv(_P_NOWAIT, argv[0],
                               (const char * const *)child_argv);
        if (pid == -1) {
            perror("Could not start worker");
            failed = 1;
            break;
        }
        pids[i] = pid;
#else
        pid_t pid = fork();

        if (pid == 0) {
            execv(argv[0], child_argv);
            perror("execv");
            _exit(127);
        }

        if (pid < 0) {
            perror("fork");
            failed = 1;
            break;
        }

        pids[i] = pid;
#endif
        launched++;
    }

    for (int i = 0; i < launched; i++) {
#ifdef _WIN32
        int status = 0;
        if (_cwait(&status, pids[i], 0) == -1 || status != 0)
            failed = 1;
#else
        int status = 0;
        pid_t result;

        do {
            result = waitpid(pids[i], &status, 0);
        } while (result < 0 && errno == EINTR);

        if (result < 0 || !WIFEXITED(status) ||
            WEXITSTATUS(status) != 0)
            failed = 1;
#endif
    }

    if (!failed && launched == workers) {
        if (merge_files(argv[4], temp_paths, workers))
            fprintf(stderr, "Results written to %s\n", argv[4]);
        else
            failed = 1;
    } else {
        fprintf(stderr,
            "One or more workers failed; temporary files were retained.\n");
    }

    for (int i = 0; i < workers; i++) {
        if (!failed)
            remove(temp_paths[i]);
        free(temp_paths[i]);
    }

    free(temp_paths);
    free(pids);

    return failed ? 1 : 0;
}
