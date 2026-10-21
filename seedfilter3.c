
#include "finders.h"
#include "generator.h"
#include "biomes.h"
#include "noise.h"
#include "util.h"
#include "biomeareachecker.h"

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
#include <io.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

#define MC MC_1_21
#define MAX_CITY_DIST       128
#define MAX_FORTRESS_DIST   128

static int floor_div(int a, int b)
{
    int q = a / b;
    int r = a % b;
    return q - (r != 0 && ((r < 0) != (b < 0)));
}

static double dist2(int x1, int z1, int x2, int z2)
{
    double dx = (double)x1 - x2;
    double dz = (double)z1 - z2;
    return dx * dx + dz * dz;
}

void freeBiomeResult(BiomeResult *result)
{
    if (!result)
        return;

    free(result->checks);
    result->checks = NULL;
    result->checkCount = 0;
    result->evaluatedCount = 0;
    result->passedCount = 0;
    result->failedCount = 0;
    result->allPassed = 0;
    result->hasError = 0;
}

static int find_near_structure(
    int structure, Generator *g, uint64_t seed,
    int cx, int cz, int radius, Pos *best, double *best_d2,
    const SurfaceNoise *end_surface)
{
    StructureConfig sc;

    if (!getStructureConfig(structure, MC, &sc))
        return 0;

    int region_blocks = sc.regionSize * 16;
    int rx0 = floor_div(cx - radius, region_blocks) - 1;
    int rz0 = floor_div(cz - radius, region_blocks) - 1;
    int rx1 = floor_div(cx + radius, region_blocks) + 1;
    int rz1 = floor_div(cz + radius, region_blocks) + 1;

    double nearest2 = (double)radius * radius;
    int found = 0;

    for (int rz = rz0; rz <= rz1; rz++) {
        for (int rx = rx0; rx <= rx1; rx++) {
            Pos p;

            if (!getStructurePos(structure, MC, seed, rx, rz, &p))
                continue;

            double d = dist2(p.x, p.z, cx, cz);

            if (d > nearest2)
                continue;

            if (!isViableStructurePos(structure, g, p.x, p.z, 0))
                continue;

            if (structure == End_City &&
                !isViableEndCityTerrain(g, end_surface, p.x, p.z))
                continue;

            nearest2 = d;
            *best = p;
            found = 1;
        }
    }

    if (found && best_d2)
        *best_d2 = nearest2;

    return found;
}

static void write_biome_results(
    FILE *out, uint64_t seed, const BiomeResult *result)
{
    fprintf(out,
        "BIOME_DEBUG seed=%" PRIu64
        " passed=%zu failed=%zu evaluated=%zu total=%zu\n",
        seed, result->passedCount, result->failedCount,
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

        fprintf(out, "  [%s] area=%.2f blocks^2 status=%s\n",
            check->name ? check->name : "(unnamed)",
            check->area, status);
    }

    fputc('\n', out);
}

/*
 * Returns 1 if the seed passes every check, 0 otherwise.
 * Debug candidates are written only when passedCount reaches threshold.
 */
static int test_seed(
    uint64_t seed,
    int smallBiomes,
    const BiomeCriteria *criteria,
    size_t biomethreshold,
    FILE *out)
{
    Generator ow, nether, end;

    setupGenerator(&ow, MC, 0);
    setupGenerator(&nether, MC, 0);
    setupGenerator(&end, MC, 0);

    ow.smallBiomes = smallBiomes;

    applySeed(&ow, DIM_OVERWORLD, seed);
    applySeed(&nether, DIM_NETHER, seed);
    applySeed(&end, DIM_END, seed);

    Pos spawn = {0, 0};

    BiomeResult biomeCheck = checkBiomeRequirements(
        criteria, &ow, &nether, &end,
        spawn.x, spawn.z, 16, 0);

    if (biomeCheck.hasError) {
        fprintf(stderr,
            "Biome criteria error for seed %" PRIu64 "\n", seed);
        freeBiomeResult(&biomeCheck);
        return 0;
    }

    if (!biomeCheck.allPassed) {
        if (biomeCheck.passedCount >= biomethreshold)
            write_biome_results(out, seed, &biomeCheck);

        freeBiomeResult(&biomeCheck);
        return 0;
    }

    freeBiomeResult(&biomeCheck);

    Pos fortress;
    double fortress_d2;

    if (!find_near_structure(
            Fortress, &nether, seed,
            0, 0, MAX_FORTRESS_DIST,
            &fortress, &fortress_d2, NULL))
        return 0;

    Pos sources[20];
    getFixedEndGateways(MC, seed, sources);

    EndNoise en;
    setEndSeed(&en, MC, seed);

    SurfaceNoise end_surface;
    initSurfaceNoise(&end_surface, DIM_END, seed);

    Pos gateway = getLinkedGatewayPos(
        &en, &end_surface, seed, sources[0]);

    Pos city;
    double city_d2;

    if (!find_near_structure(
            End_City, &end, seed,
            gateway.x, gateway.z, MAX_CITY_DIST,
            &city, &city_d2, &end_surface))
        return 0;

    fprintf(out,
        "FOUND seed=%" PRIu64
        " | fortress=%d,%d distance=%.0f"
        " | gateway=%d,%d"
        " | city=%d,%d distance=%.0f\n\n",
        seed,
        fortress.x, fortress.z, sqrt(fortress_d2),
        gateway.x, gateway.z,
        city.x, city.z, sqrt(city_d2));

    fflush(out);

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
 * executable --worker START COUNT CRITERIA THRESHOLD TEMP_FILE SMALL_BIOMES
 */
static int run_worker(int argc, char **argv)
{
    if (argc != 8)
        return 2;

    uint64_t start, count, threshold64;
    int smallBiomes;

    if (!parse_u64(argv[2], &start) ||
        !parse_u64(argv[3], &count) || count == 0 ||
        !parse_u64(argv[5], &threshold64) ||
        threshold64 > SIZE_MAX ||
        !parse_small_biomes(argv[7], &smallBiomes) ||
        start > UINT64_MAX - (count - 1)) {
        fprintf(stderr, "Invalid worker arguments.\n");
        return 2;
    }

    BiomeCriteria criteria = {0};
    char error[512];

    if (!loadBiomeCriteria(argv[4], &criteria, error, sizeof(error))) {
        fprintf(stderr, "Could not load criteria: %s\n", error);
        return 2;
    }

    FILE *out = fopen(argv[6], "w");

    if (!out) {
        perror("Could not open worker output");
        freeBiomeCriteria(&criteria);
        return 2;
    }

    for (uint64_t i = 0; i < count; i++) {
        test_seed(start + i, smallBiomes, &criteria,
                  (size_t)threshold64, out);

        if ((i + 1) % 100000 == 0) {
            fprintf(stderr,
                "Worker starting at %" PRIu64
                ": checked %" PRIu64 " seeds\n",
                start, i + 1);
        }
    }

    int failed = ferror(out);
    if (fclose(out) != 0)
        failed = 1;

    freeBiomeCriteria(&criteria);
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

    if (argc < 7 || argc > 8) {
        fprintf(stderr,
            "Usage: %s <starting-seed> <number-of-seeds> "
            "<criteria.json> <biomethreshold> <output-file> "
            "<processes> [true|false]\n",
            argv[0]);
        return 1;
    }

    uint64_t start, count, threshold64, processes64;

    if (!parse_u64(argv[1], &start) ||
        !parse_u64(argv[2], &count) || count == 0 ||
        !parse_u64(argv[4], &threshold64) ||
        !parse_u64(argv[6], &processes64) || processes64 == 0 ||
        processes64 > INT_MAX ||
        start > UINT64_MAX - (count - 1)) {
        fprintf(stderr, "Invalid numeric argument or seed range.\n");
        return 1;
    }

    if (threshold64 > SIZE_MAX) {
        fprintf(stderr, "biomethreshold is too large.\n");
        return 1;
    }

    int smallBiomes = 0;

    if (argc == 8 && !parse_small_biomes(argv[7], &smallBiomes)) {
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

    size_t path_len = strlen(argv[5]) + 48;

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
                 "%s.worker.%d.tmp", argv[5], i);
    }

    uint64_t base = count / (uint64_t)workers;
    uint64_t remainder = count % (uint64_t)workers;
    uint64_t offset = 0;

#ifdef _WIN32
    intptr_t *pids = calloc((size_t)workers, sizeof(intptr_t));
#else
    pid_t *pids = calloc((size_t)workers, sizeof(pid_t));
#endif

    if (!pids) {
        perror("calloc");
        return 1;
    }

    int launched = 0;
    int failed = 0;

    fprintf(stderr,
        "Searching %" PRIu64 " seeds with %d worker processes.\n",
        count, workers);

    for (int i = 0; i < workers; i++) {
        uint64_t chunk = base + ((uint64_t)i < remainder ? 1 : 0);
        uint64_t chunk_start = start + offset;
        offset += chunk;

        char start_str[32], count_str[32], threshold_str[32];
        char small_str[8];

        snprintf(start_str, sizeof(start_str), "%" PRIu64, chunk_start);
        snprintf(count_str, sizeof(count_str), "%" PRIu64, chunk);
        snprintf(threshold_str, sizeof(threshold_str),
                 "%" PRIu64, threshold64);
        snprintf(small_str, sizeof(small_str), "%d", smallBiomes);

        char *child_argv[] = {
            argv[0], "--worker", start_str, count_str,
            argv[3], threshold_str, temp_paths[i], small_str, NULL
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
        if (merge_files(argv[5], temp_paths, workers))
            fprintf(stderr, "Results written to %s\n", argv[5]);
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
