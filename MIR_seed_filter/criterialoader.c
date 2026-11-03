#include "criterialoader.h"
#include "finders.h"
#include "loot/cjson/cJSON.h"
#include "loot/items.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <stdint.h>
#include <math.h>

static void setError(char *buf, size_t size, const char *fmt, ...)
{
    if (!buf || !size) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, size, fmt, args);
    va_end(args);
}

static char *copyString(const char *s)
{
    size_t n = strlen(s) + 1;
    char *copy = malloc(n);
    if (copy) memcpy(copy, s, n);
    return copy;
}

static const cJSON *field(const cJSON *obj, const char *name)
{
    return cJSON_GetObjectItemCaseSensitive(obj, name);
}

static int asInteger(const cJSON *item, int *value)
{
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        item->valuedouble < INT_MIN || item->valuedouble > INT_MAX ||
        floor(item->valuedouble) != item->valuedouble)
        return 0;
    *value = (int)item->valuedouble;
    return 1;
}

static int readInt(const cJSON *obj, const char *name, int *value)
{
    return asInteger(field(obj, name), value);
}

/* Optional integer priority, defaulting to 0. Larger means tested first.
 * The arrays are sorted once at load time, never for each seed. */
static int readDifficulty(const cJSON *obj, int *value)
{
    const cJSON *item = field(obj, "difficulty");
    return !item || asInteger(item, value);
}

static int readDouble(const cJSON *obj, const char *name, double *value)
{
    const cJSON *item = field(obj, name);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) return 0;
    *value = item->valuedouble;
    return 1;
}

static int readDimension(const cJSON *obj, BiomeDimension *out)
{
    const cJSON *item = field(obj, "dimension");
    if (!cJSON_IsString(item)) return 0;
    if (!strcmp(item->valuestring, "overworld")) *out = CRITERION_DIM_OVERWORLD;
    else if (!strcmp(item->valuestring, "nether")) *out = CRITERION_DIM_NETHER;
    else if (!strcmp(item->valuestring, "end")) *out = CRITERION_DIM_END;
    else return 0;
    return 1;
}

/* Resolve dimension-independent origins at parse time. The first End
 * gateway is seed-dependent: store a reserved coordinate pair to resolve
 * at evaluation time, when the seed is available.
 * Counts without any origin default to 0,0 as before.
 */
static int readOrigin(const cJSON *obj, BiomeDimension dimension,
                      int *x, int *z, int allowDefault)
{
    const cJSON *spawn = field(obj, "spawn");
    if (spawn && !cJSON_IsBool(spawn)) return 0;
    if (cJSON_IsTrue(spawn)) {
        *x = *z = dimension == CRITERION_DIM_END
            ? CRITERION_FIRST_END_GATEWAY : 0;
        return 1;
    }
    if (field(obj, "x") || field(obj, "z")) {
        if (!readInt(obj, "x", x) || !readInt(obj, "z", z))
            return 0;
        /* The marker is reserved, not an explicit pair of coordinates. */
        return !criterionUsesFirstEndGateway(dimension, *x, *z);
    }
    if (!allowDefault) return 0;
    *x = *z = 0;
    return 1;
}

static int readName(const cJSON *obj, char **name)
{
    const cJSON *item = field(obj, "name");
    if (!cJSON_IsString(item) || !item->valuestring[0]) return 0;
    *name = copyString(item->valuestring);
    return *name != NULL;
}

/* Strictly support regional structure types that have getStructurePos(). */
static int structureFromName(const char *name, int *id, BiomeDimension *expected)
{
    static const struct { const char *name; int id; BiomeDimension dimension; } names[] = {
        {"desert_pyramid", Desert_Pyramid, CRITERION_DIM_OVERWORLD},
        {"jungle_temple", Jungle_Temple, CRITERION_DIM_OVERWORLD},
        {"jungle_pyramid", Jungle_Temple, CRITERION_DIM_OVERWORLD},
        {"swamp_hut", Swamp_Hut, CRITERION_DIM_OVERWORLD},
        {"igloo", Igloo, CRITERION_DIM_OVERWORLD},
        {"village", Village, CRITERION_DIM_OVERWORLD},
        {"ocean_ruin", Ocean_Ruin, CRITERION_DIM_OVERWORLD},
        {"shipwreck", Shipwreck, CRITERION_DIM_OVERWORLD},
        {"monument", Monument, CRITERION_DIM_OVERWORLD},
        {"mansion", Mansion, CRITERION_DIM_OVERWORLD},
        {"outpost", Outpost, CRITERION_DIM_OVERWORLD},
        {"ruined_portal", Ruined_Portal, CRITERION_DIM_OVERWORLD},
        {"ruined_portal_n", Ruined_Portal_N, CRITERION_DIM_NETHER},
        {"ancient_city", Ancient_City, CRITERION_DIM_OVERWORLD},
        {"treasure", Treasure, CRITERION_DIM_OVERWORLD},
        {"buried_treasure", Treasure, CRITERION_DIM_OVERWORLD},
        {"mineshaft", Mineshaft, CRITERION_DIM_OVERWORLD},
        {"fortress", Fortress, CRITERION_DIM_NETHER},
        {"bastion", Bastion, CRITERION_DIM_NETHER},
        {"end_city", End_City, CRITERION_DIM_END},
        {"end_gateway", End_Gateway, CRITERION_DIM_END},
        {"end_island", End_Island, CRITERION_DIM_END},
        {"trail_ruins", Trail_Ruins, CRITERION_DIM_OVERWORLD},
        {"trial_chambers", Trial_Chambers, CRITERION_DIM_OVERWORLD}
    };
    for (size_t i = 0; i < sizeof(names)/sizeof(names[0]); i++) {
        if (!strcmp(names[i].name, name)) {
            *id = names[i].id;
            *expected = names[i].dimension;
            return 1;
        }
    }
    return 0;
}

static int readStructure(const cJSON *obj, int *id,
                         BiomeDimension *dimension)
{
    const cJSON *str = field(obj, "structure_id");
    BiomeDimension expected;
    return cJSON_IsString(str) && readDimension(obj, dimension) &&
           structureFromName(str->valuestring, id, &expected) &&
           *dimension == expected;
}

static int validBounds(double min, double max)
{
    return (min == -1 || min >= 0) && (max == -1 || max >= 0) &&
           (min == -1 || max == -1 || min <= max);
}

static int readCriterionGroup(const cJSON *obj, char **group,
                              int *groupIndex, const char *name,
                              char *err, size_t cap)
{
    const cJSON *item = field(obj, "group");
    *groupIndex = -1;
    if (field(obj, "group_min_count")) {
        setError(err, cap, "'%s' uses obsolete group_min_count; use root groups.", name);
        return 0;
    }
    if (!item) return 1;
    if (!cJSON_IsString(item) || !item->valuestring[0] ||
        !(*group = copyString(item->valuestring))) {
        setError(err, cap, "Invalid group in '%s'.", name);
        return 0;
    }
    return 1;
}

static int readBiome(const cJSON *obj, BiomeCriterion *c,
                     char *err, size_t cap)
{
    const cJSON *origin = field(obj, "origin");
    const cJSON *ids = field(obj, "biome_ids");
    if (!readName(obj, &c->name) || !cJSON_IsObject(origin) ||
        !cJSON_IsArray(ids) || !readDimension(origin, &c->dimension) ||
        !readOrigin(origin, c->dimension, &c->x, &c->z, 0) ||
        !readInt(obj, "radius", &c->radius) || c->radius <= 0 ||
        c->radius > INT_MAX / 4 ||
        !readDouble(obj, "min_area", &c->minArea) ||
        !readDouble(obj, "max_area", &c->maxArea) ||
        !validBounds(c->minArea, c->maxArea) ||
        !readDifficulty(obj, &c->difficulty)) {
        setError(err, cap, "Invalid biome criterion '%s'.",
                 c->name ? c->name : "(unnamed)");
        return 0;
    }
    if (!readCriterionGroup(obj, &c->group, &c->groupIndex,
                            c->name, err, cap)) return 0;
    size_t n = (size_t)cJSON_GetArraySize(ids);
    if (!n || n > SIZE_MAX / sizeof(int)) {
        setError(err, cap, "Invalid biome_ids size in '%s'.", c->name);
        return 0;
    }
    c->biomeIds = malloc(n * sizeof(int));
    if (!c->biomeIds) {
        setError(err, cap, "Out of memory for '%s'.", c->name);
        return 0;
    }
    const cJSON *item;
    cJSON_ArrayForEach(item, ids) {
        int id;
        if (!asInteger(item, &id) || id < 0) {
            setError(err, cap, "Invalid biome ID in '%s'.", c->name);
            return 0;
        }
        int duplicate = 0;
        for (size_t j = 0; j < c->biomeIdCount; ++j)
            if (c->biomeIds[j] == id) { duplicate = 1; break; }
        if (!duplicate) c->biomeIds[c->biomeIdCount++] = id;
    }
    return 1;
}

static int readDistance(const cJSON *obj, StructureDistanceCriterion *c,
                        char *err, size_t cap)
{
    if (!readName(obj, &c->name) ||
        !readStructure(obj, &c->structureId, &c->dimension) ||
        !readOrigin(obj, c->dimension, &c->x, &c->z, 0) ||
        !readInt(obj, "min_distance", &c->minDistance) ||
        !readInt(obj, "max_distance", &c->maxDistance) ||
        !validBounds(c->minDistance, c->maxDistance) ||
        !readDifficulty(obj, &c->difficulty)) {
        setError(err, cap, "Invalid structure distance criterion '%s'.",
                 c->name ? c->name : "(unnamed)");
        return 0;
    }
    return readCriterionGroup(obj, &c->group, &c->groupIndex,
                              c->name, err, cap);
}

static int readAmount(const cJSON *obj, StructureAmountCriterion *c,
                      char *err, size_t cap)
{
    if (!readName(obj, &c->name) ||
        !readStructure(obj, &c->structureId, &c->dimension) ||
        !readOrigin(obj, c->dimension, &c->x, &c->z, 1) ||
        !readInt(obj, "count_radius", &c->countRadius) ||
        c->countRadius < -1 ||
        !readInt(obj, "min_count", &c->minCount) ||
        !readInt(obj, "max_count", &c->maxCount) ||
        !validBounds(c->minCount, c->maxCount) ||
        (c->countRadius == -1 && c->maxCount != -1) ||
        !readDifficulty(obj, &c->difficulty)) {
        setError(err, cap, "Invalid structure amount criterion '%s' (unbounded radius requires max_count:-1).",
                 c->name ? c->name : "(unnamed)");
        return 0;
    }
    return readCriterionGroup(obj, &c->group, &c->groupIndex,
                              c->name, err, cap);
}

/* The fork provides item_name2global_id() for all known Minecraft items.
 * Both "heavy_core" and "minecraft:heavy_core" are accepted. */
static int readVaultItemId(const char *name)
{
    if (strncmp(name, "minecraft:", 10) == 0)
        return item_name2global_id(name);
    if (strchr(name, ':') != NULL)
        return ITEM_UNKNOWN;  /* only minecraft namespace is supported */
    size_t len = strlen(name);
    if (len > 200) return ITEM_UNKNOWN;
    char qualified[211];
    memcpy(qualified, "minecraft:", 10);
    memcpy(qualified + 10, name, len + 1);
    return item_name2global_id(qualified);
}

/* Minecraft enchantment resource IDs -> cubiomes enum Enchantment.
 * Keep in sync with loot/loot_functions.h from branch mc-26.3. */
static int readVaultEnchantmentId(const char *name)
{
    static const struct { const char *name; int id; } names[] = {
        {"protection", PROTECTION},
        {"fire_protection", FIRE_PROTECTION},
        {"blast_protection", BLAST_PROTECTION},
        {"projectile_protection", PROJECTILE_PROTECTION},
        {"respiration", RESPIRATION},
        {"aqua_affinity", AQUA_AFFINITY},
        {"thorns", THORNS},
        {"swift_sneak", SWIFT_SNEAK},
        {"feather_falling", FEATHER_FALLING},
        {"depth_strider", DEPTH_STRIDER},
        {"frost_walker", FROST_WALKER},
        {"soul_speed", SOUL_SPEED},
        {"sharpness", SHARPNESS},
        {"smite", SMITE},
        {"bane_of_arthropods", BANE_OF_ARTHROPODS},
        {"knockback", KNOCKBACK},
        {"fire_aspect", FIRE_ASPECT},
        {"looting", LOOTING},
        {"sweeping_edge", SWEEPING_EDGE},
        {"efficiency", EFFICIENCY},
        {"silk_touch", SILK_TOUCH},
        {"fortune", FORTUNE},
        {"luck_of_the_sea", LUCK_OF_THE_SEA},
        {"lure", LURE},
        {"power", POWER},
        {"punch", PUNCH},
        {"flame", FLAME},
        {"infinity", INFINITY_ENCHANTMENT},
        {"quick_charge", QUICK_CHARGE},
        {"multishot", MULTISHOT},
        {"piercing", PIERCING},
        {"impaling", IMPALING},
        {"riptide", RIPTIDE},
        {"loyalty", LOYALTY},
        {"channeling", CHANNELING},
        {"density", DENSITY},
        {"breach", BREACH},
        {"wind_burst", WIND_BURST},
        {"mending", MENDING},
        {"unbreaking", UNBREAKING},
        {"vanishing_curse", CURSE_OF_VANISHING},
        {"binding_curse", CURSE_OF_BINDING},
        {"lunge", LUNGE}
    };
    const char *plain = name;
    if (strncmp(plain, "minecraft:", 10) == 0)
        plain += 10;
    else if (strchr(plain, ':') != NULL)
        return NO_ENCHANTMENT;
    for (size_t i = 0; i < sizeof(names)/sizeof(names[0]); ++i)
        if (strcmp(plain, names[i].name) == 0) return names[i].id;
    return NO_ENCHANTMENT;
}

static int readVaultLoot(const cJSON *obj, VaultLootCriterion *c,
                         char *err, size_t cap)
{
    const cJSON *vaultType = field(obj, "vault_type");
    const cJSON *item = field(obj, "item");
    const cJSON *enchantment = field(obj, "enchantment");
    const cJSON *repetitions = field(obj, "repetitions");
    VaultLootRule *r = &c->rule;

    if (!readName(obj, &c->name) || !cJSON_IsString(vaultType) ||
        !readInt(obj, "min_count", &r->min_count) || r->min_count < 1 ||
        !readInt(obj, "vaults_per_window", &r->vaults_per_window) ||
        r->vaults_per_window < 1 ||
        (repetitions && !asInteger(repetitions, &r->repetitions)) ||
        !readDifficulty(obj, &c->difficulty)) {
        setError(err, cap, "Invalid vault loot criterion '%s'.",
                 c->name ? c->name : "(unnamed)");
        return 0;
    }
    if (!readCriterionGroup(obj, &c->group, &c->groupIndex,
                            c->name, err, cap)) return 0;
    if (!repetitions) r->repetitions = 1;
    if (r->repetitions < 1 ||
        r->vaults_per_window > INT_MAX / r->repetitions) {
        setError(err, cap, "Invalid window count in vault loot criterion '%s'.", c->name);
        return 0;
    }
    if (!strcmp(vaultType->valuestring, "normal"))
        r->vault_kind = VAULT_LOOT_NORMAL;
    else if (!strcmp(vaultType->valuestring, "ominous"))
        r->vault_kind = VAULT_LOOT_OMINOUS;
    else {
        setError(err, cap, "Unknown vault_type in '%s' (expected normal or ominous).", c->name);
        return 0;
    }
    if (cJSON_IsString(item) && !enchantment) {
        r->target_kind = VAULT_LOOT_TARGET_ITEM;
        r->target_id = readVaultItemId(item->valuestring);
        if (r->target_id >= 0 && r->target_id < NUM_ITEMS) return 1;
    } else if (cJSON_IsString(enchantment) && !item) {
        r->target_kind = VAULT_LOOT_TARGET_ENCHANTMENT;
        r->target_id = readVaultEnchantmentId(enchantment->valuestring);
        if (r->target_id > NO_ENCHANTMENT && r->target_id <= LUNGE) return 1;
    }
    setError(err, cap,
             "Vault loot criterion '%s' requires exactly one known item or enchantment.",
             c->name);
    return 0;
}

/* Stable insertion-sort: equal priorities retain their JSON relative order.
 * This is performed exactly once, after loading/validating a config. */
#define STABLE_SORT_BY_DIFFICULTY(items, count, type)              \
    do {                                                         \
        for (size_t sort_i = 1; sort_i < (count); ++sort_i) {     \
            type entry = (items)[sort_i];                         \
            size_t sort_j = sort_i;                               \
            while (sort_j > 0 &&                                 \
                   (items)[sort_j - 1].difficulty < entry.difficulty) { \
                (items)[sort_j] = (items)[sort_j - 1];            \
                --sort_j;                                         \
            }                                                    \
            (items)[sort_j] = entry;                              \
        }                                                        \
    } while (0)

void freeBiomeCriteria(BiomeCriteria *c)
{
    if (!c) return;
    for (size_t i = 0; i < c->count; i++) {
        free(c->items[i].name);
        free(c->items[i].group);
        free(c->items[i].biomeIds);
    }
    free(c->items);
    *c = (BiomeCriteria){0};
}

static void freeDistances(StructureDistanceCriteria *c)
{
    for (size_t i = 0; i < c->count; i++) {
        free(c->items[i].name);
        free(c->items[i].group);
    }
    free(c->items);
    *c = (StructureDistanceCriteria){0};
}

static void freeAmounts(StructureAmountCriteria *c)
{
    for (size_t i = 0; i < c->count; i++) {
        free(c->items[i].name);
        free(c->items[i].group);
    }
    free(c->items);
    *c = (StructureAmountCriteria){0};
}

static void freeVaults(VaultLootCriteria *c)
{
    for (size_t i = 0; i < c->count; i++) {
        free(c->items[i].name);
        free(c->items[i].group);
    }
    free(c->items);
    *c = (VaultLootCriteria){0};
}

void freeCriteriaConfig(CriteriaConfig *c)
{
    if (!c) return;
    freeBiomeCriteria(&c->biomes);
    freeDistances(&c->structuresDistance);
    freeAmounts(&c->structuresAmount);
    freeVaults(&c->vaultLoot);
    if (c->groups)
        for (size_t i = 0; i < c->groupCount; ++i)
            free(c->groups[i].name);
    free(c->groups);
    c->groups = NULL;
    c->groupCount = 0;
}

int loadCriteriaConfig(const char *filename, CriteriaConfig *out,
                       char *err, size_t cap)
{
    if (err && cap) err[0] = '\0';
    if (!filename || !out) {
        setError(err, cap, "Invalid loader arguments.");
        return 0;
    }
    *out = (CriteriaConfig){0};
    FILE *file = fopen(filename, "rb");
    if (!file) {
        setError(err, cap, "Cannot open '%s'.", filename);
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0) goto io_fail;
    long length = ftell(file);
    if (length < 0 || (uint64_t)length >= SIZE_MAX ||
        fseek(file, 0, SEEK_SET) != 0) goto io_fail;
    char *contents = malloc((size_t)length + 1);
    if (!contents) {
        fclose(file);
        setError(err, cap, "Out of memory reading '%s'.", filename);
        return 0;
    }
    size_t n = fread(contents, 1, (size_t)length, file);
    if (fclose(file) != 0 || n != (size_t)length) {
        free(contents);
        setError(err, cap, "Failed reading '%s'.", filename);
        return 0;
    }
    contents[n] = '\0';
    cJSON *root = cJSON_Parse(contents);
    free(contents);
    if (!cJSON_IsObject(root)) {
        setError(err, cap, "Invalid JSON object in '%s'.", filename);
        cJSON_Delete(root);
        return 0;
    }

    const cJSON *groups = field(root, "groups");
    const cJSON *biomes = field(root, "biomes");
    const cJSON *distances = field(root, "structures_distance");
    const cJSON *amounts = field(root, "structures_amount");
    const cJSON *vaults = field(root, "vault_loot");
    if (field(root, "spawn_height")) {
        setError(err, cap, "spawn_height is no longer configurable: spawn surface Y >= 64 is always required by the C filter.");
        goto failed;
    }
    if ((groups && !cJSON_IsObject(groups)) ||
        !cJSON_IsArray(biomes) ||
        (distances && !cJSON_IsArray(distances)) ||
        (amounts && !cJSON_IsArray(amounts)) ||
        (vaults && !cJSON_IsArray(vaults))) {
        setError(err, cap, "JSON requires a biomes array; optional structure/vault_loot fields must be arrays.");
        goto failed;
    }
    out->biomes.count = (size_t)cJSON_GetArraySize(biomes);
    out->structuresDistance.count = (size_t)cJSON_GetArraySize(distances);
    out->structuresAmount.count = (size_t)cJSON_GetArraySize(amounts);
    out->vaultLoot.count = (size_t)cJSON_GetArraySize(vaults);
    if (out->biomes.count)
        out->biomes.items = calloc(out->biomes.count, sizeof(*out->biomes.items));
    if (out->structuresDistance.count)
        out->structuresDistance.items = calloc(out->structuresDistance.count,
                                               sizeof(*out->structuresDistance.items));
    if (out->structuresAmount.count)
        out->structuresAmount.items = calloc(out->structuresAmount.count,
                                             sizeof(*out->structuresAmount.items));
    if (out->vaultLoot.count)
        out->vaultLoot.items = calloc(out->vaultLoot.count, sizeof(*out->vaultLoot.items));
    if ((out->biomes.count && !out->biomes.items) ||
        (out->structuresDistance.count && !out->structuresDistance.items) ||
        (out->structuresAmount.count && !out->structuresAmount.items) ||
        (out->vaultLoot.count && !out->vaultLoot.items)) {
        setError(err, cap, "Out of memory allocating criteria.");
        goto failed;
    }
    size_t i = 0;
    const cJSON *entry;
    cJSON_ArrayForEach(entry, biomes) {
        if (!cJSON_IsObject(entry) || !readBiome(entry, &out->biomes.items[i], err, cap))
            goto failed;
        i++;
    }
    i = 0;
    cJSON_ArrayForEach(entry, distances) {
        if (!cJSON_IsObject(entry) ||
            !readDistance(entry, &out->structuresDistance.items[i], err, cap))
            goto failed;
        i++;
    }
    i = 0;
    cJSON_ArrayForEach(entry, amounts) {
        if (!cJSON_IsObject(entry) ||
            !readAmount(entry, &out->structuresAmount.items[i], err, cap))
            goto failed;
        i++;
    }
    i = 0;
    cJSON_ArrayForEach(entry, vaults) {
        if (!cJSON_IsObject(entry) ||
            !readVaultLoot(entry, &out->vaultLoot.items[i], err, cap))
            goto failed;
        i++;
    }

    /* Validate all named groups only after loading EVERY phase. Each criterion
     * (not each biome ID, loot item, or discovered structure) gets one vote.
     * All matching is done now, never in the per-seed hot loop. */
    out->groupCount = groups ? (size_t)cJSON_GetArraySize(groups) : 0;
    if (out->groupCount > 1024) {
        setError(err, cap, "Too many groups (max 1024).");
        goto failed;
    }
    if (out->groupCount) {
        out->groups = calloc(out->groupCount, sizeof(*out->groups));
        if (!out->groups) {
            setError(err, cap, "Out of memory allocating groups.");
            goto failed;
        }
    }
    size_t g = 0;
    const cJSON *groupDefinition;
    cJSON_ArrayForEach(groupDefinition, groups) {
        int threshold;
        if (!groupDefinition->string || !groupDefinition->string[0] ||
            !asInteger(groupDefinition, &threshold) || threshold < 1) {
            setError(err, cap, "Invalid groups entry (expected name: positive integer).");
            goto failed;
        }
        for (size_t j = 0; j < g; ++j) {
            if (!strcmp(out->groups[j].name, groupDefinition->string)) {
                setError(err, cap, "Duplicate groups entry '%s'.", groupDefinition->string);
                goto failed;
            }
        }
        CriterionGroup *target = &out->groups[g];
        target->name = copyString(groupDefinition->string);
        if (!target->name) goto failed;
        target->minCount = threshold;
#define REGISTER_MEMBERS(array) do { \
        for (size_t j = 0; j < (array).count; ++j) { \
            if ((array).items[j].group && \
                !strcmp((array).items[j].group, target->name)) { \
                (array).items[j].groupIndex = (int)g; \
                ++target->memberCount; \
            } \
        } \
    } while (0)
        REGISTER_MEMBERS(out->vaultLoot);
        REGISTER_MEMBERS(out->structuresDistance);
        REGISTER_MEMBERS(out->biomes);
        REGISTER_MEMBERS(out->structuresAmount);
#undef REGISTER_MEMBERS
        if (target->minCount > target->memberCount) {
            setError(err, cap, "Group '%s' needs %d passing criteria but has only %d members.",
                     target->name, target->minCount, target->memberCount);
            goto failed;
        }
        ++g;
    }
#define CHECK_UNKNOWN_GROUP(array) do { \
        for (size_t j = 0; j < (array).count; ++j) { \
            if ((array).items[j].group && (array).items[j].groupIndex < 0) { \
                setError(err, cap, "Criterion '%s' references undeclared group '%s'.", \
                         (array).items[j].name, (array).items[j].group); \
                goto failed; \
            } \
        } \
    } while (0)
    CHECK_UNKNOWN_GROUP(out->vaultLoot);
    CHECK_UNKNOWN_GROUP(out->structuresDistance);
    CHECK_UNKNOWN_GROUP(out->biomes);
    CHECK_UNKNOWN_GROUP(out->structuresAmount);
#undef CHECK_UNKNOWN_GROUP

    /* Sort within each stage ONLY; never move checks between stages. */
    STABLE_SORT_BY_DIFFICULTY(out->vaultLoot.items, out->vaultLoot.count, VaultLootCriterion);
    STABLE_SORT_BY_DIFFICULTY(out->structuresDistance.items, out->structuresDistance.count, StructureDistanceCriterion);
    STABLE_SORT_BY_DIFFICULTY(out->biomes.items, out->biomes.count, BiomeCriterion);
    STABLE_SORT_BY_DIFFICULTY(out->structuresAmount.items, out->structuresAmount.count, StructureAmountCriterion);
    cJSON_Delete(root);
    return 1;

io_fail:
    fclose(file);
    setError(err, cap, "Cannot read '%s'.", filename);
    return 0;
failed:
    cJSON_Delete(root);
    freeCriteriaConfig(out);
    return 0;
}

int loadBiomeCriteria(const char *filename, BiomeCriteria *out,
                      char *err, size_t cap)
{
    if (!out) {
        setError(err, cap, "Invalid biome criteria output.");
        return 0;
    }
    *out = (BiomeCriteria){0};
    CriteriaConfig config = {0};
    if (!loadCriteriaConfig(filename, &config, err, cap)) return 0;
    *out = config.biomes;
    config.biomes = (BiomeCriteria){0};
    freeCriteriaConfig(&config);
    return 1;
}
