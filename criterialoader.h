#ifndef CRITERIALOADER_H
#define CRITERIALOADER_H

#include <stddef.h>
#include <limits.h>

/* Shared, parsed criterion definitions: no cJSON dependency in checkers. */
typedef enum {
    CRITERION_DIM_OVERWORLD = 0,
    CRITERION_DIM_NETHER = 1,
    CRITERION_DIM_END = 2
} BiomeDimension;

/* JSON "spawn":true in the End is the FIRST linked outer gateway.
 * Its position is seed-dependent, so the loader stores this marker in
 * both x and z. The checkers resolve it once the seed is known.
 * All other "spawn":true dimensions use x=z=0 immediately.
 */
#define CRITERION_FIRST_END_GATEWAY INT_MIN

static inline int criterionUsesFirstEndGateway(BiomeDimension dimension,
                                               int x, int z)
{
    return dimension == CRITERION_DIM_END &&
           x == CRITERION_FIRST_END_GATEWAY &&
           z == CRITERION_FIRST_END_GATEWAY;
}


typedef struct {
    char *name;
    int *biomeIds;                /* OR: any one of these biome IDs */
    size_t biomeIdCount;
    BiomeDimension dimension;
    int x, z;
    int radius;                  /* half-width of query square, in blocks */
    double minArea, maxArea;     /* -1 disables a bound */
} BiomeCriterion;

typedef struct {
    BiomeCriterion *items;
    size_t count;
} BiomeCriteria;

typedef struct {
    char *name;
    int structureId;             /* enum StructureType from finders.h */
    BiomeDimension dimension;
    int x, z;
    int minDistance, maxDistance; /* blocks, -1 disables a bound */
} StructureDistanceCriterion;

typedef struct {
    StructureDistanceCriterion *items;
    size_t count;
} StructureDistanceCriteria;

typedef struct {
    char *name;
    int structureId;
    BiomeDimension dimension;
    int x, z;
    int countRadius;             /* square half-width in blocks; -1 = unbounded */
    int minCount, maxCount;      /* -1 disables a bound */
} StructureAmountCriterion;

typedef struct {
    StructureAmountCriterion *items;
    size_t count;
} StructureAmountCriteria;

typedef struct {
    BiomeCriteria biomes;
    StructureDistanceCriteria structuresDistance;
    StructureAmountCriteria structuresAmount;
} CriteriaConfig;

/* Returns 1 on success, 0 on parse/validation/I/O failure. Initialize config to {0}. */
int loadCriteriaConfig(const char *filename, CriteriaConfig *out,
                       char *error, size_t errorSize);
void freeCriteriaConfig(CriteriaConfig *config);

/* Compatibility entry points for existing biome-only users. */
int loadBiomeCriteria(const char *filename, BiomeCriteria *out,
                      char *error, size_t errorSize);
void freeBiomeCriteria(BiomeCriteria *criteria);

#endif
