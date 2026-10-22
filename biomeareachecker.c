
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
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

    const double fullCellArea = (double)scale * scale;

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


typedef struct {
    const Generator *generator;
    int centerX;
    int centerZ;
    int radius;
    int scale;
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
        case CRITERION_DIM_NETHER:    return nether;
        case CRITERION_DIM_END:       return end;
        default:            return NULL;
    }
}

static BiomeMap *getCachedBiomeMap(
    BiomeMapCacheEntry *cache,
    size_t *cacheCount,
    size_t cacheCapacity,
    const Generator *generator,
    int centerX,
    int centerZ,
    int radius,
    int scale)
{
    for (size_t i = 0; i < *cacheCount; i++) {
        BiomeMapCacheEntry *entry = &cache[i];

        if (entry->generator == generator &&
            entry->centerX == centerX &&
            entry->centerZ == centerZ &&
            entry->radius == radius &&
            entry->scale == scale) {
            return &entry->map;
        }
    }

    if (*cacheCount >= cacheCapacity)
        return NULL;

    BiomeMapCacheEntry *entry = &cache[*cacheCount];

    entry->generator = generator;
    entry->centerX = centerX;
    entry->centerZ = centerZ;
    entry->radius = radius;
    entry->scale = scale;

    int width = radius * 2;

    entry->map = generateBiomeMap(
        generator,
        centerX,
        centerZ,
        width,
        width,
        scale
    );

    if (!entry->map.ids)
        return NULL;

    (*cacheCount)++;
    return &entry->map;
}

/*
 * Checks all biome requirements.
 *
 * End spawn:true markers resolve to the seed-dependent first gateway.
 * Configure each Generator for the appropriate dimension before calling.
 *
 * scale should be one of the scales supported by generateBiomeMap(),
 * such as 1, 4, 16, 64, or 256.
 */
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
    out.allPassed = 0;

    if (!criteria || scale <= 0)
        goto error;

    if (scale != 1 && scale != 4 && scale != 16 &&
        scale != 64 && scale != 256)
        goto error;

    out.checkCount = criteria->count;
    out.allPassed = 1;

    out.checks = calloc(
        criteria->count, sizeof(BiomeCheckResult));

    if (criteria->count > 0 && !out.checks)
        goto error;

    for (size_t i = 0; i < criteria->count; i++) {
        out.checks[i].name = criteria->items[i].name;
        out.checks[i].status = BIOME_CHECK_NOT_EVALUATED;
        out.checks[i].area = 0.0;
    }

    BiomeMapCacheEntry *cache = calloc(
        criteria->count, sizeof(BiomeMapCacheEntry));

    if (criteria->count > 0 && !cache)
        goto error;

    size_t cacheCount = 0;

    /* The End gateway position changes with the seed; only compute it when
       an End biome criterion actually uses the loader's reserved marker. */
    Pos endGateway = {0, 0};
    for (size_t i = 0; i < criteria->count; ++i) {
        const BiomeCriterion *c = &criteria->items[i];
        if (criterionUsesFirstEndGateway(c->dimension, c->x, c->z)) {
            if (!end) {
                out.hasError = 1;
                out.allPassed = 0;
                goto cleanup;
            }
            endGateway = firstLinkedEndGateway(end->mc, seed, NULL);
            break;
        }
    }

    /*
     * Dimension indices:
     *   0 = Overworld
     *   1 = Nether
     *   2 = End
     */
    int hasDimension[3] = {0};
    int minX[3], minZ[3], maxX[3], maxZ[3];

    /*
     * Pass 1: calculate the combined bounds required by all
     * criteria in each dimension.
     */
    for (size_t i = 0; i < criteria->count; i++) {
        const BiomeCriterion *criterion = &criteria->items[i];

        int dim;

        switch (criterion->dimension) {
            case CRITERION_DIM_OVERWORLD:
                dim = 0;
                break;

            case CRITERION_DIM_NETHER:
                dim = 1;
                break;

            case CRITERION_DIM_END:
                dim = 2;
                break;

            default:
                out.hasError = 1;
                out.allPassed = 0;
                goto cleanup;
        }

        const Generator *generator = getDimensionGenerator(
            criterion->dimension, overworld, nether, end);

        if (!generator) {
            out.hasError = 1;
            out.allPassed = 0;
            goto cleanup;
        }

        int useGateway = criterionUsesFirstEndGateway(
            criterion->dimension, criterion->x, criterion->z);
        int centerX = useGateway ? endGateway.x : criterion->x;
        int centerZ = useGateway ? endGateway.z : criterion->z;
        int radius = criterion->radius;

        int left   = centerX - radius;
        int right  = centerX + radius;
        int top    = centerZ - radius;
        int bottom = centerZ + radius;

        if (!hasDimension[dim]) {
            minX[dim] = left;
            maxX[dim] = right;
            minZ[dim] = top;
            maxZ[dim] = bottom;
            hasDimension[dim] = 1;
        } else {
            if (left   < minX[dim]) minX[dim] = left;
            if (right  > maxX[dim]) maxX[dim] = right;
            if (top    < minZ[dim]) minZ[dim] = top;
            if (bottom > maxZ[dim]) maxZ[dim] = bottom;
        }
    }

    /*
     * Pass 2: calculate the map parameters for each used dimension.
     */
    int mapCenterX[3] = {0};
    int mapCenterZ[3] = {0};
    int mapSide[3] = {0};

    for (int dim = 0; dim < 3; dim++) {
        if (!hasDimension[dim])
            continue;

        int spanX = maxX[dim] - minX[dim];
        int spanZ = maxZ[dim] - minZ[dim];

        mapCenterX[dim] = minX[dim] + spanX / 2;
        mapCenterZ[dim] = minZ[dim] + spanZ / 2;

        int halfX = (spanX + 1) / 2;
        int halfZ = (spanZ + 1) / 2;

        mapSide[dim] = (halfX > halfZ ? halfX : halfZ) + scale;
    }

    /*
     * Pass 3: order dimensions by ascending map size.
     *
     * The dimension requiring the smallest map is processed first.
     * Ties preserve the original dimension order.
     */
    int dimensionOrder[3];
    int dimensionCount = 0;

    for (int dim = 0; dim < 3; dim++) {
        if (hasDimension[dim])
            dimensionOrder[dimensionCount++] = dim;
    }

    for (int i = 0; i < dimensionCount - 1; i++) {
        for (int j = i + 1; j < dimensionCount; j++) {
            if (mapSide[dimensionOrder[j]] <
                mapSide[dimensionOrder[i]]) {
                int temp = dimensionOrder[i];
                dimensionOrder[i] = dimensionOrder[j];
                dimensionOrder[j] = temp;
            }
        }
    }

    /*
     * Pass 4: process one dimension completely before moving
     * to the next.
     *
     * For each dimension:
     *   1. Generate its biome map.
     *   2. Evaluate every criterion belonging to that dimension.
     *   3. Stop immediately if any criterion fails.
     *
     * Larger maps are therefore generated only when all criteria
     * in the smaller-map dimensions have passed.
     */
    for (int order = 0; order < dimensionCount; order++) {
        int dim = dimensionOrder[order];

        BiomeDimension dimension;

        switch (dim) {
            case 0:
                dimension = CRITERION_DIM_OVERWORLD;
                break;

            case 1:
                dimension = CRITERION_DIM_NETHER;
                break;

            default:
                dimension = CRITERION_DIM_END;
                break;
        }

        const Generator *generator = getDimensionGenerator(
            dimension, overworld, nether, end);

        /*
         * Generate or retrieve the map for this dimension.
         * No maps for subsequent dimensions are generated yet.
         */
        BiomeMap *map = getCachedBiomeMap(
            cache, &cacheCount, criteria->count,
            generator,
            mapCenterX[dim],
            mapCenterZ[dim],
            mapSide[dim],
            scale);

        if (!map) {
            out.hasError = 1;
            out.allPassed = 0;
            goto cleanup;
        }

        /*
         * Evaluate every criterion belonging to this dimension.
         */
        for (size_t i = 0; i < criteria->count; i++) {
            const BiomeCriterion *criterion = &criteria->items[i];

            if ((int)criterion->dimension != dim)
                continue;

            int useGateway = criterionUsesFirstEndGateway(
                criterion->dimension, criterion->x, criterion->z);
            int centerX = useGateway ? endGateway.x : criterion->x;
            int centerZ = useGateway ? endGateway.z : criterion->z;
            int width = criterion->radius * 2;

            /* OR semantics: sum all matching biomes in one map pass,
               counting each sampled cell only once. */
            int passed = checkBiomeAreaConditions(
                map,
                criterion->biomeIds,
                criterion->biomeIdCount,
                centerX,
                centerZ,
                width,
                width,
                criterion->minArea,
                criterion->maxArea
            );

            if (passed < 0) {
                if (verbose) {
                    fprintf(stderr,
                        "[%s] ERROR: checkBiomeAreaConditions failed "
                        "(center=%d,%d, radius=%d)\n",
                        criterion->name,
                        centerX, centerZ,
                        criterion->radius);
                }

                out.checks[i].status = BIOME_CHECK_ERROR;
                out.checks[i].area = -1.0;
                out.hasError = 1;
                out.allPassed = 0;
                goto cleanup;
            }

            /* Record this check's result. */
            BiomeCheckResult *check = &out.checks[i];

            check->status = passed
                ? BIOME_CHECK_PASSED
                : BIOME_CHECK_FAILED;

            out.evaluatedCount++;

            if (passed) {
                out.passedCount++;
            } else {
                out.failedCount++;
                out.allPassed = 0;
                goto cleanup;
            }

            if (verbose) {
                printf("[%s] : %s\n",
                    criterion->name,
                    passed ? "PASS" : "FAIL");
            }

        }
    }

cleanup:
    for (size_t i = 0; i < cacheCount; i++)
        freeBiomeMap(&cache[i].map);

    free(cache);
    return out;

error:
    out.hasError = 1;
    out.allPassed = 0;
    return out;
}