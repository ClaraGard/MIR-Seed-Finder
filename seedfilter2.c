
#include "finders.h"
#include "generator.h"
#include "biomes.h"
#include "noise.h"
#include "util.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <inttypes.h>
#include <string.h>

#define MC MC_1_21

// Search thresholds in blocks. Change these to taste.
#define MAX_CITY_DIST       128
#define MAX_FORTRESS_DIST   128

#include "biomeareachecker.h"

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

// Finds the closest viable structure within radius of (cx,cz).
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

    double limit2 = (double)radius * radius;
    double nearest2 = limit2;
    int found = 0;

    for (int rz = rz0; rz <= rz1; rz++) {
        for (int rx = rx0; rx <= rx1; rx++) {
            Pos p;

            if (!getStructurePos(structure, MC, seed, rx, rz, &p))
                continue;

            double d2 = dist2(p.x, p.z, cx, cz);
            if (d2 > nearest2)
                continue;

            if (!isViableStructurePos(structure, g, p.x, p.z, 0))
                continue;

            if (structure == End_City &&
                !isViableEndCityTerrain(g, end_surface, p.x, p.z))
                continue;

            nearest2 = d2;
            *best = p;
            found = 1;
        }
    }

    if (found && best_d2)
        *best_d2 = nearest2;

    return found;
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

static void write_biome_results(
    FILE *out,
    uint64_t seed,
    const BiomeResult *result)
{
    fprintf(out,
        "BIOME_DEBUG seed=%" PRIu64
        " passed=%zu evaluated=%zu total=%zu\n",
        seed,
        result->passedCount,
        result->evaluatedCount,
        result->checkCount);

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

        fprintf(out, "  [%s] area=%.2f blocks^2\n",
            check->name ? check->name : "(unnamed)",
            check->area);
        fprintf(out, "    status=%s\n", status);
    }

    fputc('\n', out);
    fflush(out);
}

static void test_seed(uint64_t seed, int smallBiomes, const BiomeCriteria *criteria)
{
    Generator ow, nether, end;

    setupGenerator(&ow, MC, 0);
    setupGenerator(&nether, MC, 0);
    setupGenerator(&end, MC, 0);

    // Apply custom biome noise only to the Overworld.
    ow.smallBiomes = smallBiomes;

    applySeed(&ow, DIM_OVERWORLD, seed);
    applySeed(&nether, DIM_NETHER, seed);
    applySeed(&end, DIM_END, seed);

    // 1. Approximate the actual Overworld spawn and reject unwanted biomes.
    Pos spawn = {0, 0};

    // Apply user-defined biome area criteria from the JSON file.
    // Scale 4 samples biome data at quart resolution.
    BiomeResult biomeCheck = checkBiomeRequirements(
    criteria, &ow, &nether, &end,
    spawn.x, spawn.z, 16, 0);
    
    if (biomeCheck.hasError) {
        fprintf(stderr,
            "Biome criteria check ran into an error for seed %" PRIu64 "\n",
            seed);

        freeBiomeResult(&biomeCheck);
        return;
    }

    if (!biomeCheck.allPassed) {
        return;
    }

    fprintf(stderr, "All biome checks passed for seed %" PRIu64 "\n", seed);
    freeBiomeResult(&biomeCheck);

    // 3. Find a viable Nether fortress near Nether origin.
    Pos fortress;
    double fortress_d2;

    if (!find_near_structure(Fortress, &nether, seed,
            0, 0, MAX_FORTRESS_DIST,
            &fortress, &fortress_d2, NULL))
        return;

    // 5. Calculate the linked outer gateway destination.
    Pos sources[20];
    getFixedEndGateways(MC, seed, sources);

    EndNoise en;
    setEndSeed(&en, MC, seed);

    SurfaceNoise end_surface;
    initSurfaceNoise(&end_surface, DIM_END, seed);

    Pos gateway = getLinkedGatewayPos(
        &en, &end_surface, seed, sources[0]);

    // 6. Find a viable End city close to that destination.
    Pos city;
    double city_d2;

    if (!find_near_structure(End_City, &end, seed,
            gateway.x, gateway.z, MAX_CITY_DIST,
            &city, &city_d2, &end_surface))
        return;

    printf(
        "FOUND SEED %" PRIu64
        " | fortress %d,%d (%.0f)"
        " | gateway %d,%d"
        " | city %d,%d (%.0f)\n",
        fortress.x, fortress.z, sqrt(fortress_d2),
        gateway.x, gateway.z,
        city.x, city.z, sqrt(city_d2));
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 4 || argc > 5) {
        fprintf(stderr,
            "Usage: %s <starting-seed> <number-of-seeds> <criteria.json> [true|false]\n",
            argv[0]);
        return 1;
    }

    char *endptr = NULL;
    uint64_t start = strtoull(argv[1], &endptr, 0);
    if (!endptr || *endptr != '\0') {
        fprintf(stderr, "Invalid starting seed: %s\n", argv[1]);
        return 1;
    }

    uint64_t count = strtoull(argv[2], &endptr, 0);
    if (!endptr || *endptr != '\0' || count == 0) {
        fprintf(stderr, "Number of seeds must be a positive integer.\n");
        return 1;
    }

    int smallBiomes = 0;
    if (argc == 5) {
        if (strcmp(argv[4], "true") == 0 ||
            strcmp(argv[4], "1") == 0) {
            smallBiomes = 1;
        } else if (strcmp(argv[4], "false") != 0 &&
                   strcmp(argv[4], "0") != 0) {
            fprintf(stderr,
                "The biome option must be true, false, 1, or 0.\n");
            return 1;
        }
    }

    BiomeCriteria criteria = {0};
    char error[512];
    if (!loadBiomeCriteria(argv[3], &criteria, error, sizeof(error))) {
        fprintf(stderr, "Could not load biome criteria: %s\n", error);
        return 1;
    }

    fprintf(stderr, "Loaded %zu biome criteria from %s\n",
            criteria.count, argv[3]);
    fprintf(stderr, "Small biomes: %s\n",
            smallBiomes ? "enabled" : "disabled");

    for (uint64_t i = 0; i < count; i++) {
        test_seed(start + i, smallBiomes, &criteria);

        if ((i + 1) % 100000 == 0) {
            fprintf(stderr, "Checked %" PRIu64 " seeds\n", i + 1);
            fflush(stderr);
        }
    }

    freeBiomeCriteria(&criteria);
    return 0;
}
