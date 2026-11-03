# MIR Seed Finder

A multiprocess Minecraft Java Edition seed finder written in C, built on a modified [cubiomes](https://github.com/Cubitect/cubiomes) source tree. The application in `MIR_seed_filter/` tests randomly generated 64-bit world seeds against JSON-configured **biome areas**, **structure distances**, **structure counts**, and **normal/ominous vault loot** requirements.

The active program is [`MIR_seed_filter/seedfilter3.c`](MIR_seed_filter/seedfilter3.c), targeting **Minecraft Java Edition 26.3** (`MC_26_3`). MIR is built separately from cubiomes and links against `libcubiomes.a`. Its loot filtering uses a C simulation of Minecraft's loot sequences; **verify vault predictions against the target Minecraft version** before relying on them.

## Features

- Search random 64-bit seeds in parallel using independent worker **processes**.
- Check estimated biome coverage in square areas across the Overworld, Nether, and End.
- Combine biome criteria in named **at-least-N-of-M groups** (for example, mushroom fields in any 2 of 4 quadrants). Groups currently apply to **biome criteria only**.
- Enforce a **mandatory, hardcoded estimated spawn-surface height of at least Y=64**.
- Check minimum/maximum structure distances and counts, including origins based on the first linked outer End gateway.
- Filter item and enchantment rewards in successive normal or ominous vault openings.
- Set optional per-criterion `difficulty` priorities without rearranging the JSON files.
- Write matching seeds, detailed successful checks, and optionally near-match diagnostics to a text file.
- Optionally enable the tool's custom small-biomes Overworld mode; normal vanilla Overworld generation is the default.

## Quick start

Clone the repository and work from its root directory:

```bash
git clone https://github.com/ClaraGard/MIR-Seed-Finder.git cubiomes
cd cubiomes
```

**Windows (MSYS2 UCRT64):**

```bash
make -C MIR_seed_filter -j8
./MIR_seed_filter/seedfilter3.exe 10000 MIR_seed_filter/criterias/courte.json 999 MIR_seed_filter/results.txt 8 false
```

**Linux:**

```bash
make -C MIR_seed_filter -j8
./MIR_seed_filter/seedfilter3 10000 MIR_seed_filter/criterias/courte.json 999 MIR_seed_filter/results.txt 8 false
```

These commands build the application, then test **10,000 random seeds total** with the short preset, eight workers, normal Overworld generation, and minimal rejected-seed debug output. `FOUND` results are written to `MIR_seed_filter/results.txt`.

## Requirements and building

### Prerequisites

- **Windows:** [MSYS2](https://www.msys2.org/) **UCRT64**, GCC/MinGW-w64, and GNU Make. Install missing packages in the UCRT64 shell with:

  ```bash
  pacman -S --needed mingw-w64-ucrt-x86_64-gcc make
  ```

  The build automatically links Windows' BCrypt library (`-lbcrypt`) to initialize the random seed generator. Use the **UCRT64** shell; `apt` and `sudo` are not MSYS2 package-management commands.
- **Linux:** GCC, GNU Make, and a Linux environment providing `getrandom` (used by `seedfilter3.c`).
- **Source layout:** the cubiomes source/library in the repository root and MIR's custom `.c`/`.h` files in `MIR_seed_filter/`.
- **No CMake is needed for MIR.** The root `makefile` builds `libcubiomes.a`, and `MIR_seed_filter/Makefile` links the MIR executable against it. The root `CMakeLists.txt` is an alternative library build definition, not part of this workflow.

### Build and rebuild commands

Run from `cubiomes/`:

```bash
# Normal build; also builds libcubiomes.a if it is missing.
make -C MIR_seed_filter -j8

# After editing cubiomes/library sources, rebuild the library explicitly.
make -f makefile release -j8
make -C MIR_seed_filter -j8

# Clean and rebuild both components when needed.
make -C MIR_seed_filter clean
make -f makefile clean
make -C MIR_seed_filter -j8
```

You may instead enter `MIR_seed_filter/` and run `make -j8`. Windows produces `MIR_seed_filter/seedfilter3.exe`; Linux produces `MIR_seed_filter/seedfilter3`. The MIR Makefile checks whether `../libcubiomes.a` exists, but it does **not** track all library source changes, so rebuild the library separately after changing cubiomes internals.

If an older build accidentally included MIR source files in the cubiomes library, run `make -f makefile clean` once before building again. `make -C MIR_seed_filter clean` removes only the MIR executable; `make -f makefile clean` removes library build artifacts and objects.

The application Makefile compiles **only** `seedfilter3.c`, `biomeareachecker.c`, `structurechecker.c`, `criterialoader.c`, and `vault_loot_filter.c`. Older `seedfilter.c` and `seedfilter2.c` are retained for history, **not** compiled. cubiomes already includes cJSON under `loot/cjson/`: do **not** download, add, or compile another `cJSON.c` into MIR.

An earlier supplied source snapshot was built successfully on Linux. A native MSYS2 UCRT64 build was not run during that documentation check.

## Repository layout and Git-tracked files

```text
cubiomes/
├── README.md                      # This consolidated MIR guide
├── .gitignore                     # Build/output exclusions
├── makefile                       # Builds libcubiomes.a
├── CMakeLists.txt                 # Alternative library build definition
├── biomes.*, finders.*, ...       # Cubiomes world-generation code
├── loot/                          # Loot engine, cJSON, loot tables
├── features/, tables/, tests/     # Other library internals and tests
└── MIR_seed_filter/
    ├── Makefile                   # Builds the MIR application
    ├── seedfilter3.c              # Active CLI and multiprocess runner
    ├── seedfilter.c               # Legacy source; not built
    ├── seedfilter2.c              # Legacy source; not built
    ├── criterialoader.[ch]        # JSON parsing and validation
    ├── biomeareachecker.[ch]      # Biome area checks and sampling
    ├── structurechecker.[ch]      # Structure distance/count checks
    ├── vault_loot_filter.[ch]     # Vault reward sequence checks
    └── criterias/
        ├── courte.json
        ├── intermédiaire.json
        └── longue.json
```

The examined `.gitignore` excludes objects and binaries (`*.o`, `*.a`, `*.exe`, `*.dll`, `*.so`, `*.dylib`, `*.out`), temporary files (`*.tmp`), `build/`, text outputs (`*.txt`), IDE files, and other generated artifacts. In the earlier audited checkout, the MIR folder had 15 tracked files: its Makefile, five active `.c` files, four `.h` files, two legacy `.c` files, and three preset JSONs (after removal of the old folder-level README).

In that working-directory archive, the following were **local**, not tracked in Git: executables such as `seedfilter3.exe`, `libcubiomes.a` and `.o` files, `results.txt`, `resultats_*.txt`, `total_results*.txt`, and the notes file `MIR_seed_filter/criterias/critères.txt`. Files appearing in a ZIP of a working directory are not necessarily committed to GitHub. A fresh clone includes the source/configuration and must build its own executable and generate its own results.

Check the actual current tracking state with:

```bash
git ls-files MIR_seed_filter
git status --short --ignored MIR_seed_filter
```

`.gitignore` prevents **new untracked** matching files from being added automatically; it does not untrack already committed files.

## Running the seed finder

The executable uses **positional arguments**, not named flags:

```text
seedfilter3 <number-of-seeds> <criteria.json> <biomethreshold> <output-file> <processes> [true|false]
```

The first **five** arguments are required. The sixth is optional.

| Position | Argument | Accepted values | Purpose |
| --- | --- | --- | --- |
| 1 | `number-of-seeds` | Positive integer (`>= 1`) | **Total** random seeds to test, shared across all workers. In the documented source snapshot, `0` is rejected, **not** an unlimited search. |
| 2 | `criteria.json` | Existing JSON file path | Filter rules to apply. Relative paths use the process's current working directory. |
| 3 | `biomethreshold` | Nonnegative integer | How many biome criteria must have passed to qualify certain rejected candidates for debug logging. **Not** a matching requirement. |
| 4 | `output-file` | Writable file path | Destination for matching seeds and enabled diagnostics. Overwritten on a completed run; its parent directory must exist. |
| 5 | `processes` | Positive integer (`>= 1`) | Number of parallel **worker processes**, capped at the number of seeds for a finite search. |
| 6 (optional) | `true|false` | `true`, `false`, `1`, or `0` | Custom **small-biomes Overworld** mode. Defaults to `false` (standard Overworld generation). |

### Argument details

- **`number-of-seeds`:** `1000000` means one million candidates **in total**, not per worker. With eight workers the search is divided into approximately 125,000 candidates per worker. `1` is a valid small test. Each process generates an independent pseudorandom stream initialized from operating-system randomness; there is no command-line starting-seed argument, sequential scan, or guaranteed reproducible sequence. **In the documented checked-in version, `0` is rejected in both main and worker modes.** A previously discussed experimental search-until-first-match implementation was not in that snapshot; do not assume `0` enables it.
- **`criteria.json`:** use any of the three supplied files or your own JSON. The configured filters, together with the mandatory spawn-height test, determine which seeds can appear as `FOUND`.
- **`biomethreshold`:** this controls logging, **not** seed acceptance. `0` enables a large number of qualifying rejection reports, `5` requires at least five passed biome criteria for relevant debug messages, and `999` effectively suppresses those messages with the supplied presets. It does not suppress normal terminal progress or details of `FOUND` seeds. Not every failure stage produces a debug record.
- **`output-file`:** workers first write separate files named like `<output-file>.worker.N.tmp`, then merge them into the selected output. A completed run **replaces** the previous results file. Use a new filename to preserve an older run. Parent directories are not created automatically. The repository ignores `*.txt` and `*.tmp` by default.
- **`processes`:** these are OS **processes**, not threads. For example, `10000` seeds over `8` workers is about `1250` seeds each. More processes than available CPU cores may not help; choose a count that leaves room for other applications. Merged results are grouped by worker, not sorted by seed or by discovery time.
- **`true|false`:** leave unset or pass `false`/`0` for ordinary Minecraft worlds. `true`/`1` enables a **custom** small-biomes generator for the Overworld; the Nether and End are not switched by this option.

### Command examples

All commands below assume the repository root and a Windows UCRT64 shell; on Linux, replace `seedfilter3.exe` with `seedfilter3`.

```bash
# One million seeds with the intermediate preset; suppress rejected-seed debug noise.
./MIR_seed_filter/seedfilter3.exe 1000000 MIR_seed_filter/criterias/intermédiaire.json 999 MIR_seed_filter/results.txt 8 false

# Long preset, including the Heavy Core vault requirement.
./MIR_seed_filter/seedfilter3.exe 1000000 MIR_seed_filter/criterias/longue.json 999 MIR_seed_filter/results.txt 8 false

# Record qualifying near-matches with at least five passed biome checks.
./MIR_seed_filter/seedfilter3.exe 10000 MIR_seed_filter/criterias/courte.json 5 MIR_seed_filter/debug_results.txt 4 false

# Explicitly enable extensive rejected-seed debugging (potentially a huge file).
./MIR_seed_filter/seedfilter3.exe 10000 MIR_seed_filter/criterias/courte.json 0 MIR_seed_filter/debug_results.txt 4 false
```

For example, a command with `1000000`, `longue.json`, `5`, `results.txt`, `8`, and `false` tests one million seeds across eight processes using the long preset, standard Overworld generation, and logs qualifying rejected candidates after five passed biome criteria.

### Included JSON presets

| Preset | Documented purpose and content |
| --- | --- |
| [`courte.json`](MIR_seed_filter/criterias/courte.json) | Short search: documented with **20 biome-area requirements** (woods, poplar/dappled forest, bamboo), **4 structure-distance checks**, and no structure-count or vault requirement. |
| [`intermédiaire.json`](MIR_seed_filter/criterias/intermédiaire.json) | Intermediate search: documented with **19 biome-area requirements** (including poplar), **5 structure-distance checks**, and no structure-count or vault requirement. |
| [`longue.json`](MIR_seed_filter/criterias/longue.json) | Long search: multiple biome and structure requirements, structure counts, **2-of-4 Mushroom Fields quadrants** via a named group, and **at least 2 Heavy Cores per 10 ominous vault openings, repeated 4 times**. No separate Wind Burst rule. |

The long preset also includes **5 structure-distance** and **2 structure-count** requirements in the documented configuration. All three presets identify biome ID `188` (dappled forest/poplar) among their near-spawn wood and food biomes; the short and intermediate presets also have a dedicated `poplar_wood_area` check. The long preset's Overworld wood-supporting biome checks use a square half-width of 5,000 blocks. The precise requirement counts may change as presets are updated; the linked JSON files are authoritative.

## JSON configuration reference

The root JSON object supports **one optional group definition object and four filter arrays**:

| Root key | Required? | Meaning |
| --- | --- | --- |
| `groups` | No | Object mapping group names to the number of passing **biome criteria** required, e.g. `"mushroom_quadrants": 2`. Use `{}` when no groups are needed. |
| `biomes` | **Yes**, may be `[]` | Estimated combined area of selected biome IDs inside square search regions. |
| `structures_distance` | No | Distance to the nearest matching structure. |
| `structures_amount` | No | Number of matching structures in a square search region. |
| `vault_loot` | No | Item or enchantment counts in successive vault-opening windows. |

The included JSON presets place `groups` first, and use `{}` when no groups are defined. Optional arrays can be omitted or empty. **Standard JSON does not allow comments or trailing commas.** Use numeric Cubiomes biome IDs (from [`biomes.h`](biomes.h)), **not** Minecraft namespaced biome strings such as `minecraft:plains`; supported structure strings are listed below.

### Evaluation order and priorities

A seed is evaluated in this fixed order:

1. **Vault loot** (if configured).
2. **Structure distance** requirements.
3. **Mandatory spawn-height precheck** (always enabled).
4. **Biome area** requirements, including biome groups.
5. **Structure count** requirements.

The program stops testing a seed when a stage fails. **All ungrouped requirements must pass**, and each declared group must meet its threshold. Structure counts always happen **after** biome checks, including small-radius villages and rare structures: they are never moved to an earlier phase.

Every entry in any of the four filter arrays may specify an optional signed integer **`difficulty`** (default `0`):

- **Higher numbers are tested first within their own phase**; negative values are allowed.
- Equal values keep the same order as in the JSON (stable sort).
- The loader sorts criteria **in memory** once; the JSON file itself is not rewritten or reordered.
- These are **manual priorities**, not measured CPU costs. Put selective/hard checks earlier and common woods (such as oak and birch) later if that helps reject seeds faster.
- Runtime check details follow the chosen within-phase order. The biome checker generates maps lazily and reuses an existing map when it covers a later check's region.

The old per-criterion `group_min_count` and root-level `spawn_height` configuration are **rejected**, not ignored. Spawn height is intentionally not user-configurable.

### Complete custom criteria example

Save this as `MIR_seed_filter/criterias/custom.json`:

```json
{
  "groups": {},
  "biomes": [
    {
      "name": "poplar_near_origin",
      "biome_ids": [188],
      "origin": { "dimension": "overworld", "spawn": true },
      "radius": 1500,
      "min_area": 3000,
      "max_area": -1,
      "difficulty": 60
    }
  ],
  "structures_distance": [
    {
      "name": "fortress_near_nether_origin",
      "structure_id": "fortress",
      "dimension": "nether",
      "x": 0,
      "z": 0,
      "min_distance": -1,
      "max_distance": 200,
      "difficulty": 80
    }
  ],
  "structures_amount": [
    {
      "name": "at_least_three_villages",
      "structure_id": "village",
      "dimension": "overworld",
      "x": 0,
      "z": 0,
      "count_radius": 2000,
      "min_count": 3,
      "max_count": -1
    }
  ],
  "vault_loot": [
    {
      "name": "heavy_core_every_ten",
      "vault_type": "ominous",
      "item": "minecraft:heavy_core",
      "min_count": 2,
      "vaults_per_window": 10,
      "repetitions": 4,
      "difficulty": 100
    }
  ]
}
```

Run it (Windows):

```bash
./MIR_seed_filter/seedfilter3.exe 10000 MIR_seed_filter/criterias/custom.json 999 MIR_seed_filter/results.txt 8 false
```

### Biome area criteria

Example entry inside `biomes`:

```json
{
  "name": "forest_coverage",
  "biome_ids": [4, 27, 29],
  "origin": { "dimension": "overworld", "x": 1000, "z": -500 },
  "radius": 512,
  "min_area": 10000,
  "max_area": -1,
  "difficulty": 50
}
```

| Field | Meaning |
| --- | --- |
| `name` | Nonempty diagnostic label. |
| `biome_ids` | Nonempty list of nonnegative numeric Cubiomes biome IDs. Matching areas for these **alternative** IDs are combined. |
| `origin` | Nested object containing `dimension` (`overworld`, `nether`, or `end`) and either `spawn: true` or both `x` and `z`. |
| `radius` | Positive **half-width** of the square region, in blocks. `512` spans roughly 1,024 × 1,024 blocks. |
| `min_area` | Minimum estimated combined area in square blocks; `-1` disables the lower bound. |
| `max_area` | Maximum estimated combined area in square blocks; `-1` disables the upper bound. |
| `group` | Optional group name declared in root `groups`. This criterion then contributes one possible success to that group. |
| `difficulty` | Optional signed integer priority (default `0`); higher = earlier **within `biomes`**. |

Area is **estimated from samples at biome scale 16**, not measured block by block. A small requirement such as `min_area: 1` requires a detectable sample, **not** a guaranteed literal one-block biome patch. Boundaries and small areas are sensitive to this resolution. Both area bound fields should be present; use `-1` to disable either limit.

For instance:

- `"min_area": 1, "max_area": -1` requires some detectable coverage.
- `"min_area": -1, "max_area": 0` excludes detectable coverage.
- `"min_area": 10000, "max_area": 50000` requires an estimated area in that interval.

#### Named groups: require N of M biome criteria

A group is declared once at the root:

```json
"groups": { "mushroom_quadrants": 2 }
```

Each member criterion references it with `"group": "mushroom_quadrants"`. The group passes when **at least 2 distinct member criteria** pass; it does not require every member to succeed. Each member counts **at most once**, even if its `biome_ids` array lists many alternatives. The loader requires a positive integer threshold no greater than the number of members, and rejects undeclared group names. The checker skips remaining members once the threshold is met, or fails early if too few possibilities remain.

**Example — mushroom fields in any two of four quadrants.** The documented `longue.json` searches an 8,000 × 8,000 square covering X/Z from -4,000 to +4,000. Divide it into four 4,000 × 4,000 squares, each with `radius: 2000` and centers at `(-2000,-2000)`, `(2000,-2000)`, `(-2000,2000)`, and `(2000,2000)`. All four use biome ID `14` (`mushroom_fields`). One entry looks like:

```json
{
  "name": "mushroom_fields_north_west",
  "biome_ids": [14],
  "origin": { "dimension": "overworld", "x": -2000, "z": -2000 },
  "radius": 2000,
  "min_area": 1,
  "max_area": -1,
  "group": "mushroom_quadrants",
  "difficulty": 95
}
```

Add the other three entries with unique names and their corresponding centers. Together with the root declaration, this accepts **any 2 of the 4 quadrants** that show mushroom biome coverage. Ordinary ungrouped biome entries remain mandatory. **Groups do not currently combine biome, structure, and vault criteria**; only biome entries can reference them.

### Mandatory spawn-height check

The program defines `MIN_SPAWN_SURFACE_Y` as **64** in `seedfilter3.c`. Every seed that survives vault and structure-distance checks undergoes this mandatory test, whether or not any biome criteria are configured.

For this particular check, Cubiomes `getSpawn()` estimates the **actual spawn X/Z**, then `mapApproxHeight()` estimates the surface Y there. Seeds with an estimate below 64, or for which an estimate cannot be obtained, are rejected. This is a **biome-noise height estimate**, not a precise block-level Minecraft terrain/surface calculation.

There is **no JSON setting** to change, omit, or disable the rule. Adding root-level `spawn_height` is an error. Successful results include a diagnostic of the form:

```text
SPAWN_HEIGHT pos=(x,z) approx_y=... min_y=64
```

This height test uses a different spawn calculation from the shorthand `"spawn": true` criterion origins described below.

### Structure distance criteria

Example entry inside `structures_distance`:

```json
{
  "name": "bastion_near_nether_origin",
  "structure_id": "bastion",
  "dimension": "nether",
  "spawn": true,
  "min_distance": -1,
  "max_distance": 200,
  "difficulty": 70
}
```

| Field | Meaning |
| --- | --- |
| `name` | Label printed in diagnostics. |
| `structure_id` | Supported **string** identifier from the structure table below. |
| `dimension` | `overworld`, `nether`, or `end`, consistent with the structure type. |
| `spawn` or `x`, `z` | Origin **directly on this entry**. Specify `spawn: true` or both coordinates. |
| `min_distance` | Minimum horizontal X/Z distance in blocks; `-1` removes the lower bound. |
| `max_distance` | Maximum horizontal X/Z distance in blocks; `-1` removes the upper bound. |
| `difficulty` | Optional signed integer priority; higher = earlier in the **structure-distance** phase. |

The distance check finds a matching structure relative to this origin, subject to the configured bounds. For a nearby-structure rule, use `min_distance: -1` and a positive `max_distance`. Structure origins are **not** nested under `origin`.

### Structure count criteria

Example entry inside `structures_amount`:

```json
{
  "name": "at_least_three_villages",
  "structure_id": "village",
  "dimension": "overworld",
  "x": 0,
  "z": 0,
  "count_radius": 2000,
  "min_count": 3,
  "max_count": -1,
  "difficulty": 40
}
```

| Field | Meaning |
| --- | --- |
| `name` | Label printed in diagnostics. |
| `structure_id` | Supported structure identifier. |
| `dimension` | Dimension containing the structures. |
| `spawn` or `x`, `z` | Search center **directly on this entry**. If omitted for a count, defaults to `(0,0)`. |
| `count_radius` | Square search **half-width** in blocks, not a circular radius; `-1` is a special unbounded case. |
| `min_count` | Minimum matching structures; `-1` disables the lower bound. |
| `max_count` | Maximum matching structures; `-1` disables the upper bound. |
| `difficulty` | Optional signed integer priority; higher = earlier in the **structure-count** phase. |

When `count_radius` is `-1`, `max_count` **must** be `-1` because an unbounded count cannot enforce an upper limit.

### Origins and dimensions

**Biome** origins use a nested object, for example:

```json
"origin": { "dimension": "overworld", "spawn": true }
```

or:

```json
"origin": { "dimension": "overworld", "x": 250, "z": -1000 }
```

**Structure** distance/count origins instead have `dimension` and `spawn` or `x`/`z` **directly on each criterion** (see their examples above). For either kind of criterion, `"spawn": true` means a special reference point:

| Dimension | Reference point used by a criterion |
| --- | --- |
| `overworld` | `(0,0)`, **not** Minecraft's calculated player spawn. |
| `nether` | `(0,0)`. |
| `end` | The first linked **outer End gateway**, calculated from the world seed. |

Use both `x` and `z` to specify a fixed position explicitly. Unlike these criterion origins, the mandatory spawn-height precheck uses `getSpawn()` to obtain its X/Z.

### Supported structure IDs

Use these **strings** as `structure_id` values; the loader maps them to Cubiomes structure enums:

| Dimension | Supported IDs |
| --- | --- |
| **Overworld** | `desert_pyramid`, `jungle_temple`, `jungle_pyramid`, `swamp_hut`, `igloo`, `village`, `ocean_ruin`, `shipwreck`, `monument`, `mansion`, `outpost`, `ruined_portal`, `ancient_city`, `treasure`, `buried_treasure`, `mineshaft`, `trail_ruins`, `trial_chambers` |
| **Nether** | `fortress`, `bastion`, `ruined_portal_n` |
| **End** | `end_city`, `end_gateway`, `end_island` |

`jungle_pyramid` aliases `jungle_temple`; `buried_treasure` aliases `treasure`. Refer to [`MIR_seed_filter/criterialoader.c`](MIR_seed_filter/criterialoader.c) for validation/mapping and [`biomes.h`](biomes.h) for numeric biome IDs. Unsupported types can be rejected by the loader or may not give useful results.

### Vault loot criteria (normal and ominous)

Example entry inside `vault_loot`:

```json
{
  "name": "heavy_core_every_ten",
  "vault_type": "ominous",
  "item": "minecraft:heavy_core",
  "min_count": 2,
  "vaults_per_window": 10,
  "repetitions": 4,
  "difficulty": 100
}
```

| Field | Meaning |
| --- | --- |
| `name` | Diagnostic label for the vault rule. |
| `vault_type` | `normal` or `ominous`; their reward streams are independent. |
| `item` **or** `enchantment` | Exactly **one** target item ID or enchantment ID. Known `minecraft:`-prefixed or unprefixed forms are accepted. |
| `min_count` | Positive integer: minimum qualifying rewards **in each window**. |
| `vaults_per_window` | Positive integer: number of consecutive openings **per window**. |
| `repetitions` | Positive integer number of windows; optional, defaults to `1`. |
| `difficulty` | Optional signed integer priority; higher = earlier in the **vault** phase. |

The rule above requires **at least two Heavy Cores separately** in openings **1–10**, **11–20**, **21–30**, and **31–40**. These windows do **not** overlap, and each window's count resets. `repetitions: 1` expresses the simpler requirement of X rewards within the first Y openings.

To filter an enchantment instead of an item:

```json
{
  "name": "wind_burst_within_twelve",
  "vault_type": "ominous",
  "enchantment": "minecraft:wind_burst",
  "min_count": 1,
  "vaults_per_window": 12
}
```

An enchantment target counts generated items carrying that enchantment; it does **not** require a particular enchantment level. You may use multiple `vault_loot` entries to require several targets (AND logic). The supplied `longue.json` requires Heavy Cores but **does not** currently include a separate Wind Burst requirement; the short and intermediate presets have no vault conditions.

Vault output represents the simulated **initial sequence of rewards for the seed**, not loot assigned to particular trial-chamber coordinates. Openings already performed in an existing world can affect which reward is next. Normal and ominous sequences are treated independently. Predictions still require in-game verification for Minecraft 26.3.

## Results and diagnostics

A successful seed has a `FOUND` line and details of the configured checks. An **illustrative**, not real, example:

```text
FOUND seed=123456789
VAULT_LOOT seed=123456789 passed=1 failed=0 evaluated=1 total=1
  [heavy_core_every_ten] type=ominous target=item:minecraft:heavy_core min_count=2 vaults_per_window=10 repetitions=4 status=PASS
    openings=1-10 count=2 required>=2 status=PASS
    ...
STRUCTURES_DISTANCE seed=123456789 ...
SPAWN_HEIGHT pos=(x,z) approx_y=... min_y=64
BIOMES seed=123456789 ...
STRUCTURES_AMOUNT seed=123456789 ...
```

With a vault condition, the report includes **per-window counts**. A matching seed records the spawn-height diagnostic. Some stages output additional positions, distances, or counts, while biome area tests can stop as soon as the condition is decided and need not report an exact area.

Rejected seeds can produce `BIOME_DEBUG` or `STRUCTURE_AMOUNT_DEBUG` reports if enough biome checks passed to meet `biomethreshold`; some documentation also names `STRUCTURE_DISTANCE_DEBUG`, but **not every rejection stage is logged**, and a structure-distance-only rejection may be silent. The threshold does not change acceptance, and a failure in vault filtering does not by itself produce a biome-threshold report. Progress messages still appear in the terminal.

Each process writes temporary results to `<output-file>.worker.N.tmp`. Those are merged into the final file at the end of the run, **grouped by worker**, not sorted by discovery time or seed value. The output is overwritten on the next completed run. World seeds are printed as **signed 64-bit integers**, so negative values are valid.

## Performance notes and limitations

- Start with a small run to check that your JSON parses and outputs plausible results. `biomethreshold` set to `0` on the CLI can make rejected-seed logging enormous; use a high threshold when you only care about matches.
- Bigger biome regions and larger structure-search ranges can increase CPU time considerably. Biome sampling is approximate and uses scale 16; biome checks generate maps lazily and reuse a map when it already covers a later requested region.
- Use `difficulty` to improve **within-phase** short-circuiting without changing the fixed stage order or the order of entries in your JSON. Rare or selective checks can be assigned higher priorities; this is manual tuning, not automatically computed cost estimation.
- Ungrouped checks use **AND** logic; alternative biome IDs **within one entry** contribute combined coverage (OR-like behavior); named biome groups allow **at least N of M** member entries. Groups cannot currently mix structure, biome, and vault checks.
- Biome `radius` and structure `count_radius` describe **squares**, not circles. Structure distances are horizontal X/Z distances.
- A wood-supporting biome passing the filter is **not proof of a particular tree or usable log** in the game. Structure viability, the spawn-height estimate, and biome/vault predictions should be verified against the exact Minecraft version and world settings to be used.
- On normal worlds, keep small-biomes mode set to `false`. The alternate mode changes **Overworld** generation only.
- For source ownership, redistribution, and applicable licenses, see [`LICENSE`](LICENSE) and the licensing notes under [`loot/`](loot/).
