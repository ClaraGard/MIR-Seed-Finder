#ifndef BIOMEAREACHECKER_H
#define BIOMEAREACHECKER_H

#include <stddef.h>
#include "generator.h"

typedef enum {
    BIOME_CHECK_NOT_EVALUATED = 0,
    BIOME_CHECK_PASSED,
    BIOME_CHECK_FAILED,
    BIOME_CHECK_ERROR
} BiomeCheckStatus;

typedef struct {
    const char *name;
    BiomeCheckStatus status;
    double area;
} BiomeCheckResult;

typedef struct {
    size_t checkCount;
    size_t evaluatedCount;
    size_t passedCount;
    size_t failedCount;

    int allPassed;
    int hasError;

    BiomeCheckResult *checks;
} BiomeResult;


typedef struct {
    int *ids;
    int centerX, centerZ;
    int sx, sz;
    int scale;
    int x0, z0;
} BiomeMap;

typedef enum {
    CRITERION_DIM_OVERWORLD,
    CRITERION_DIM_NETHER,
    CRITERION_DIM_END
} BiomeDimension;

typedef struct {
    char *name;
    int *biomeIds;
    size_t biomeIdCount;
    BiomeDimension dimension;
    int useSpawn;
    int x;
    int z;
    int radius;
    double minArea;
    double maxArea;
} BiomeCriterion;

typedef struct {
    BiomeCriterion *items;
    size_t count;
} BiomeCriteria;

BiomeMap generateBiomeMap(const Generator *g, int centerX, int centerZ,
                          int sx, int sz, int scale);
void freeBiomeMap(BiomeMap *map);
double estimateBiomeArea(const BiomeMap *map, int biomeId,
                         int queryCenterX, int queryCenterZ,
                         int queryWidth, int queryHeight);

int loadBiomeCriteria(const char *filename, BiomeCriteria *out,
                      char *error, size_t errorSize);
void freeBiomeCriteria(BiomeCriteria *criteria);
BiomeResult checkBiomeRequirements(const BiomeCriteria *criteria,
                           const Generator *overworld,
                           const Generator *nether,
                           const Generator *end,
                           int spawnX, int spawnZ,
                           int scale, int verbose);

#endif
