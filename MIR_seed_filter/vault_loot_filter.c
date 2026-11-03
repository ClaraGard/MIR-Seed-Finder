/*
 * Vault loot sequence filter for hensito701/cubiomes, branch mc-26.3.
 *
 * x items in each group of y consecutive openings, repeated z times.
 * For example: HEAVY_CORE, 2, 10, 4 means at least two cores in EACH
 * of openings 1..10, 11..20, 21..30 and 31..40.
 *
 * Place this file and vault_loot_filter.h in MIR_seed_filter/.
 * Build with -I.. from MIR_seed_filter/ for the cubiomes headers.
 */
#include "vault_loot_filter.h"
#include "criterialoader.h"

#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#include "biomes.h"
#include "loot/items.h"
#include "loot/loot_tables.h"

/* RandomSupport.mixStafford13: unsigned arithmetic is intentional (mod 2^64). */
static uint64_t vault_mix_stafford13(uint64_t v)
{
    v = (v ^ (v >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    v = (v ^ (v >> 27)) * UINT64_C(0x94d049bb133111eb);
    return v ^ (v >> 31);
}

/* The MD5 of the UTF-8 random_sequence identifier, split into two
 * big-endian 64-bit words (as in Minecraft RandomSupport.seedFromHashOf).
 *   minecraft:chests/trial_chambers/reward
 *     102ea793e31f23ffd7c231952cddadf1
 *   minecraft:chests/trial_chambers/reward_ominous
 *     05a13d5ce5edaab31a3950a30a86bc23
 * Fixed identifiers avoid an MD5 dependency for a seed finder hot path.
 */
static void vault_sequence_hash(VaultLootKind kind, uint64_t *lo, uint64_t *hi)
{
    if (kind == VAULT_LOOT_OMINOUS) {
        *lo = UINT64_C(0x05a13d5ce5edaab3);
        *hi = UINT64_C(0x1a3950a30a86bc23);
    } else {
        *lo = UINT64_C(0x102ea793e31f23ff);
        *hi = UINT64_C(0xd7c231952cddadf1);
    }
}

RandomState vault_loot_initial_state(uint64_t world_seed, VaultLootKind kind)
{
    const uint64_t silver_ratio = UINT64_C(0x6a09e667f3bcc909);
    const uint64_t golden_ratio = UINT64_C(0x9e3779b97f4a7c15);
    const uint64_t lo = world_seed ^ silver_ratio;
    const uint64_t hi = lo + golden_ratio;
    uint64_t hash_lo, hash_hi;
    RandomState state;

    vault_sequence_hash(kind, &hash_lo, &hash_hi);
    state.xr.lo = vault_mix_stafford13(lo ^ hash_lo);
    state.xr.hi = vault_mix_stafford13(hi ^ hash_hi);

    /* XoroshiroRandomSource guards against the all-zero state. */
    if ((state.xr.lo | state.xr.hi) == 0) {
        state.xr.lo = golden_ratio;
        state.xr.hi = silver_ratio;
    }
    return state;
}

int vault_loot_engine_init(VaultLootEngine *engine)
{
    LootTableContext *normal = NULL;
    LootTableContext *ominous = NULL;
    if (engine == NULL)
        return 0;

    /* Generated table initializers mutate shared static structures ONCE.
     * Hence call this initialization before launching worker threads.
     */
    engine->normal_template = NULL;
    engine->ominous_template = NULL;
    if (!init_reward(&normal, MC_26_3) || normal == NULL)
        return 0;
    if (!init_reward_ominous(&ominous, MC_26_3) || ominous == NULL)
        return 0;
    engine->normal_template = normal;
    engine->ominous_template = ominous;
    return 1;
}

static int vault_rule_valid(const VaultLootRule *rule)
{
    if (rule == NULL || rule->min_count <= 0 ||
        rule->vaults_per_window <= 0 || rule->repetitions <= 0 ||
        rule->vaults_per_window > INT_MAX / rule->repetitions)
        return 0;
    if (rule->vault_kind != VAULT_LOOT_NORMAL &&
        rule->vault_kind != VAULT_LOOT_OMINOUS)
        return 0;
    if (rule->target_kind == VAULT_LOOT_TARGET_ITEM)
        return rule->target_id >= 0 && rule->target_id < NUM_ITEMS;
    if (rule->target_kind == VAULT_LOOT_TARGET_ENCHANTMENT)
        return rule->target_id > NO_ENCHANTMENT && rule->target_id <= LUNGE;
    return 0;
}

static int vault_stack_matches(const LootTableContext *context,
                               const ItemStack *stack,
                               const VaultLootRule *rule)
{
    if (rule->target_kind == VAULT_LOOT_TARGET_ITEM)
        return get_global_item_id((LootTableContext *)context, stack->item) ==
               rule->target_id;

    for (int i = 0; i < stack->enchantment_count; ++i) {
        if (stack->enchantments[i].enchantment == rule->target_id)
            return 1;
    }
    return 0;
}

static int vault_loot_filter_run(const VaultLootEngine *engine,
                                 RandomState initial,
                                 const VaultLootRule *rule,
                                 int64_t *window_counts,
                                 size_t window_count)
{
    if (engine == NULL || !vault_rule_valid(rule) ||
        (window_counts && window_count < (size_t)rule->repetitions))
        return -1;

    const LootTableContext *table = rule->vault_kind == VAULT_LOOT_OMINOUS
                                  ? engine->ominous_template
                                  : engine->normal_template;
    if (table == NULL)
        return -1;

    /* Fast impossible-case rejection for ordinary items. */
    if (rule->target_kind == VAULT_LOOT_TARGET_ITEM &&
        !has_item((LootTableContext *)table, rule->target_id)) {
        if (window_counts)
            for (int i = 0; i < rule->repetitions; ++i)
                window_counts[i] = 0;
        return 0;
    }

    /* Copy the context: the generated loot buffer AND RNG state are mutable.
     * The pool arrays are immutable after the one-time table initialization.
     */
    LootTableContext local = *table;
    set_loot_prng_type(&local, XOROSHIRO);
    set_internal_loot_seed(&local, initial);

    for (int window = 0; window < rule->repetitions; ++window) {
        int64_t found = 0;
        for (int opening = 0; opening < rule->vaults_per_window; ++opening) {
            /* Even if x has been reached, generate the remaining openings:
             * their RNG calls must be consumed before the NEXT window.
             */
            generate_loot(&local);
            if (!window_counts && found >= rule->min_count)
                continue;

            for (int s = 0; s < local.generated_item_count; ++s) {
                const ItemStack *stack = &local.generated_items[s];
                if (vault_stack_matches(&local, stack, rule) && stack->count > 0)
                    found += stack->count;
            }
        }
        if (window_counts)
            window_counts[window] = found;
        if (found < rule->min_count)
            return 0;
    }
    return 1;
}

int vault_loot_filter_match_state(const VaultLootEngine *engine,
                                  RandomState state,
                                  const VaultLootRule *rule)
{
    return vault_loot_filter_run(engine, state, rule, NULL, 0);
}

int vault_loot_filter_match(const VaultLootEngine *engine,
                            uint64_t world_seed,
                            const VaultLootRule *rule)
{
    if (!vault_rule_valid(rule))
        return -1;
    return vault_loot_filter_run(
        engine, vault_loot_initial_state(world_seed, rule->vault_kind),
        rule, NULL, 0);
}

int vault_loot_filter_window_counts(const VaultLootEngine *engine,
                                    uint64_t world_seed,
                                    const VaultLootRule *rule,
                                    int64_t *window_counts,
                                    size_t window_count)
{
    if (!vault_rule_valid(rule) || !window_counts ||
        window_count < (size_t)rule->repetitions)
        return -1;
    return vault_loot_filter_run(
        engine, vault_loot_initial_state(world_seed, rule->vault_kind),
        rule, window_counts, window_count);
}

int vault_loot_filter_match_config(const VaultLootEngine *engine,
                                   uint64_t world_seed,
                                   const struct CriteriaConfig *config)
{
    if (!engine || !config) return -1;
    if (config->vaultLoot.count && !config->vaultLoot.items) return -1;
    for (size_t i = 0; i < config->vaultLoot.count; ++i) {
        int result = vault_loot_filter_match(engine, world_seed,
                                             &config->vaultLoot.items[i].rule);
        if (result != 1) return result; /* 0 = no match, -1 = bad rule */
    }
    return 1;
}
