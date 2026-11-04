#ifndef CRITERIALOADER_H
#define CRITERIALOADER_H

#include <stddef.h>
#include <limits.h>
#include "vault_loot_filter.h"

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
    char *group;                 /* optional shared group, e.g. four quadrants */
    int groupIndex;              /* -1 = mandatory; >=0 = shared group index */
    int difficulty;              /* larger value = checked earlier within biome phase */
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
    char *group;                 /* optional shared group across all criterion types */
    int groupIndex;              /* -1 = mandatory */
    int difficulty;              /* larger value = checked earlier within distance phase */
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
    char *group;
    int groupIndex;              /* -1 = mandatory */
    int difficulty;              /* larger value = checked earlier within amount phase */
} StructureAmountCriterion;

typedef struct {
    StructureAmountCriterion *items;
    size_t count;
} StructureAmountCriteria;

/* JSON section "vault_loot": ungrouped criteria are mandatory. */
typedef struct {
    char *name;
    VaultLootRule rule;
    char *group;
    int groupIndex;              /* -1 = mandatory */
    int difficulty;              /* larger value = checked earlier within vault phase */
} VaultLootCriterion;

typedef struct {
    VaultLootCriterion *items;
    size_t count;
} VaultLootCriteria;

typedef struct {
    char *name;
    int minCount;
    int memberCount;
} CriterionGroup;

/* Per-seed counts. Held on the worker stack: no per-seed group heap allocation
 * or name comparisons. Groups can contain criteria from ANY phase. */
typedef struct {
    int passed;
    int remaining;
} CriterionGroupProgress;

typedef struct {
    const CriterionGroup *definitions;
    CriterionGroupProgress *progress;
    size_t count;
} CriterionGroupRuntime;

static inline int criterionGroupShouldCheck(const CriterionGroupRuntime *state,
                                            int groupIndex)
{
    return groupIndex < 0 || !state ||
        state->progress[groupIndex].passed < state->definitions[groupIndex].minCount;
}

/* Returns false if an ungrouped criterion fails or if a group is now
 * mathematically impossible. Skipped members do not consume votes. */
static inline int criterionGroupRecord(CriterionGroupRuntime *state,
                                       int groupIndex, int passed)
{
    /* -2: independent statistics mode. Evaluate the next criterion even
     * when this one fails. Normal seedfilter3 never produces this index. */
    if (groupIndex == -2) return 1;
    if (groupIndex < 0) return passed;
    if (!state) return 0;
    CriterionGroupProgress *g = &state->progress[groupIndex];
    --g->remaining;
    if (passed) ++g->passed;
    return g->passed + g->remaining >= state->definitions[groupIndex].minCount;
}

static inline int criterionGroupsSatisfied(const CriterionGroupRuntime *state)
{
    if (!state) return 1;
    for (size_t i = 0; i < state->count; ++i)
        if (state->progress[i].passed < state->definitions[i].minCount)
            return 0;
    return 1;
}

/* Spawn surface height is mandatory and hardcoded in seedfilter3.c. */
typedef struct CriteriaConfig {
    BiomeCriteria biomes;
    StructureDistanceCriteria structuresDistance;
    StructureAmountCriteria structuresAmount;
    VaultLootCriteria vaultLoot;
    CriterionGroup *groups;
    size_t groupCount;
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
