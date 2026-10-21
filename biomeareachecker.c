
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <math.h>
#include "finders.h"
#include "generator.h"
#include "biomeareachecker.h"
#include "cJSON.h"

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

int checkBiomeAreaConditions(
    const BiomeMap *map,
    int biomeId,
    int queryCenterX,
    int queryCenterZ,
    int queryWidth,
    int queryHeight,
    int minSize,
    int maxSize
) {
    if (!map || !map->ids ||
        map->sx <= 0 || map->sz <= 0 ||
        map->scale <= 0 ||
        queryWidth <= 0 || queryHeight <= 0)
        return -1;

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

    if (minSize == -1 && maxSize == -1)
        return 1;

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

            if (maxSize != -1 && area > maxSize)
                return 0;

            if (minSize != -1 && area >= minSize && maxSize == -1)
                return 1;
        }
    }

    if (minSize != -1 && area < minSize)
        return 0;

    if (maxSize != -1 && area > maxSize)
        return 0;

    return 1;
}


static void setError(char *error, size_t errorSize,
                     const char *format, ...)
{
    if (!error || errorSize == 0)
        return;

    va_list args;
    va_start(args, format);
    vsnprintf(error, errorSize, format, args);
    va_end(args);
}

static char *duplicateString(const char *s)
{
    size_t n = strlen(s) + 1;
    char *copy = malloc(n);

    if (copy)
        memcpy(copy, s, n);

    return copy;
}

void freeBiomeCriteria(BiomeCriteria *criteria)
{
    if (!criteria)
        return;

    for (size_t i = 0; i < criteria->count; i++) {
        free(criteria->items[i].name);
        free(criteria->items[i].biomeIds);
    }

    free(criteria->items);
    criteria->items = NULL;
    criteria->count = 0;
}

static cJSON *requiredField(const cJSON *object, const char *name)
{
    return cJSON_GetObjectItemCaseSensitive(object, name);
}

static int readInteger(const cJSON *object, const char *name,
                       int *result)
{
    cJSON *value = requiredField(object, name);

    if (!cJSON_IsNumber(value) ||
        !isfinite(value->valuedouble) ||
        value->valuedouble < INT_MIN ||
        value->valuedouble > INT_MAX ||
        floor(value->valuedouble) != value->valuedouble)
        return 0;

    *result = value->valueint;
    return 1;
}

static int readDouble(const cJSON *object, const char *name,
                      double *result)
{
    cJSON *value = requiredField(object, name);

    if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble))
        return 0;

    *result = value->valuedouble;
    return 1;
}

static int readDimension(const cJSON *origin,
                         BiomeDimension *dimension)
{
    cJSON *value = requiredField(origin, "dimension");

    if (!cJSON_IsString(value))
        return 0;

    if (strcmp(value->valuestring, "overworld") == 0)
        *dimension = CRITERION_DIM_OVERWORLD;
    else if (strcmp(value->valuestring, "nether") == 0)
        *dimension = CRITERION_DIM_NETHER;
    else if (strcmp(value->valuestring, "end") == 0)
        *dimension = CRITERION_DIM_END;
    else
        return 0;

    return 1;
}

static int readBiomeCriterion(const cJSON *json,
                              BiomeCriterion *out,
                              char *error, size_t errorSize)
{
    memset(out, 0, sizeof(*out));

    cJSON *name = requiredField(json, "name");
    cJSON *ids = requiredField(json, "biome_ids");
    cJSON *origin = requiredField(json, "origin");

    if (!cJSON_IsString(name) ||
        !cJSON_IsArray(ids) ||
        !cJSON_IsObject(origin)) {
        setError(error, errorSize,
                 "Criterion requires name, biome_ids, and origin.");
        return 0;
    }

    out->name = duplicateString(name->valuestring);
    if (!out->name) {
        setError(error, errorSize, "Out of memory reading criterion name.");
        return 0;
    }

    if (!readDimension(origin, &out->dimension)) {
        setError(error, errorSize,
                 "Invalid dimension in criterion '%s'.", out->name);
        return 0;
    }

    cJSON *spawn = requiredField(origin, "spawn");

    if (spawn) {
        if (!cJSON_IsBool(spawn)) {
            setError(error, errorSize,
                     "origin.spawn must be a boolean in '%s'.", out->name);
            return 0;
        }

        out->useSpawn = cJSON_IsTrue(spawn);
    }

    if (!out->useSpawn &&
        (!readInteger(origin, "x", &out->x) ||
         !readInteger(origin, "z", &out->z))) {
        setError(error, errorSize,
                 "Origin needs x and z, or spawn:true, in '%s'.",
                 out->name);
        return 0;
    }

    if (!readInteger(json, "radius", &out->radius) ||
        out->radius <= 0 ||
        out->radius > INT_MAX / 2) {
        setError(error, errorSize,
                 "Invalid radius in criterion '%s'.", out->name);
        return 0;
    }

    if (!readDouble(json, "min_area", &out->minArea) ||
        !readDouble(json, "max_area", &out->maxArea) ||
        (out->minArea < 0 && out->minArea != -1) ||
        (out->maxArea < 0 && out->maxArea != -1) ||
        (out->minArea >= 0 && out->maxArea >= 0 &&
         out->minArea > out->maxArea)) {
        setError(error, errorSize,
                 "Invalid area bounds in criterion '%s'.", out->name);
        return 0;
    }

    out->biomeIdCount = (size_t)cJSON_GetArraySize(ids);

    if (out->biomeIdCount == 0) {
        setError(error, errorSize,
                 "Empty biome_ids array in criterion '%s'.", out->name);
        return 0;
    }

    out->biomeIds = malloc(out->biomeIdCount * sizeof(int));
    if (!out->biomeIds) {
        setError(error, errorSize,
                 "Out of memory reading biome IDs.");
        return 0;
    }

    size_t i = 0;
    cJSON *id;

    cJSON_ArrayForEach(id, ids) {
        if (!cJSON_IsNumber(id) ||
            !isfinite(id->valuedouble) ||
            floor(id->valuedouble) != id->valuedouble ||
            id->valuedouble < 0 ||
            id->valuedouble > INT_MAX) {
            setError(error, errorSize,
                     "Invalid biome ID in criterion '%s'.", out->name);
            return 0;
        }

        out->biomeIds[i++] = id->valueint;
    }

    return 1;
}

/*
 * Returns 1 on success, 0 on failure.
 *
 * Loads only the "biomes" array. The "structures" array is ignored here.
 * On failure, error receives a diagnostic if its buffer is provided.
 */
int loadBiomeCriteria(const char *filename,
                      BiomeCriteria *out,
                      char *error, size_t errorSize)
{
    if (!out || !filename) {
        setError(error, errorSize, "Invalid loader arguments.");
        return 0;
    }

    out->items = NULL;
    out->count = 0;

    if (error && errorSize)
        error[0] = '\0';

    FILE *file = fopen(filename, "rb");
    if (!file) {
        setError(error, errorSize, "Cannot open '%s'.", filename);
        return 0;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        setError(error, errorSize, "Cannot seek in '%s'.", filename);
        return 0;
    }

    long fileSize = ftell(file);
    if (fileSize < 0 || fseek(file, 0, SEEK_SET) != 0 ||
        (unsigned long)fileSize >= (unsigned long)SIZE_MAX) {
        fclose(file);
        setError(error, errorSize, "Invalid file size for '%s'.", filename);
        return 0;
    }

    char *buffer = malloc((size_t)fileSize + 1);
    if (!buffer) {
        fclose(file);
        setError(error, errorSize, "Out of memory reading '%s'.", filename);
        return 0;
    }

    size_t bytesRead = fread(buffer, 1, (size_t)fileSize, file);
    fclose(file);

    if (bytesRead != (size_t)fileSize) {
        free(buffer);
        setError(error, errorSize, "Failed to read '%s'.", filename);
        return 0;
    }

    buffer[bytesRead] = '\0';

    cJSON *root = cJSON_Parse(buffer);
    free(buffer);

    if (!root) {
        setError(error, errorSize, "Invalid JSON in '%s'.", filename);
        return 0;
    }

    cJSON *biomes = requiredField(root, "biomes");

    if (!cJSON_IsArray(biomes)) {
        cJSON_Delete(root);
        setError(error, errorSize,
                 "Configuration must contain a 'biomes' array.");
        return 0;
    }

    out->count = (size_t)cJSON_GetArraySize(biomes);
    out->items = calloc(out->count, sizeof(BiomeCriterion));

    if (out->count > 0 && !out->items) {
        out->count = 0;
        cJSON_Delete(root);
        setError(error, errorSize, "Out of memory allocating criteria.");
        return 0;
    }

    size_t i = 0;
    cJSON *entry;

    cJSON_ArrayForEach(entry, biomes) {
        if (!cJSON_IsObject(entry) ||
            !readBiomeCriterion(entry, &out->items[i],
                                error, errorSize)) {
            cJSON_Delete(root);
            freeBiomeCriteria(out);
            return 0;
        }
        i++;
    }

    cJSON_Delete(root);
    return 1;
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
 * spawnX/spawnZ are the actual coordinates to use for origin.spawn:true.
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
    int spawnX,
    int spawnZ,
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
    int result = 1;

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
                result = -1;
                goto cleanup;
        }

        const Generator *generator = getDimensionGenerator(
            criterion->dimension, overworld, nether, end);

        if (!generator) {
            result = -1;
            goto cleanup;
        }

        int centerX = criterion->useSpawn ? spawnX : criterion->x;
        int centerZ = criterion->useSpawn ? spawnZ : criterion->z;
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
            result = -1;
            goto cleanup;
        }

        /*
         * Evaluate every criterion belonging to this dimension.
         */
        for (size_t i = 0; i < criteria->count; i++) {
            const BiomeCriterion *criterion = &criteria->items[i];

            if ((int)criterion->dimension != dim)
                continue;

            int centerX = criterion->useSpawn
                ? spawnX : criterion->x;
            int centerZ = criterion->useSpawn
                ? spawnZ : criterion->z;
            int width = criterion->radius * 2;

            /*
             * biome_ids uses OR semantics.
             * Each sampled cell contains only one biome, so
             * distinct biome IDs do not double-count a cell.
             */
            double area = 0.0;

            for (size_t j = 0;
                 j < criterion->biomeIdCount;
                 j++) {
                double part = estimateBiomeArea(
                    map,
                    criterion->biomeIds[j],
                    centerX,
                    centerZ,
                    width,
                    width
                );

                if (part < 0.0 || !isfinite(part)) {
                    if (verbose) {
                        fprintf(stderr,
                            "[%s] ERROR: estimateBiomeArea failed "
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

                area += part;
            }

            int passed =
                (criterion->minArea == -1 ||
                area >= criterion->minArea) &&
                (criterion->maxArea == -1 ||
                area <= criterion->maxArea);

            /* Record this check's result. */
            BiomeCheckResult *check = &out.checks[i];

            check->area = area;
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
                printf("[%s] area = %.2f blocks^2 : %s\n",
                    criterion->name,
                    area,
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