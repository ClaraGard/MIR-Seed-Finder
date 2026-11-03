#ifndef BIOMEAREACHECKER_H
#define BIOMEAREACHECKER_H

#include <stddef.h>
#include <stdint.h>
#include "generator.h"
#include "criterialoader.h"

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

BiomeMap generateBiomeMap(const Generator *g, int centerX, int centerZ,
                          int sx, int sz, int scale);
void freeBiomeMap(BiomeMap *map);
double estimateBiomeArea(const BiomeMap *map, int biomeId,
                         int queryCenterX, int queryCenterZ,
                         int queryWidth, int queryHeight);

/* Combined area of biomeIds: 1=pass, 0=fail, -1=error.
 * areaOut receives the exact total area (also for failed criteria).
 */
int checkBiomeAreaConditions(const BiomeMap *map,
                             const int *biomeIds, size_t biomeIdCount,
                             int queryCenterX, int queryCenterZ,
                             int queryWidth, int queryHeight,
                             double minSize, double maxSize);

void freeBiomeResult(BiomeResult *result);
BiomeResult checkBiomeRequirements(const BiomeCriteria *criteria,
                           const Generator *overworld,
                           const Generator *nether,
                           const Generator *end,
                           uint64_t seed, int scale, int verbose);

#endif
