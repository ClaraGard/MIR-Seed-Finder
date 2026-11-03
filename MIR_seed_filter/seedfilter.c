
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
#define MAX_WARPED_DIST     128
#define MAX_FORTRESS_DIST   128

// Grid spacing for sampling the Nether's warped forest biome.
#define BIOME_STEP          8

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

static int bad_spawn_biome(int id)
{
    return id == desert || isMesa(id) || isSnowy(id);
}

// Finds the closest sampled warped forest point near Nether (0,0).
// Coordinates are approximate, with up to roughly BIOME_STEP error.
static int find_warped_forest(
    Generator *nether, int radius, Pos *best, double *best_d2)
{
    int found = 0;
    double limit2 = (double)radius * radius;
    double nearest2 = limit2;

    int step_q = BIOME_STEP / 4;
    int radius_q = radius / 4;

    for (int qz = -radius_q; qz <= radius_q; qz += step_q) {
        for (int qx = -radius_q; qx <= radius_q; qx += step_q) {
            int id = getBiomeAt(nether, 4, qx, 15, qz);

            if (id != warped_forest)
                continue;

            int x = qx * 4;
            int z = qz * 4;
            double d2 = dist2(x, z, 0, 0);

            if (d2 <= nearest2) {
                nearest2 = d2;
                best->x = x;
                best->z = z;
                found = 1;
            }
        }
    }

    if (found && best_d2)
        *best_d2 = nearest2;

    return found;
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

static void test_seed(uint64_t seed, int smallBiomes)
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
    Pos spawn = getSpawn(&ow);
    int spawn_biome = getBiomeAt(&ow, 4,
        spawn.x >> 2, 16, spawn.z >> 2);

    if (spawn_biome < 0 || bad_spawn_biome(spawn_biome))
        return;

    // 2. Find a warped forest near Nether origin.
    Pos warped;
    double warped_d2;

    if (!find_warped_forest(&nether, MAX_WARPED_DIST,
                            &warped, &warped_d2))
        return;

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
        "SEED %" PRIu64
        " | spawn biome %d at %d,%d"
        " | warped %d,%d (%.0f)"
        " | fortress %d,%d (%.0f)"
        " | gateway %d,%d"
        " | city %d,%d (%.0f)\n",
        seed, spawn_biome, spawn.x, spawn.z,
        warped.x, warped.z, sqrt(warped_d2),
        fortress.x, fortress.z, sqrt(fortress_d2),
        gateway.x, gateway.z,
        city.x, city.z, sqrt(city_d2));
}

int main(int argc, char **argv)
{
    if (argc < 3 || argc > 4) {
        fprintf(stderr,
            "Usage: %s <starting-seed> <number-of-seeds> [true|false]\n",
            argv[0]);
        return 1;
    }

    uint64_t start = strtoull(argv[1], NULL, 0);
    uint64_t count = strtoull(argv[2], NULL, 0);

    int smallBiomes = 0;

    if (argc == 4) {
        if (strcmp(argv[3], "true") == 0 ||
            strcmp(argv[3], "1") == 0) {
            smallBiomes = 1;
        } else if (strcmp(argv[3], "false") != 0 &&
                   strcmp(argv[3], "0") != 0) {
            fprintf(stderr,
                "The biome option must be true, false, 1, or 0.\n");
            return 1;
        }
    }

    if (count == 0) {
        fprintf(stderr, "Number of seeds must be positive.\n");
        return 1;
    }

    fprintf(stderr, "Small biomes: %s\n",
            smallBiomes ? "enabled" : "disabled");

    for (uint64_t i = 0; i < count; i++) {
        test_seed(start + i, smallBiomes);

        if ((i + 1) % 100000 == 0) {
            fprintf(stderr, "Checked %" PRIu64 " seeds\n", i + 1);
            fflush(stderr);
        }
    }

    return 0;
}
