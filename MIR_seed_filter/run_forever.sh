#!/usr/bin/env bash
set -Eeuo pipefail

# Run from cubiomes/MIR_seed_filter
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel)"
cd "$SCRIPT_DIR"

PROCESSES=2
BIOME_THRESHOLD=999

PUSH_REMOTE=origin
PUSH_BRANCH=master

# Prevent multiple instances from running simultaneously
exec 9>"$SCRIPT_DIR/.run_forever.lock"

if ! flock -n 9; then
    echo "Another instance is already running."
    exit 1
fi

if [[ ! -x "$SCRIPT_DIR/seedfilter3" ]]; then
    echo "Error: seedfilter3 executable not found."
    exit 1
fi

PRESETS=(courte intermediaire longue)
mkdir -p "$REPO_DIR/sample_seeds"

# Count seeds (not lines of diagnostic output). Missing files count as zero.
count_seeds() {
    local file="$1"
    if [[ -f "$file" ]]; then
        awk '/^FOUND seed=/{count++} END {print count+0}' "$file"
    else
        echo 0
    fi
}

# Start at courte; on ties, prefer the preset after the last one run.
last_index=-1

while true; do
    selected_index=-1
    lowest_count=-1

    echo "Current saved seed counts:"
    for ((offset = 1; offset <= ${#PRESETS[@]}; offset++)); do
        index=$(( (last_index + offset) % ${#PRESETS[@]} ))
        candidate="${PRESETS[index]}"
        candidate_file="$REPO_DIR/sample_seeds/${candidate}_26_3.txt"
        count="$(count_seeds "$candidate_file")"
        echo "  $candidate: $count"

        if (( selected_index == -1 || count < lowest_count )); then
            selected_index=$index
            lowest_count=$count
        fi
    done

    last_index=$selected_index
    preset="${PRESETS[selected_index]}"

    # Select JSON and small biomes option
    case "$preset" in
        courte)
            JSON="criterias/courte.json"
            SMALL_BIOMES=true
            SEED_COUNT=1000000000
            ;;
        intermediaire)
            if [[ -f "criterias/intermédiaire.json" ]]; then
                JSON="criterias/intermédiaire.json"
            else
                JSON="criterias/intermediaire.json"
            fi
            SMALL_BIOMES=true
            SEED_COUNT=1000000000
            ;;
        longue)
            JSON="criterias/longue.json"
            SMALL_BIOMES=false
            SEED_COUNT=30000000000
            ;;
    esac

    FULL_FILE="$REPO_DIR/sample_seeds/${preset}_26_3.txt"
    REL_FILE="sample_seeds/${preset}_26_3.txt"

    # Temporary batch file
    BATCH_FILE="$(mktemp "$SCRIPT_DIR/batch_${preset}_XXXXXXXX.txt")"

    echo "========================================"
    echo "Starting $preset (currently $lowest_count saved seeds)"
    echo "Seeds: $SEED_COUNT"
    echo "Processes: $PROCESSES"
    echo "Small biomes: $SMALL_BIOMES"
    echo "Time: $(date)"
    echo "========================================"

    # Run seedfilter3
    if ! ./seedfilter3 \
        "$SEED_COUNT" \
        "$JSON" \
        "$BIOME_THRESHOLD" \
        "$BATCH_FILE" \
        "$PROCESSES" \
        "$SMALL_BIOMES"; then

        echo "Seedfilter failed!"
        echo "Batch saved at $BATCH_FILE"
        exit 1
    fi

    # Append results to permanent file
    if [[ -s "$BATCH_FILE" ]]; then
        if [[ -s "$FULL_FILE" ]]; then
            printf '\n' >> "$FULL_FILE"
        fi

        cat "$BATCH_FILE" >> "$FULL_FILE"
        printf '\n' >> "$FULL_FILE"

        echo "Results appended to $FULL_FILE"
    else
        echo "No seeds found in this batch."
    fi

    rm -f "$BATCH_FILE"

    # Force-add because *.txt may be ignored
    if [[ -f "$FULL_FILE" ]]; then
        git -C "$REPO_DIR" add -f -- "$REL_FILE"

        # Commit only if this result file changed
        if ! git -C "$REPO_DIR" diff --cached --quiet -- "$REL_FILE"; then
            git -C "$REPO_DIR" commit --only \
                -m "Add $preset seeds - $(date -u)" \
                -- "$REL_FILE"
        fi
    fi

    # Push all pending commits
    if ! git -C "$REPO_DIR" push \
        "$PUSH_REMOTE" "HEAD:$PUSH_BRANCH"; then

        echo "Git push failed. Will retry after next batch."
    fi

    echo "Finished $preset"
    echo
done
