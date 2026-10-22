# Minecraft Seed Filter

A multithreaded Minecraft seed search tool written in C, built on [Cubiomes](https://github.com/Cubitect/cubiomes). It tests randomly generated 64-bit seeds against configurable biome-area and structure requirements defined in JSON.

The current source targets **Minecraft Java Edition 1.21** (`#define MC MC_1_21`). If you are targeting a different version, review the version selection and compatibility of the Cubiomes code before building.

## Features

- Tests random 64-bit Minecraft seeds using multiple worker processes.
- Filters by the total area covered by one or more biome IDs in a region.
- Filters by the distance to structures and the number of structures within a radius.
- Supports the Overworld, Nether and End, including an origin based on the first linked End gateway.
- Writes fully matching seeds and optional near-match diagnostics to a text file.
- Supports an optional custom small-biomes Overworld generation mode.

## Requirements

- A C compiler such as GCC (MinGW-w64 on Windows).
- The Cubiomes sources and the project's custom `.c`/`.h` files in your build directory.
- `cJSON.c` and `cJSON.h`.
- On Windows, the system BCrypt library (linked with `-lbcrypt`).

## Building

Run the following command from the directory containing the source files.

### Windows (MinGW-w64 / GCC)

```bash
gcc -O3 -o seedfilter.exe seedfilter3.c biomes.c biomenoise.c finders.c generator.c layers.c noise.c quadbase.c util.c biomeareachecker.c criterialoader.c structurechecker.c cJSON.c -lm -lbcrypt
```

### Linux (GCC)

```bash
gcc -O3 -o seedfilter seedfilter3.c biomes.c biomenoise.c finders.c generator.c layers.c noise.c quadbase.c util.c biomeareachecker.c criterialoader.c structurechecker.c cJSON.c -lm
```

The Linux implementation uses `getrandom()`, so this command is intended for Linux rather than macOS. If you have renamed `seedfilter3.c` or your source files, update the command accordingly. You can add `-march=native` for a machine-specific optimized build.

## Running

```text
seedfilter <number-of-seeds> <criteria.json> <biomethreshold> <output-file> <processes> [true|false]
```

On Windows, use `seedfilter.exe`; on Linux, use `./seedfilter`.

| Argument | Meaning |
| --- | --- |
| `number-of-seeds` | Total number of random seeds to test, across all workers (positive integer). |
| `criteria.json` | Path to a JSON file containing the filter rules. |
| `biomethreshold` | Minimum number of passed biome criteria needed to save diagnostics for a rejected candidate. `0` logs many more rejected candidates. It does **not** relax the requirements for a `FOUND` seed. |
| `output-file` | Destination text file (for example, `results.txt`). |
| `processes` | Number of parallel worker processes. |
| `true|false` | Optional small-biomes mode; defaults to `false`. Also accepts `1` or `0`. |

**Example (Windows):**

```bash
./seedfilter.exe 1000000 filters.json 5 results.txt 8 false
```

**Example (Linux):**

```bash
./seedfilter 1000000 filters.json 5 results.txt 8 false
```

This tests **1,000,000 seeds in total** using **8 processes**, loads `filters.json`, and saves fully matching seeds plus qualifying diagnostic entries to `results.txt`.

Each worker generates its own pseudorandom sequence, initialized from operating-system randomness. The command does not take a starting seed, and repeated runs do not search a reproducible sequence. Workers write separate temporary files, which are merged into the final output when they finish.

## Creating filter JSON files

The JSON root object contains three arrays:

- `biomes` — biome area requirements (**required**, but may be empty).
- `structures_distance` — nearest-structure distance requirements (optional).
- `structures_amount` — structure count requirements (optional).

**All criteria must pass** for a seed to be written as `FOUND`. Within a *single* `biome_ids` array, however, the IDs are treated as alternatives: the filter measures the **combined area** of any of those biomes. To require separate biome types, put them in separate criteria.

Create a file named `filters.json`:

```json
{
  "biomes": [
    {
      "name": "Plains or forest near origin",
      "biome_ids": [1, 4],
      "origin": { "dimension": "overworld", "spawn": true },
      "radius": 256,
      "min_area": 10000,
      "max_area": -1
    },
    {
      "name": "Warped forest near Nether origin",
      "biome_ids": [172],
      "origin": { "dimension": "nether", "spawn": true },
      "radius": 128,
      "min_area": 1,
      "max_area": -1
    }
  ],
  "structures_distance": [
    {
      "name": "Fortress near Nether origin",
      "structure_id": "fortress",
      "dimension": "nether",
      "spawn": true,
      "min_distance": -1,
      "max_distance": 128
    },
    {
      "name": "End city near gateway",
      "structure_id": "end_city",
      "dimension": "end",
      "spawn": true,
      "min_distance": -1,
      "max_distance": 256
    }
  ],
  "structures_amount": [
    {
      "name": "At least two villages",
      "structure_id": "village",
      "dimension": "overworld",
      "spawn": true,
      "count_radius": 1024,
      "min_count": 2,
      "max_count": -1
    }
  ]
}
```

> **Biome IDs are Cubiomes numeric IDs**, not Minecraft namespaced identifiers (such as `minecraft:plains`). Verify IDs in the `enum BiomeID` in `biomes.h` for your Cubiomes version. The JSON example is illustrative; check the numeric ID of any biome you change or add.

### 1. Biome area criteria

```json
{
  "name": "Forest coverage",
  "biome_ids": [4, 27, 29],
  "origin": { "dimension": "overworld", "x": 1000, "z": -500 },
  "radius": 512,
  "min_area": 50000,
  "max_area": -1
}
```

| Field | Meaning |
| --- | --- |
| `name` | Nonempty label shown in diagnostics. |
| `biome_ids` | Nonempty array of nonnegative Cubiomes biome IDs; matching area is combined. |
| `origin` | Object specifying `dimension` and either `spawn: true` or both `x` and `z`. |
| `radius` | Positive search-region radius in blocks. |
| `min_area` | Minimum matching area in square blocks (`-1` disables lower limit). |
| `max_area` | Maximum matching area in square blocks (`-1` disables upper limit). |

The area is **estimated** from generated biome samples, rather than counted block-by-block. Consequently, very small required areas and boundaries can be sensitive to the sampling scale. The current program's biome check uses scale `16`.

**Examples of area constraints:**

- `"min_area": 1, "max_area": -1` — require some matching biome coverage.
- `"min_area": -1, "max_area": 0` — require no matching biome coverage.
- `"min_area": 10000, "max_area": 50000` — require estimated area within that interval.

The values `-1` disable an individual bound; do **not** omit the fields.

### 2. Structure distance criteria

```json
{
  "name": "Bastion within 200 blocks",
  "structure_id": "bastion",
  "dimension": "nether",
  "spawn": true,
  "min_distance": -1,
  "max_distance": 200
}
```

| Field | Meaning |
| --- | --- |
| `name` | Diagnostic label. |
| `structure_id` | Supported structure name (see below). |
| `dimension` | `overworld`, `nether`, or `end`; must match the structure's dimension. |
| `spawn` or `x`, `z` | Origin for the distance check. |
| `min_distance` | Minimum distance in blocks, or `-1` for no lower bound. |
| `max_distance` | Maximum distance in blocks, or `-1` for no upper bound. |

Distances are measured in the X/Z plane. For a nearby-structure requirement, use `min_distance: -1` and a positive `max_distance`.

### 3. Structure count criteria

```json
{
  "name": "Three or more villages",
  "structure_id": "village",
  "dimension": "overworld",
  "x": 0,
  "z": 0,
  "count_radius": 2000,
  "min_count": 3,
  "max_count": -1
}
```

| Field | Meaning |
| --- | --- |
| `name` | Diagnostic label. |
| `structure_id` | Supported structure name. |
| `dimension` | Dimension containing the structure. |
| `spawn` or `x`, `z` | Center of the count search; omitted origin defaults to `(0, 0)` for counts. |
| `count_radius` | Radius in blocks around that origin; `-1` is an unbounded special case. |
| `min_count` | Minimum required number, or `-1` to disable. |
| `max_count` | Maximum allowed number, or `-1` to disable. |

**Important:** If `count_radius` is `-1`, `max_count` must also be `-1`. Unbounded counting cannot enforce an upper limit.

### Origins and dimensions

Origins use different JSON shapes depending on the filter type.

For a biome criterion, place the origin in a nested `origin` object:

```json
"origin": { "dimension": "overworld", "spawn": true }
```

or

```json
"origin": { "dimension": "overworld", "x": 250, "z": -1000 }
```

For structure criteria, put `dimension`, `spawn` or `x`/`z` **directly on the criterion** (no nested `origin` object).

`"spawn": true` follows these conventions in this implementation:

| Dimension | Reference point |
| --- | --- |
| `overworld` | `(0, 0)` — **not** an exact calculation of the game's actual player spawn. |
| `nether` | `(0, 0)`. |
| `end` | The first linked End gateway, calculated for the seed. |

To target an exact coordinate in any dimension, provide both `x` and `z` instead of `spawn: true`.

### Supported structure IDs

Use the following **strings** in `structure_id` (not the numeric C enum values):

| Dimension | Structure IDs |
| --- | --- |
| Overworld | `desert_pyramid`, `jungle_temple`, `jungle_pyramid`, `swamp_hut`, `igloo`, `village`, `ocean_ruin`, `shipwreck`, `monument`, `mansion`, `outpost`, `ruined_portal`, `ancient_city`, `treasure`, `buried_treasure`, `mineshaft`, `trail_ruins`, `trial_chambers` |
| Nether | `fortress`, `bastion`, `ruined_portal_n` |
| End | `end_city`, `end_gateway`, `end_island` |

`jungle_pyramid` is an alias of `jungle_temple`, and `buried_treasure` is an alias of `treasure`. These strings are validated and mapped to Cubiomes structure enum IDs by `criterialoader.c`.

## Output and diagnostics

A fully matching seed is written with a `FOUND` line, followed by structure check details where implemented:

```text
FOUND seed=123456789
STRUCTURES_DISTANCE ...
STRUCTURES_AMOUNT ...
```

Rejected candidates can also be recorded if they reached the chosen `biomethreshold`. Diagnostic sections may include `BIOME_DEBUG`, `STRUCTURE_DISTANCE_DEBUG`, or `STRUCTURE_AMOUNT_DEBUG`, showing which checks passed or failed, estimated areas, distances, positions, or structure counts.

**`biomethreshold` is a logging threshold, not a filter requirement.** All configured checks still have to pass for a `FOUND` result. Setting it too low can make output files extremely large; setting it higher reduces diagnostic noise.

The output file is overwritten on a new run. Temporary worker outputs are combined after the workers finish, so results in the final file are grouped by worker, not necessarily ordered by discovery time or seed number.

## Tips and limitations

- Start with a small seed count to verify that the JSON loads and the output is sensible.
- Choose a process count that leaves enough CPU capacity for other work; `processes` is a number of processes, not threads per process.
- Larger biome regions and larger structure search radii can cost considerably more CPU time.
- Multiple entries in `biomes` enforce **AND** logic; multiple IDs within one entry enforce **OR/combined-area** logic. This matters when requiring multiple wood-supporting biomes or separate biome families.
- Standard JSON does not support comments or trailing commas.
- This is a seed-based **approximation/search** tool. Structure viability and sampled biome coverage are not a substitute for checking important finds in the actual Minecraft version and world settings you intend to play.
