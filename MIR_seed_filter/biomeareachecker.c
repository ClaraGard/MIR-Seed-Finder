
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <limits.h>
#include "finders.h"
#include "generator.h"
#include "biomeareachecker.h"
#include "structurechecker.h"

void freeBiomeResult(BiomeResult *result)
{
    if (!result) return;
    free(result->checks);
    *result = (BiomeResult){0};
}

static int floor_div(int a, int b)
{
    int q = a / b;
    int r = a % b;
    return q - (r != 0 && ((r < 0) != (b < 0)));
}

BiomeMap generateBiomeMap(
    const Generator *g,
    int centerX,
    int centerZ,
    int sx,
    int sz,
    int scale
) {
    BiomeMap map = {0};

    if (!g || sx <= 0 || sz <= 0 ||
        (scale != 1 && scale != 4 && scale != 16 &&
         scale != 64 && scale != 256))
        return map;

    map.centerX = centerX;
    map.centerZ = centerZ;
    map.scale = scale;

    // Number of cells needed to cover the requested block dimensions.
    map.sx = (sx + scale - 1) / scale;
    map.sz = (sz + scale - 1) / scale;

    // Origin in scaled coordinates, centered approximately on the point.
    map.x0 = floor_div(centerX, scale) - map.sx / 2;
    map.z0 = floor_div(centerZ, scale) - map.sz / 2;

    int y = (scale == 1) ? 63 : 15;
    Range r = {scale, map.x0, map.z0,
               map.sx, map.sz, y, 1};

    map.ids = allocCache(g, r);
    if (!map.ids)
        return (BiomeMap){0};

    if (genBiomes(g, map.ids, r) != 0) {
        free(map.ids);
        return (BiomeMap){0};
    }

    return map;
}

void freeBiomeMap(BiomeMap *map)
{
    if (map) {
        free(map->ids);
        *map = (BiomeMap){0};
    }
}

// Estimate biome area in a rectangular query region.
// queryWidth, queryHeight are measured in blocks.
// Returns -1 if the query is outside the map or invalid.
double estimateBiomeArea(
    const BiomeMap *map,
    int biomeId,
    int queryCenterX,
    int queryCenterZ,
    int queryWidth,
    int queryHeight
) {
    if (!map || !map->ids ||
        map->sx <= 0 || map->sz <= 0 ||
        map->scale <= 0 ||
        queryWidth <= 0 || queryHeight <= 0)
        return -1.0;

    // Query rectangle in block coordinates: [left, right) x [top, bottom).
    long long left = (long long)queryCenterX - queryWidth / 2;
    long long top = (long long)queryCenterZ - queryHeight / 2;
    long long right = left + queryWidth;
    long long bottom = top + queryHeight;

    // Generated map bounds in block coordinates.
    long long mapLeft = (long long)map->x0 * map->scale;
    long long mapTop = (long long)map->z0 * map->scale;
    long long mapRight =
        ((long long)map->x0 + map->sx) * map->scale;
    long long mapBottom =
        ((long long)map->z0 + map->sz) * map->scale;

    // Reject queries that are not fully covered by the map.
    if (left < mapLeft || right > mapRight ||
        top < mapTop || bottom > mapBottom)
        return -1.0;

    double area = 0.0;
    int scale = map->scale;

    for (int iz = 0; iz < map->sz; iz++) {
        long long cellTop =
            ((long long)map->z0 + iz) * scale;
        long long cellBottom = cellTop + scale;

        long long overlapZ =
            (cellBottom < bottom ? cellBottom : bottom) -
            (cellTop > top ? cellTop : top);

        if (overlapZ <= 0)
            continue;

        for (int ix = 0; ix < map->sx; ix++) {
            if (map->ids[(size_t)iz * map->sx + ix] != biomeId)
                continue;

            long long cellLeft =
                ((long long)map->x0 + ix) * scale;
            long long cellRight = cellLeft + scale;

            long long overlapX =
                (cellRight < right ? cellRight : right) -
                (cellLeft > left ? cellLeft : left);

            if (overlapX <= 0)
                continue;

            area += (double)overlapX * overlapZ;
        }
    }

    return area;
}

/*
 * Check the combined area of any biome ID in biomeIds.
 * Each map cell is counted at most once, including when IDs are duplicated.
 * Returns: 1 = pass, 0 = fail, -1 = invalid arguments / uncovered query.
 * The exact combined area is written to areaOut if provided, even on failure.
 */
int checkBiomeAreaConditions(
    const BiomeMap *map,
    const int *biomeIds,
    size_t biomeIdCount,
    int queryCenterX,
    int queryCenterZ,
    int queryWidth,
    int queryHeight,
    double minSize,
    double maxSize)
{
    if (!map || !map->ids || !biomeIds || biomeIdCount == 0 ||
        map->sx <= 0 || map->sz <= 0 || map->scale <= 0 ||
        queryWidth <= 0 || queryHeight <= 0 ||
        !isfinite(minSize) || !isfinite(maxSize) ||
        (minSize < 0.0 && minSize != -1.0) ||
        (maxSize < 0.0 && maxSize != -1.0) ||
        (minSize != -1.0 && maxSize != -1.0 &&
         minSize > maxSize))
        return -1;

    const int hasMin = (minSize != -1.0);
    const int hasMax = (maxSize != -1.0);

    /* No constraints. */
    if (!hasMin && !hasMax)
        return 1;

    long long left = (long long)queryCenterX - queryWidth / 2;
    long long top = (long long)queryCenterZ - queryHeight / 2;
    long long right = left + queryWidth;
    long long bottom = top + queryHeight;

    long long mapLeft = (long long)map->x0 * map->scale;
    long long mapTop = (long long)map->z0 * map->scale;
    long long mapRight =
        ((long long)map->x0 + map->sx) * map->scale;
    long long mapBottom =
        ((long long)map->z0 + map->sz) * map->scale;

    if (left < mapLeft || right > mapRight ||
        top < mapTop || bottom > mapBottom)
        return -1;

    unsigned char included[256] = {0};
    int hasLargeIds = 0;

    for (size_t i = 0; i < biomeIdCount; ++i) {
        int id = biomeIds[i];

        if (id < 0)
            return -1;

        if (id < 256)
            included[id] = 1;
        else
            hasLargeIds = 1;
    }

    const int scale = map->scale;

    int x0 = (int)((left - mapLeft) / scale);
    int z0 = (int)((top - mapTop) / scale);
    int x1 = (int)((right - mapLeft + scale - 1) / scale);
    int z1 = (int)((bottom - mapTop + scale - 1) / scale);

    if (x1 > map->sx) x1 = map->sx;
    if (z1 > map->sz) z1 = map->sz;

    double area = 0.0;

    /*
     * Maximum possible matching area in the query.
     * Decreases as cells are processed.
     */
    double remaining =
        (double)queryWidth * (double)queryHeight;

    /* Impossible to reach the minimum. */
    if (hasMin && remaining < minSize)
        return 0;

    /*
     * If the minimum is already met and even the
     * entire query fits under the maximum, succeed.
     */
    if ((!hasMin || minSize <= 0.0) &&
        (!hasMax || remaining <= maxSize))
        return 1;


    for (int iz = z0; iz < z1; ++iz) {
        long long cellTop =
            ((long long)map->z0 + iz) * scale;
        long long cellBottom = cellTop + scale;

        long long overlapZ =
            (cellBottom < bottom ? cellBottom : bottom) -
            (cellTop > top ? cellTop : top);

        for (int ix = x0; ix < x1; ++ix) {
            long long overlapArea;

            /*
             * Interior cells have full area.
             * Only boundary cells need clipping.
             */
            if (overlapZ == scale &&
                ix > x0 && ix < x1 - 1) {
                overlapArea = (long long)scale * scale;
            } else {
                long long cellLeft =
                    ((long long)map->x0 + ix) * scale;
                long long cellRight = cellLeft + scale;

                long long overlapX =
                    (cellRight < right ? cellRight : right) -
                    (cellLeft > left ? cellLeft : left);

                overlapArea = overlapX * overlapZ;
            }

            int id = map->ids[(size_t)iz * map->sx + ix];

            int matches =
                (id >= 0 && id < 256 && included[id]);

            if (!matches && hasLargeIds && id >= 256) {
                for (size_t k = 0; k < biomeIdCount; ++k) {
                    if (biomeIds[k] == id) {
                        matches = 1;
                        break;
                    }
                }
            }

            if (matches) {
                area += (double)overlapArea;

                /* Maximum exceeded: impossible. */
                if (hasMax && area > maxSize)
                    return 0;
            }

            remaining -= (double)overlapArea;

            /*
             * Even if every remaining cell matches,
             * the minimum cannot be reached.
             */
            if (hasMin && area + remaining < minSize)
                return 0;

            /*
             * Minimum reached, and the maximum
             * cannot be exceeded by remaining cells.
             */
            if ((!hasMin || area >= minSize) &&
                (!hasMax || area + remaining <= maxSize))
                return 1;
        }
    }

    return (!hasMin || area >= minSize) &&
           (!hasMax || area <= maxSize);
}


/* Cache each generated biome map. Larger maps can satisfy later, smaller
 * queries (including quadrant origins) without regenerating the same terrain. */
typedef struct {
    const Generator *generator;
    BiomeMap map;
} BiomeMapCacheEntry;

static const Generator *getDimensionGenerator(
    BiomeDimension dimension,
    const Generator *overworld,
    const Generator *nether,
    const Generator *end)
{
    switch (dimension) {
        case CRITERION_DIM_OVERWORLD: return overworld;
        case CRITERION_DIM_NETHER: return nether;
        case CRITERION_DIM_END: return end;
        default: return NULL;
    }
}

static int mapContains(const BiomeMap *map, int centerX, int centerZ, int radius)
{
    const long long minX = (long long)map->x0 * map->scale;
    const long long minZ = (long long)map->z0 * map->scale;
    const long long maxX = ((long long)map->x0 + map->sx) * map->scale;
    const long long maxZ = ((long long)map->z0 + map->sz) * map->scale;
    return map->ids && (long long)centerX - radius >= minX &&
           (long long)centerZ - radius >= minZ &&
           (long long)centerX + radius <= maxX &&
           (long long)centerZ + radius <= maxZ;
}

static BiomeMap *getCachedBiomeMap(
    BiomeMapCacheEntry *cache, size_t *cacheCount, size_t cacheCapacity,
    const Generator *generator, int centerX, int centerZ, int radius, int scale)
{
    for (size_t i = 0; i < *cacheCount; ++i) {
        if (cache[i].generator == generator &&
            mapContains(&cache[i].map, centerX, centerZ, radius))
            return &cache[i].map;
    }
    if (*cacheCount >= cacheCapacity || radius > (INT_MAX / 2) - scale)
        return NULL;
    BiomeMap map = generateBiomeMap(generator, centerX, centerZ,
                                    2 * (radius + scale),
                                    2 * (radius + scale), scale);
    if (!map.ids || !mapContains(&map, centerX, centerZ, radius)) {
        freeBiomeMap(&map);
        return NULL;
    }
    cache[*cacheCount].generator = generator;
    cache[*cacheCount].map = map;
    return &cache[(*cacheCount)++].map;
}

/* How many group members have passed, and how many remain unevaluated?
 * A criterion with a group may fail individually without rejecting the seed. */
static void groupProgress(const BiomeCriteria *criteria,
                          const BiomeResult *result, const char *group,
                          int *passed, int *pending)
{
    *passed = *pending = 0;
    for (size_t i = 0; i < criteria->count; ++i) {
        if (!criteria->items[i].group ||
            strcmp(criteria->items[i].group, group)) continue;
        if (result->checks[i].status == BIOME_CHECK_PASSED) ++*passed;
        if (result->checks[i].status == BIOME_CHECK_NOT_EVALUATED) ++*pending;
    }
}

/* Lazy map generation with priority order determined during JSON loading.
 * A small map need not be inflated by a later 5000-block biome requirement.
 * Group members each contribute at most one passing criterion, regardless
 * of how many biome IDs the criterion contains. */
BiomeResult checkBiomeRequirements(
    const BiomeCriteria *criteria,
    const Generator *overworld,
    const Generator *nether,
    const Generator *end,
    uint64_t seed,
    int scale,
    int verbose)
{
    BiomeResult out = {0};
    if (!criteria || (scale != 1 && scale != 4 && scale != 16 &&
                      scale != 64 && scale != 256)) {
        out.hasError = 1;
        return out;
    }
    out.checkCount = criteria->count;
    out.allPassed = 1;
    out.checks = calloc(criteria->count ? criteria->count : 1,
                        sizeof(*out.checks));
    BiomeMapCacheEntry *cache = calloc(criteria->count ? criteria->count : 1,
                                       sizeof(*cache));
    if (!out.checks || !cache) {
        out.hasError = 1;
        out.allPassed = 0;
        free(cache);
        return out;
    }
    for (size_t i = 0; i < criteria->count; ++i) {
        out.checks[i].name = criteria->items[i].name;
        out.checks[i].area = -1.0; /* early-exit area checker doesn't tally it */
    }

    size_t cacheCount = 0;
    int endGatewayResolved = 0;
    Pos endGateway = {0, 0};
    for (size_t i = 0; i < criteria->count; ++i) {
        const BiomeCriterion *c = &criteria->items[i];
        BiomeCheckResult *check = &out.checks[i];
        if (c->group) {
            int passed, pending;
            groupProgress(criteria, &out, c->group, &passed, &pending);
            if (passed >= c->groupMinCount)
                continue; /* group already satisfied */
        }
        const Generator *generator = getDimensionGenerator(
            c->dimension, overworld, nether, end);
        if (!generator) {
            out.hasError = 1;
            out.allPassed = 0;
            check->status = BIOME_CHECK_ERROR;
            goto cleanup;
        }
        int centerX = c->x, centerZ = c->z;
        if (criterionUsesFirstEndGateway(c->dimension, c->x, c->z)) {
            if (!endGatewayResolved) {
                endGateway = firstLinkedEndGateway(end->mc, seed, NULL);
                endGatewayResolved = 1;
            }
            centerX = endGateway.x;
            centerZ = endGateway.z;
        }
        BiomeMap *map = getCachedBiomeMap(cache, &cacheCount,
                                           criteria->count, generator,
                                           centerX, centerZ, c->radius, scale);
        if (!map) {
            if (verbose)
                fprintf(stderr, "[%s] biome map generation failed\n", c->name);
            out.hasError = 1;
            out.allPassed = 0;
            check->status = BIOME_CHECK_ERROR;
            goto cleanup;
        }
        int passed = checkBiomeAreaConditions(
            map, c->biomeIds, c->biomeIdCount,
            centerX, centerZ, c->radius * 2, c->radius * 2,
            c->minArea, c->maxArea);
        if (passed < 0) {
            if (verbose)
                fprintf(stderr, "[%s] biome area check failed\n", c->name);
            check->status = BIOME_CHECK_ERROR;
            out.hasError = 1;
            out.allPassed = 0;
            goto cleanup;
        }
        check->status = passed ? BIOME_CHECK_PASSED : BIOME_CHECK_FAILED;
        ++out.evaluatedCount;
        if (passed) ++out.passedCount;
        else ++out.failedCount;

        if (verbose)
            printf("[%s] : %s\n", c->name, passed ? "PASS" : "FAIL");
        if (!c->group) {
            if (!passed) {
                out.allPassed = 0;
                goto cleanup;
            }
        } else {
            int successful, pending;
            groupProgress(criteria, &out, c->group, &successful, &pending);
            if (successful + pending < c->groupMinCount) {
                out.allPassed = 0;
                goto cleanup;
            }
        }
    }
cleanup:
    for (size_t i = 0; i < cacheCount; ++i)
        freeBiomeMap(&cache[i].map);
    free(cache);
    return out;
}
