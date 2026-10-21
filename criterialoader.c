#include "criterialoader.h"
#include "finders.h"
#include "cJSON.h"

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
        !validBounds(c->minArea, c->maxArea)) {
        setError(err, cap, "Invalid biome criterion '%s'.",
                 c->name ? c->name : "(unnamed)");
        return 0;
    }
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
        !validBounds(c->minDistance, c->maxDistance)) {
        setError(err, cap, "Invalid structure distance criterion '%s'.",
                 c->name ? c->name : "(unnamed)");
        return 0;
    }
    return 1;
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
        (c->countRadius == -1 && c->maxCount != -1)) {
        setError(err, cap, "Invalid structure amount criterion '%s' (unbounded radius requires max_count:-1).",
                 c->name ? c->name : "(unnamed)");
        return 0;
    }
    return 1;
}

void freeBiomeCriteria(BiomeCriteria *c)
{
    if (!c) return;
    for (size_t i = 0; i < c->count; i++) {
        free(c->items[i].name);
        free(c->items[i].biomeIds);
    }
    free(c->items);
    *c = (BiomeCriteria){0};
}

static void freeDistances(StructureDistanceCriteria *c)
{
    for (size_t i = 0; i < c->count; i++) free(c->items[i].name);
    free(c->items);
    *c = (StructureDistanceCriteria){0};
}

static void freeAmounts(StructureAmountCriteria *c)
{
    for (size_t i = 0; i < c->count; i++) free(c->items[i].name);
    free(c->items);
    *c = (StructureAmountCriteria){0};
}

void freeCriteriaConfig(CriteriaConfig *c)
{
    if (!c) return;
    freeBiomeCriteria(&c->biomes);
    freeDistances(&c->structuresDistance);
    freeAmounts(&c->structuresAmount);
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

    const cJSON *biomes = field(root, "biomes");
    const cJSON *distances = field(root, "structures_distance");
    const cJSON *amounts = field(root, "structures_amount");
    if (!cJSON_IsArray(biomes) ||
        (distances && !cJSON_IsArray(distances)) ||
        (amounts && !cJSON_IsArray(amounts))) {
        setError(err, cap, "JSON requires a biomes array; structure arrays must be arrays when present.");
        goto failed;
    }
    out->biomes.count = (size_t)cJSON_GetArraySize(biomes);
    out->structuresDistance.count = (size_t)cJSON_GetArraySize(distances);
    out->structuresAmount.count = (size_t)cJSON_GetArraySize(amounts);
    if (out->biomes.count)
        out->biomes.items = calloc(out->biomes.count, sizeof(*out->biomes.items));
    if (out->structuresDistance.count)
        out->structuresDistance.items = calloc(out->structuresDistance.count,
                                               sizeof(*out->structuresDistance.items));
    if (out->structuresAmount.count)
        out->structuresAmount.items = calloc(out->structuresAmount.count,
                                             sizeof(*out->structuresAmount.items));
    if ((out->biomes.count && !out->biomes.items) ||
        (out->structuresDistance.count && !out->structuresDistance.items) ||
        (out->structuresAmount.count && !out->structuresAmount.items)) {
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
