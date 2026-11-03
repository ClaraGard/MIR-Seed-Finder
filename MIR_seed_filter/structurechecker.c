#include "structurechecker.h"
#include "noise.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

/* Coordinates checked for an unrestricted count are inside the world border. */
#define WORLD_BORDER 29999984LL

static int64_t floorDiv64(int64_t a, int64_t positiveB)
{
    int64_t q = a / positiveB;
    return q - (a % positiveB < 0);
}

static int inWorld(int x, int z)
{
    return x >= -WORLD_BORDER && x <= WORLD_BORDER &&
           z >= -WORLD_BORDER && z <= WORLD_BORDER;
}

static int candidateAt(int id, int mc, uint64_t seed,
                       int rx, int rz, Pos *p)
{
    return getStructurePos(id, mc, seed, rx, rz, p) && inWorld(p->x, p->z);
}

static int viableAt(int id, Generator *generator,
                    const SurfaceNoise *endSurface, Pos p)
{
    if (!isViableStructurePos(id, generator, p.x, p.z, 0)) return 0;
    if (id == End_City &&
        (!endSurface || !isViableEndCityTerrain(generator, endSurface, p.x, p.z)))
        return 0;
    return 1;
}

/* Region enumeration covers all possible attempt positions within the square. */
static int64_t regionStep(int id, int mc)
{
    StructureConfig cfg;
    if (!getStructureConfig(id, mc, &cfg) || cfg.regionSize <= 0) return -1;
    return (int64_t)cfg.regionSize * 16;
}

int checkStructureDistance(int id, int mc, uint64_t seed,
                           Generator *g, const SurfaceNoise *endSurface,
                           int cx, int cz, int minDistance, int maxDistance,
                           Pos *closest, double *closestDistance)
{
    if (closest) *closest = (Pos){0};
    if (closestDistance) *closestDistance = -1.0;
    int64_t step = regionStep(id, mc);
    if (!g || step <= 0 || minDistance < -1 || maxDistance < -1 ||
        (minDistance >= 0 && maxDistance >= 0 && minDistance > maxDistance) ||
        (id == End_City && !endSurface))
        return -1;

    /* With no upper bound, proving the nearest distance is >= min requires
       searching only the forbidden inner circle; no infinite search. */
    if (minDistance == -1 && maxDistance == -1) return 1;
    if (minDistance == 0 && maxDistance == -1) return 1;
    int radius = maxDistance >= 0 ? maxDistance : minDistance;
    int64_t rx0 = floorDiv64((int64_t)cx - radius, step);
    int64_t rx1 = floorDiv64((int64_t)cx + radius, step);
    int64_t rz0 = floorDiv64((int64_t)cz - radius, step);
    int64_t rz1 = floorDiv64((int64_t)cz + radius, step);
    double best2 = INFINITY;
    Pos best = {0};
    int found = 0;
    double radius2 = (double)radius * radius;

    /* No added margin is needed: getStructurePos() yields an attempt within
       its own region, so regions intersecting the search bounds suffice. */
    for (int64_t rz = rz0; rz <= rz1; rz++) {
        for (int64_t rx = rx0; rx <= rx1; rx++) {
            if (rx < INT_MIN || rx > INT_MAX || rz < INT_MIN || rz > INT_MAX)
                return -1;
            Pos p;
            if (!candidateAt(id, mc, seed, (int)rx, (int)rz, &p))
                continue;
            double dx = (double)p.x - cx;
            double dz = (double)p.z - cz;
            double d2 = dx*dx + dz*dz;
            if (d2 > radius2 || d2 >= best2) continue;
            if (!viableAt(id, g, endSurface, p)) continue;
            best = p;
            best2 = d2;
            found = 1;
            /* A minimum-only check can immediately reject too-close sites. */
            if (maxDistance == -1 && minDistance >= 0 &&
                d2 < (double)minDistance * minDistance) {
                if (closest) *closest = best;
                if (closestDistance) *closestDistance = sqrt(best2);
                return 0;
            }
        }
    }

    if (found) {
        if (closest) *closest = best;
        if (closestDistance) *closestDistance = sqrt(best2);
    }
    /* For min-only: no viable attempt strictly inside the forbidden radius. */
    if (maxDistance == -1) return 1;
    if (!found) return 0;
    return minDistance == -1 || best2 >= (double)minDistance * minDistance;
}

static int countOne(int id, int mc, uint64_t seed, Generator *g,
                    const SurfaceNoise *endSurface, int64_t rx, int64_t rz,
                    int64_t left, int64_t right, int64_t top, int64_t bottom,
                    size_t *count, int minCount, int maxCount,
                    int *exact)
{
    if (rx < INT_MIN || rx > INT_MAX || rz < INT_MIN || rz > INT_MAX)
        return -1;
    Pos p;
    if (!candidateAt(id, mc, seed, (int)rx, (int)rz, &p)) return 0;
    if ((int64_t)p.x < left || p.x > right ||
        (int64_t)p.z < top || p.z > bottom) return 0;
    if (!viableAt(id, g, endSurface, p)) return 0;
    ++*count;
    if (maxCount >= 0 && *count > (size_t)maxCount) {
        *exact = 0;
        return 2; /* fail early; count is a lower bound */
    }
    if (maxCount == -1 && minCount >= 0 && *count >= (size_t)minCount) {
        *exact = 0;
        return 1; /* pass early; count is a lower bound */
    }
    return 0;
}

int checkStructureCount(int id, int mc, uint64_t seed,
                        Generator *g, const SurfaceNoise *endSurface,
                        int cx, int cz, int radius,
                        int minCount, int maxCount,
                        size_t *countFound, int *countExact)
{
    if (countFound) *countFound = 0;
    if (countExact) *countExact = 1;
    int64_t step = regionStep(id, mc);
    if (!g || step <= 0 || radius < -1 || minCount < -1 || maxCount < -1 ||
        (minCount >= 0 && maxCount >= 0 && minCount > maxCount) ||
        (radius == -1 && maxCount != -1) ||
        (id == End_City && !endSurface))
        return -1;
    if ((minCount <= 0) && maxCount == -1) return 1;

    int64_t left = radius == -1 ? -WORLD_BORDER : (int64_t)cx - radius;
    int64_t right = radius == -1 ? WORLD_BORDER : (int64_t)cx + radius;
    int64_t top = radius == -1 ? -WORLD_BORDER : (int64_t)cz - radius;
    int64_t bottom = radius == -1 ? WORLD_BORDER : (int64_t)cz + radius;
    if (left < -WORLD_BORDER) left = -WORLD_BORDER;
    if (right > WORLD_BORDER) right = WORLD_BORDER;
    if (top < -WORLD_BORDER) top = -WORLD_BORDER;
    if (bottom > WORLD_BORDER) bottom = WORLD_BORDER;
    int64_t rx0 = floorDiv64(left, step), rx1 = floorDiv64(right, step);
    int64_t rz0 = floorDiv64(top, step), rz1 = floorDiv64(bottom, step);
    size_t count = 0;
    int exact = 1;
    int signal = 0;

    if (radius != -1) {
        for (int64_t rz = rz0; rz <= rz1; rz++) {
            for (int64_t rx = rx0; rx <= rx1; rx++) {
                signal = countOne(id, mc, seed, g, endSurface, rx, rz,
                                  left, right, top, bottom, &count,
                                  minCount, maxCount, &exact);
                if (signal != 0) goto done;
            }
        }
    } else {
        /* Expanding region rings avoid scanning the entire world's area for
           unbounded minimum-only counts (such as 4 mansions or 25 temples). */
        int64_t ox = floorDiv64(cx, step), oz = floorDiv64(cz, step);
        int64_t maxRing = llabs(ox - rx0);
        if (llabs(rx1 - ox) > maxRing) maxRing = llabs(rx1 - ox);
        if (llabs(oz - rz0) > maxRing) maxRing = llabs(oz - rz0);
        if (llabs(rz1 - oz) > maxRing) maxRing = llabs(rz1 - oz);
        for (int64_t ring = 0; ring <= maxRing; ring++) {
            int64_t xa = ox-ring, xb = ox+ring;
            int64_t za = oz-ring, zb = oz+ring;
            for (int64_t rx = xa; rx <= xb; rx++) {
                if (rx < rx0 || rx > rx1) continue;
                if (za >= rz0 && za <= rz1) {
                    signal = countOne(id, mc, seed, g, endSurface, rx, za,
                                      left, right, top, bottom, &count,
                                      minCount, maxCount, &exact);
                    if (signal) goto done;
                }
                if (zb != za && zb >= rz0 && zb <= rz1) {
                    signal = countOne(id, mc, seed, g, endSurface, rx, zb,
                                      left, right, top, bottom, &count,
                                      minCount, maxCount, &exact);
                    if (signal) goto done;
                }
            }
            for (int64_t rz = za+1; rz < zb; rz++) {
                if (rz < rz0 || rz > rz1) continue;
                if (xa >= rx0 && xa <= rx1) {
                    signal = countOne(id, mc, seed, g, endSurface, xa, rz,
                                      left, right, top, bottom, &count,
                                      minCount, maxCount, &exact);
                    if (signal) goto done;
                }
                if (xb != xa && xb >= rx0 && xb <= rx1) {
                    signal = countOne(id, mc, seed, g, endSurface, xb, rz,
                                      left, right, top, bottom, &count,
                                      minCount, maxCount, &exact);
                    if (signal) goto done;
                }
            }
        }
    }

done:
    if (countFound) *countFound = count;
    if (countExact) *countExact = exact;
    if (signal == -1) return -1;
    if (signal == 1) return 1;
    if (signal == 2) return 0;
    return (minCount == -1 || count >= (size_t)minCount) &&
           (maxCount == -1 || count <= (size_t)maxCount);
}

void freeStructureResult(StructureResult *r)
{
    if (!r) return;
    free(r->checks);
    *r = (StructureResult){0};
}

Pos firstLinkedEndGateway(int mc, uint64_t seed,
                          const SurfaceNoise *endSurface)
{
    SurfaceNoise localSurface;
    if (!endSurface) {
        initSurfaceNoise(&localSurface, DIM_END, seed);
        endSurface = &localSurface;
    }
    Pos sources[20];
    EndNoise en;
    getFixedEndGateways(mc, seed, sources);
    setEndSeed(&en, mc, seed);
    return getLinkedGatewayPos(&en, endSurface, seed, sources[0]);
}

typedef struct {
    Generator *generator[3];
    Pos endGateway;
    SurfaceNoise endSurface;
    int hasEndSurface;
} CheckContext;

static void prepareContext(CheckContext *ctx, int mc, uint64_t seed,
                           Generator *ow, Generator *nether, Generator *end,
                           int needsEndCity, int needsEndGateway)
{
    *ctx = (CheckContext){0};
    ctx->generator[0] = ow;
    ctx->generator[1] = nether;
    ctx->generator[2] = end;
    if (needsEndCity || needsEndGateway) {
        initSurfaceNoise(&ctx->endSurface, DIM_END, seed);
        ctx->hasEndSurface = 1;
    }
    if (needsEndGateway)
        ctx->endGateway = firstLinkedEndGateway(mc, seed, &ctx->endSurface);
}

static StructureResult newResult(size_t count)
{
    StructureResult r = {0};
    r.checkCount = count;
    r.allPassed = 1;
    if (count) {
        r.checks = calloc(count, sizeof(*r.checks));
        if (!r.checks) { r.hasError = 1; r.allPassed = 0; }
    }
    return r;
}

StructureResult checkStructureDistanceRequirements(
    const StructureDistanceCriteria *criteria, int mc, uint64_t seed,
    Generator *ow, Generator *nether, Generator *end,
    int verbose)
{
    if (!criteria || !ow || !nether || !end) {
        StructureResult error = {0};
        error.hasError = 1;
        return error;
    }
    StructureResult r = newResult(criteria->count);
    if (r.hasError) return r;
    int needsCity = 0, needsSpawn = 0;
    for (size_t i = 0; i < criteria->count; ++i) {
        r.checks[i].name = criteria->items[i].name;
        r.checks[i].distance = -1.0;
        if (criteria->items[i].structureId == End_City) needsCity = 1;
        if (criterionUsesFirstEndGateway(criteria->items[i].dimension,
                        criteria->items[i].x, criteria->items[i].z))
            needsSpawn = 1;
    }
    CheckContext ctx;
    prepareContext(&ctx, mc, seed, ow, nether, end,
                   needsCity, needsSpawn);
    for (size_t i = 0; i < criteria->count; ++i) {
        const StructureDistanceCriterion *c = &criteria->items[i];
        StructureCheckEntry *entry = &r.checks[i];
        if (c->dimension < 0 || c->dimension > 2) {
            entry->status = STRUCTURE_CHECK_ERROR;
            r.hasError = 1;
            r.allPassed = 0;
            break;
        }
        Pos origin = criterionUsesFirstEndGateway(c->dimension, c->x, c->z)
            ? ctx.endGateway : (Pos){c->x, c->z};
        int passed = checkStructureDistance(c->structureId, mc, seed,
                        ctx.generator[c->dimension],
                        ctx.hasEndSurface ? &ctx.endSurface : NULL,
                        origin.x, origin.z, c->minDistance, c->maxDistance,
                        &entry->position, &entry->distance);
        entry->status = passed < 0 ? STRUCTURE_CHECK_ERROR :
                       passed ? STRUCTURE_CHECK_PASSED : STRUCTURE_CHECK_FAILED;
        r.evaluatedCount++;
        if (passed < 0) r.hasError = 1;
        if (passed > 0) r.passedCount++;
        else r.failedCount++;
        if (verbose) fprintf(stderr, "[%s] structure distance %.2f : %s\n",
                             c->name, entry->distance,
                             passed < 0 ? "ERROR" : passed ? "PASS" : "FAIL");
        if (passed <= 0) { r.allPassed = 0; break; }
    }
    return r;
}

StructureResult checkStructureAmountRequirements(
    const StructureAmountCriteria *criteria, int mc, uint64_t seed,
    Generator *ow, Generator *nether, Generator *end,
    int verbose)
{
    if (!criteria || !ow || !nether || !end) {
        StructureResult error = {0};
        error.hasError = 1;
        return error;
    }
    StructureResult r = newResult(criteria->count);
    if (r.hasError) return r;
    int needsCity = 0, needsSpawn = 0;
    for (size_t i = 0; i < criteria->count; ++i) {
        r.checks[i].name = criteria->items[i].name;
        if (criteria->items[i].structureId == End_City) needsCity = 1;
        if (criterionUsesFirstEndGateway(criteria->items[i].dimension,
                        criteria->items[i].x, criteria->items[i].z))
            needsSpawn = 1;
    }
    CheckContext ctx;
    prepareContext(&ctx, mc, seed, ow, nether, end,
                   needsCity, needsSpawn);
    for (size_t i = 0; i < criteria->count; ++i) {
        const StructureAmountCriterion *c = &criteria->items[i];
        StructureCheckEntry *entry = &r.checks[i];
        if (c->dimension < 0 || c->dimension > 2) {
            entry->status = STRUCTURE_CHECK_ERROR;
            r.hasError = 1;
            r.allPassed = 0;
            break;
        }
        Pos origin = criterionUsesFirstEndGateway(c->dimension, c->x, c->z)
            ? ctx.endGateway : (Pos){c->x, c->z};
        int passed = checkStructureCount(c->structureId, mc, seed,
                        ctx.generator[c->dimension],
                        ctx.hasEndSurface ? &ctx.endSurface : NULL,
                        origin.x, origin.z, c->countRadius,
                        c->minCount, c->maxCount,
                        &entry->count, &entry->countExact);
        entry->status = passed < 0 ? STRUCTURE_CHECK_ERROR :
                       passed ? STRUCTURE_CHECK_PASSED : STRUCTURE_CHECK_FAILED;
        r.evaluatedCount++;
        if (passed < 0) r.hasError = 1;
        if (passed > 0) r.passedCount++;
        else r.failedCount++;
        if (verbose) fprintf(stderr, "[%s] structure count %s%zu : %s\n",
                             c->name, entry->countExact ? "" : ">=",
                             entry->count,
                             passed < 0 ? "ERROR" : passed ? "PASS" : "FAIL");
        if (passed <= 0) { r.allPassed = 0; break; }
    }
    return r;
}
