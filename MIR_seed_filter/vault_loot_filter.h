#ifndef VAULT_LOOT_FILTER_H
#define VAULT_LOOT_FILTER_H

#include <stdint.h>
#include <stddef.h>
#include "loot/loot_table_context.h"

struct CriteriaConfig;  /* from criterialoader.h */

#ifdef __cplusplus
extern "C" {
#endif

/* Vanilla trial vault sequences are separate for normal and ominous vaults. */
typedef enum VaultLootKind {
    VAULT_LOOT_NORMAL = 0,
    VAULT_LOOT_OMINOUS = 1
} VaultLootKind;

typedef enum VaultLootTargetKind {
    VAULT_LOOT_TARGET_ITEM = 0,
    VAULT_LOOT_TARGET_ENCHANTMENT = 1
} VaultLootTargetKind;

typedef struct VaultLootRule {
    VaultLootKind vault_kind;
    VaultLootTargetKind target_kind;
    int target_id;           /* ITEM_HEAVY_CORE or WIND_BURST, for example */
    int min_count;           /* x: minimum items per window */
    int vaults_per_window;   /* y: number of consecutive vault openings */
    int repetitions;         /* z: number of consecutive windows */
} VaultLootRule;

typedef struct VaultLootEngine {
    const LootTableContext *normal_template;
    const LootTableContext *ominous_template;
} VaultLootEngine;

/* Call ONCE, on a single thread, before starting the parallel seed search.
 * Returns 1 on success, 0 on failure. The caller owns no allocations.
 */
int vault_loot_engine_init(VaultLootEngine *engine);

/* Initial Xoroshiro128++ state for vanilla random sequences with default
 * settings: salt=0, include_world_seed=true, include_sequence_id=true.
 * Doesn't consume any RNG values.
 */
RandomState vault_loot_initial_state(uint64_t world_seed, VaultLootKind kind);

/* Test a seed. Returns 1=matches, 0=doesn't match, -1=invalid arguments.
 * Each window MUST pass independently; this is NOT a rolling window.
 * Safe to call concurrently after a serial vault_loot_engine_init().
 */
int vault_loot_filter_match(const VaultLootEngine *engine,
                            uint64_t world_seed,
                            const VaultLootRule *rule);

/* Same filter but from an explicit saved random-sequence state, e.g. after
 * other openings /random reset or when testing a world already in progress.
 * The passed state is copied; this function doesn't change it.
 */
int vault_loot_filter_match_state(const VaultLootEngine *engine,
                                  RandomState state,
                                  const VaultLootRule *rule);

/* Evaluate a rule and collect the exact number of target items/enchantments
 * obtained in each consecutive window. window_counts must contain at least
 * rule->repetitions entries. On failure, only the windows up to the first
 * failing window are written. Returns 1=match, 0=nonmatch, -1=error.
 * Note: this recomputes the sequence, so call it only for diagnostic output
 * or seeds that passed the fast match check.
 */
int vault_loot_filter_window_counts(const VaultLootEngine *engine,
                                    uint64_t world_seed,
                                    const VaultLootRule *rule,
                                    int64_t *window_counts,
                                    size_t window_count);

/* Evaluate every rule loaded from JSON's vault_loot array (logical AND).
 * Returns 1 if all pass (also for an empty array), 0 if any fails,
 * -1 if any rule/argument is invalid.
 */
int vault_loot_filter_match_config(const VaultLootEngine *engine,
                                   uint64_t world_seed,
                                   const struct CriteriaConfig *config);

#ifdef __cplusplus
}
#endif
#endif
