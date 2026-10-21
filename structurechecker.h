#ifndef STRUCTURECHECKER_H
#define STRUCTURECHECKER_H

#include <stddef.h>
#include <stdint.h>
#include "criterialoader.h"
#include "finders.h"
#include "noise.h"

/* Primitive checks return 1=passes, 0=fails, -1=error.
 * The structure coordinate is its Cubiomes generation-attempt position.
 * minDistance applies to the NEAREST viable structure; negative disables it.
 * countRadius >= 0 defines a square centered at (centerX,centerZ).
 * countRadius == -1 searches progressively to the Minecraft world border,
 * stopping as soon as minCount is satisfied. Requires maxCount == -1.
 * The endSurface argument is required when checking End_City.
 */
int checkStructureDistance(int structureId, int mc, uint64_t seed,
                           Generator *generator, const SurfaceNoise *endSurface,
                           int centerX, int centerZ, int minDistance, int maxDistance,
                           Pos *closest, double *closestDistance);

int checkStructureCount(int structureId, int mc, uint64_t seed,
                        Generator *generator, const SurfaceNoise *endSurface,
                        int centerX, int centerZ, int countRadius,
                        int minCount, int maxCount,
                        size_t *countFound, int *countExact);

typedef enum {
    STRUCTURE_CHECK_NOT_EVALUATED = 0,
    STRUCTURE_CHECK_PASSED,
    STRUCTURE_CHECK_FAILED,
    STRUCTURE_CHECK_ERROR
} StructureCheckStatus;

typedef struct {
    const char *name; /* borrowed from parsed criteria */
    StructureCheckStatus status;
    double distance; /* nearest distance; -1 when none was found/needed */
    Pos position;
    size_t count;
    int countExact;  /* false when count was satisfied via early termination */
} StructureCheckEntry;

typedef struct {
    size_t checkCount, evaluatedCount, passedCount, failedCount;
    int allPassed, hasError;
    StructureCheckEntry *checks;
} StructureResult;

void freeStructureResult(StructureResult *result);

/* End 'spawn:true' resolves to the destination of the FIRST fixed inner
 * End gateway (the route used by seedfilter3.c). Nether 'spawn:true'
 * resolves to Nether 0,0; Overworld to the provided spawnX,spawnZ.
 * The three generators MUST already have the given seed applied.
 */
StructureResult checkStructureDistanceRequirements(
    const StructureDistanceCriteria *criteria, int mc, uint64_t seed,
    Generator *overworld, Generator *nether, Generator *end,
    int verbose);

StructureResult checkStructureAmountRequirements(
    const StructureAmountCriteria *criteria, int mc, uint64_t seed,
    Generator *overworld, Generator *nether, Generator *end,
    int verbose);

Pos firstLinkedEndGateway(int mc, uint64_t seed,
                          const SurfaceNoise *endSurface);
#endif
