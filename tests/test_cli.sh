#!/bin/sh

set -eu

simulator=${1:-./build/old-school-sim}
original_directory=$(pwd)
case $simulator in
    /*) ;;
    *) simulator=$original_directory/${simulator#./} ;;
esac
cli_workspace=$(
    mktemp -d "${TMPDIR:-/tmp}/old-school-cli-workspace.XXXXXX"
)
cd "$cli_workspace"

cli_output=
cli_status=0
g8_cache=build/model-cache/old-school-value-g8-v1-t1-s424242.bin
g8_t8_cache=build/model-cache/old-school-value-g8-v1-t8-s424242.bin
mix50_cache=build/model-cache/old-school-value-g8-mix50-v1-t8-s424242.bin
probe_cache=
mix50_probe_cache=
probe_directory=

cleanup() {
    rm -f "$g8_cache" "$g8_t8_cache" "$mix50_cache"
    if [ -n "$probe_cache" ]; then
        rm -f "$probe_cache"
    fi
    if [ -n "$mix50_probe_cache" ]; then
        rm -f "$mix50_probe_cache"
    fi
    if [ -n "$probe_directory" ]; then
        rmdir "$probe_directory" 2>/dev/null || true
    fi
    cd "$original_directory"
    rm -rf "$cli_workspace"
}
trap cleanup EXIT HUP INT TERM
rm -f "$g8_cache"
rm -f "$g8_t8_cache" "$mix50_cache"

run_cli() {
    set +e
    cli_output=$("$simulator" "$@" 2>&1)
    cli_status=$?
    set -e
}

run_cli_input() {
    cli_input=$1
    shift
    set +e
    cli_output=$(
        printf '%s\n' "$cli_input" |
            "$simulator" "$@" 2>&1
    )
    cli_status=$?
    set -e
}

expect_error() {
    expected=$1
    shift
    run_cli "$@"
    if [ "$cli_status" -ne 2 ]; then
        printf 'expected CLI error status 2, got %s\n%s\n' \
            "$cli_status" "$cli_output" >&2
        exit 1
    fi
    case $cli_output in
        *"$expected"*) ;;
        *)
            printf 'expected CLI error containing %s\n%s\n' \
                "$expected" "$cli_output" >&2
            exit 1
            ;;
    esac
}

help_output=$("$simulator" --help)
case $help_output in
    *"RU Aggro: 13 Mountain, 4 Island, 3 Flying Men, 5 Ironclaw Orcs, 2 Gray Ogre, 8 Hill Giant, 3 Lightning Bolt, 2 Disintegrate"*\
"--interactive"*"learned-value-g0..g8"*"learned-value-mix50-g8"*\
"--value-generation N"*"--value-recipe NAME"*\
"--actor-policy-epochs N"*"--actor-policy-rate X"*\
"--refresh-value-g8-cache"*"--refresh-value-mix50-cache"*) ;;
    *)
        printf 'learned-generation options missing from --help\n' >&2
        exit 1
        ;;
esac

run_cli_input "q" --interactive --seed 1 \
    --train-games 1 --train-seed 424242
if [ "$cli_status" -ne 0 ]; then
    printf 'interactive quit failed\n%s\n' "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"Old School Magic Interactive"*\
"Match: Human RU Aggro vs Learned Value RU Aggro"*\
"Game seed: 1"*\
"Training seed: 424242"*\
"cards (hidden)"*\
"Game abandoned."*) ;;
    *)
        printf 'interactive banner or hidden-state rendering missing\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac

set +e
cli_output=$(
    "$simulator" --interactive --seed 1 \
        --train-games 1 --train-seed 424242 </dev/null 2>&1
)
cli_status=$?
set -e
if [ "$cli_status" -ne 0 ]; then
    printf 'interactive EOF failed\n%s\n' "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"Input closed; game abandoned."*) ;;
    *)
        printf 'interactive EOF did not abandon cleanly\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac

interactive_bad_input=$(printf 'not-a-number\n999\nq')
run_cli_input "$interactive_bad_input" --interactive --seed 1 \
    --train-games 1 --train-seed 424242
if [ "$cli_status" -ne 0 ]; then
    printf 'interactive invalid-input recovery failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"Please enter a number from 0 to "*"or q to quit."*\
"Game abandoned."*) ;;
    *)
        printf 'interactive invalid input did not reprompt\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac

expect_error "--interactive only accepts" \
    --interactive --games 1
expect_error "cannot be combined" \
    --interactive --benchmark

run_cli --games 1 --seed 1 --bots random
if [ "$cli_status" -ne 0 ]; then
    printf 'five-deck random tournament smoke failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"Old School Magic Bot Simulator"*\
"Total games: 10"*\
"RU Aggro — 13 Mountain / 4 Island / 3 Flying Men / 5 Ironclaw Orcs / 2 Gray Ogre / 8 Hill Giant / 3 Lightning Bolt / 2 Disintegrate"*) ;;
    *)
        printf 'RU deck statistics missing from tournament output\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac
for pairing in \
    "Green vs RU Aggro" \
    "Red vs RU Aggro" \
    "Blue vs RU Aggro" \
    "White vs RU Aggro"
do
    case $cli_output in
        *"$pairing"*) ;;
        *)
            printf 'RU matchup missing from tournament output: %s\n%s\n' \
                "$pairing" "$cli_output" >&2
            exit 1
            ;;
    esac
done

expect_error "require --score-probes" \
    --games 1 --value-generation 8
expect_error "require --score-probes" \
    --games 1 --value-recipe mix50
expect_error "--value-generation must be zero or eight" \
    --score-probes --value-generation 1
expect_error "--value-generation must be zero or eight" \
    --score-probes --value-generation 3
expect_error "--value-recipe must be canonical or mix50" \
    --score-probes --value-recipe mixed
expect_error "--value-recipe mix50 requires --value-generation 8" \
    --score-probes --value-recipe mix50
expect_error "invalid bot name" \
    --benchmark --games 1 --challenger learned-value-g9 \
    --baseline random
expect_error "invalid bot name" \
    --benchmark --games 1 --challenger learned-value-mix50-g7 \
    --baseline random
refresh_scope_error="requires a benchmark or probe route"
expect_error "$refresh_scope_error" \
    --games 1 --refresh-value-g8-cache
expect_error "$refresh_scope_error" \
    --benchmark --games 1 --challenger learned-value-g0 \
    --baseline random --refresh-value-g8-cache
expect_error "$refresh_scope_error" \
    --score-probes --value-generation 0 \
    --refresh-value-g8-cache
expect_error "$refresh_scope_error" \
    --games 1 --refresh-value-mix50-cache
expect_error "$refresh_scope_error" \
    --benchmark --games 1 --challenger learned-value-g8 \
    --baseline random --refresh-value-mix50-cache
expect_error "$refresh_scope_error" \
    --score-probes --value-generation 8 \
    --refresh-value-mix50-cache
expect_error "$refresh_scope_error" \
    --benchmark --games 1 --challenger learned-value-mix50-g8 \
    --baseline random --refresh-value-g8-cache
expect_error "$refresh_scope_error" \
    --score-probes --value-recipe mix50 --value-generation 8 \
    --refresh-value-g8-cache

scope_error="require a selected Actor G1"
expect_error "$scope_error" \
    --games 1 --actor-policy-epochs 16
expect_error "$scope_error" \
    --score-probes --actor-generation 0 --actor-policy-rate 0.005
expect_error "$scope_error" \
    --benchmark --games 1 --challenger learned-actor-g0 \
    --baseline random --actor-policy-epochs 16
expect_error "$scope_error" \
    --benchmark --games 1 --challenger learned-value \
    --baseline random --actor-policy-epochs 16
expect_error "$scope_error" \
    --benchmark --games 1 --challenger learned-value-g0 \
    --baseline random --actor-policy-epochs 16

run_cli --benchmark --games 1 --seed 1 --train-games 1 \
    --train-seed 424242 --challenger learned \
    --baseline random
if [ "$cli_status" -eq 2 ]; then
    printf 'legacy learned alias failed\n%s\n' "$cli_output" >&2
    exit 1
fi
learned_alias_output=$cli_output
run_cli --benchmark --games 1 --seed 1 --train-games 1 \
    --train-seed 424242 --challenger learned-value \
    --baseline random
if [ "$cli_status" -eq 2 ]; then
    printf 'legacy learned-value alias failed\n%s\n' "$cli_output" >&2
    exit 1
fi
learned_value_alias_output=$cli_output
case "$learned_alias_output
$learned_value_alias_output" in
    *"Challenger: Learned Value G0"*"Challenger: Learned Value G0"*) ;;
    *)
        printf 'legacy Value aliases changed routing\n%s\n%s\n' \
            "$learned_alias_output" "$learned_value_alias_output" >&2
        exit 1
        ;;
esac
case "$learned_alias_output
$learned_value_alias_output" in
    *"immutable Value G8"*|*"Value G8 artifact cache:"*)
        printf 'legacy Value aliases unexpectedly used G8 bundle\n%s\n%s\n' \
            "$learned_alias_output" "$learned_value_alias_output" >&2
        exit 1
        ;;
esac

positive_error="must be a positive finite number"
expect_error "must be greater than zero" \
    --benchmark --games 1 --challenger learned-actor-g1 \
    --baseline random --actor-policy-epochs 0
for invalid_rate in 0 -0.005 nan inf 0.005x 0x1p-8; do
    expect_error "$positive_error" \
        --benchmark --games 1 --challenger learned-actor-g1 \
        --baseline random --actor-policy-rate "$invalid_rate"
done

g1_args="--benchmark --games 1 --seed 1 --train-games 1 \
--train-seed 424242 --challenger learned-actor-g1 --baseline random"

# Benchmark status 0 or 1 is a strength result; status 2 is a CLI failure.
# shellcheck disable=SC2086
run_cli $g1_args
if [ "$cli_status" -eq 2 ]; then
    printf 'default G1 CLI invocation failed\n%s\n' "$cli_output" >&2
    exit 1
fi
default_fingerprint=$(
    printf '%s\n' "$cli_output" |
        sed -n 's/^  G1 fingerprint: //p'
)
case $cli_output in
    *"policy epochs=2, rate=0.001)"*) ;;
    *)
        printf 'default G1 recipe was not printed exactly\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac

# shellcheck disable=SC2086
run_cli $g1_args --actor-policy-epochs 2 --actor-policy-rate 0.001
if [ "$cli_status" -eq 2 ]; then
    printf 'explicit-default G1 CLI invocation failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
explicit_fingerprint=$(
    printf '%s\n' "$cli_output" |
        sed -n 's/^  G1 fingerprint: //p'
)
if [ -z "$default_fingerprint" ] ||
    [ "$default_fingerprint" != "$explicit_fingerprint" ]; then
    printf 'default and explicit-default G1 fingerprints differ\n' >&2
    printf 'default:  %s\nexplicit: %s\n' \
        "$default_fingerprint" "$explicit_fingerprint" >&2
    exit 1
fi

run_cli --benchmark --games 1 --seed 1 --train-games 1 \
    --train-seed 424242 --challenger learned-value-g8 \
    --baseline learned
if [ "$cli_status" -eq 2 ]; then
    printf 'Value G8 benchmark smoke failed\n%s\n' "$cli_output" >&2
    exit 1
fi
g8_generated_output=$cli_output
case $cli_output in
    *"Value G8 artifact cache: generated $g8_cache"*\
"G8 generation 8:"*\
"Challenger: Learned Value G8"*\
"Baseline: Learned Value G0"*) ;;
    *)
        printf 'Value G8 route/progress missing\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac
generated_training_count=$(
    printf '%s\n' "$g8_generated_output" |
        sed -n '/^Training immutable Value G8 /p' |
        wc -l |
        tr -d ' '
)
if [ "$generated_training_count" -ne 1 ]; then
    printf 'fresh Value G8 cache trained %s times; expected one\n%s\n' \
        "$generated_training_count" "$g8_generated_output" >&2
    exit 1
fi

expected_g3_fingerprint=$(
    printf '%s\n' "$g8_generated_output" |
        sed -n 's/^  G8 generation 3:.* -> \([0-9a-f]*\)$/\1/p'
)
if [ -z "$expected_g3_fingerprint" ]; then
    printf 'small-seed G8 bundle did not report its G3 checkpoint\n%s\n' \
        "$g8_generated_output" >&2
    exit 1
fi

run_cli --benchmark --games 1 --seed 1 --train-games 1 \
    --train-seed 424242 --challenger learned-value-g3 \
    --baseline random
if [ "$cli_status" -eq 2 ]; then
    printf 'Value G3 benchmark route failed\n%s\n' "$cli_output" >&2
    exit 1
fi
g3_output=$cli_output
case $g3_output in
    *"Value G8 artifact cache: loaded $g8_cache"*\
"Selected Value G3 fingerprint: $expected_g3_fingerprint"*\
"Challenger: Learned Value G3"*) ;;
    *)
        printf 'Value G3 did not map to exact checkpoint 3\n%s\n' \
            "$g3_output" >&2
        exit 1
        ;;
esac
case $g3_output in
    *"Training immutable Value G8"*)
        printf 'Value G3 route retrained a cached bundle\n%s\n' \
            "$g3_output" >&2
        exit 1
        ;;
esac

run_cli --benchmark --games 1 --seed 1 --train-games 1 \
    --train-seed 424242 --challenger learned-value-g3 \
    --baseline learned-value-g8
if [ "$cli_status" -eq 2 ]; then
    printf 'multi-checkpoint benchmark route failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
shared_bundle_output=$cli_output
case $shared_bundle_output in
    *"Selected Value G3 fingerprint: $expected_g3_fingerprint"*\
"Selected Value G8 fingerprint:"*\
"Challenger: Learned Value G3"*\
"Baseline: Learned Value G8"*) ;;
    *)
        printf 'multi-checkpoint benchmark was routed incorrectly\n%s\n' \
            "$shared_bundle_output" >&2
        exit 1
        ;;
esac
shared_load_count=$(
    printf '%s\n' "$shared_bundle_output" |
        sed -n '/^Loading immutable Value G8 artifact /p' |
        wc -l |
        tr -d ' '
)
shared_training_count=$(
    printf '%s\n' "$shared_bundle_output" |
        sed -n '/^Training immutable Value G8 /p' |
        wc -l |
        tr -d ' '
)
if [ "$shared_load_count" -ne 1 ] ||
    [ "$shared_training_count" -ne 0 ]; then
    printf 'G3/G8 did not share one loaded bundle (%s loads, %s trains)\n%s\n' \
        "$shared_load_count" "$shared_training_count" \
        "$shared_bundle_output" >&2
    exit 1
fi

run_cli --benchmark --games 1 --seed 1 --train-games 8 \
    --train-seed 424242 --challenger learned-value-mix50-g8 \
    --baseline random
if [ "$cli_status" -eq 2 ]; then
    printf 'Value G8 Late-Mix50 benchmark smoke failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
mix50_generated_output=$cli_output
case $mix50_generated_output in
    *"Value G8 Late-Mix50 artifact cache: generated $mix50_cache"*\
"Mix50 G8 generation 1: 2 games"*\
"collection 2 raw games/"*"0 search games/0 search examples"*\
"Mix50 G8 generation 5: 2 games"*\
"collection 1 raw games/"*"1 search games/"*\
"Challenger: Learned Value Mix50 G8"*) ;;
    *)
        printf 'Mix50 route or collection accounting missing\n%s\n' \
            "$mix50_generated_output" >&2
        exit 1
        ;;
esac
mix50_final_fingerprint=$(
    printf '%s\n' "$mix50_generated_output" |
        sed -n 's/^  Value G8 Late-Mix50 final fingerprint: //p'
)
if [ -z "$mix50_final_fingerprint" ]; then
    printf 'Mix50 final fingerprint was not printed\n%s\n' \
        "$mix50_generated_output" >&2
    exit 1
fi

# Equal generation numbers from different recipe families must resolve
# independently. This catches accidental canonical-G8/Mix50-G8 pointer
# sharing in the benchmark route.
run_cli --benchmark --games 1 --seed 1 --train-games 8 \
    --train-seed 424242 --challenger learned-value-mix50-g8 \
    --baseline learned-value-g8
if [ "$cli_status" -eq 2 ]; then
    printf 'cross-recipe Value G8 benchmark failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
cross_recipe_output=$cli_output
case $cross_recipe_output in
    *"Value G8 Late-Mix50 artifact cache: loaded $mix50_cache"*\
"Selected Value Mix50 G8 fingerprint: $mix50_final_fingerprint"*\
"Value G8 artifact cache: generated $g8_t8_cache"*\
"Selected Value G8 fingerprint:"*\
"Challenger: Learned Value Mix50 G8"*\
"Baseline: Learned Value G8"*) ;;
    *)
        printf 'canonical and Mix50 G8 routes were not isolated\n%s\n' \
            "$cross_recipe_output" >&2
        exit 1
        ;;
esac
canonical_t8_fingerprint=$(
    printf '%s\n' "$cross_recipe_output" |
        sed -n 's/^  Value G8 final fingerprint: //p'
)
if [ -z "$canonical_t8_fingerprint" ] ||
    [ "$canonical_t8_fingerprint" = "$mix50_final_fingerprint" ]; then
    printf 'canonical and Mix50 G8 fingerprints were not distinct\n' >&2
    printf 'canonical: %s\nMix50:   %s\n' \
        "$canonical_t8_fingerprint" "$mix50_final_fingerprint" >&2
    exit 1
fi
mix50_cross_training_count=$(
    printf '%s\n' "$cross_recipe_output" |
        sed -n '/^Training immutable Value G8 Late-Mix50 /p' |
        wc -l |
        tr -d ' '
)
canonical_cross_training_count=$(
    printf '%s\n' "$cross_recipe_output" |
        sed -n '/^Training immutable Value G8 (/p' |
        wc -l |
        tr -d ' '
)
if [ "$mix50_cross_training_count" -ne 0 ] ||
    [ "$canonical_cross_training_count" -ne 1 ]; then
    printf 'cross-recipe route did not load/train each family once\n%s\n' \
        "$cross_recipe_output" >&2
    exit 1
fi

probe_directory=$(
    mktemp -d "${TMPDIR:-/tmp}/old-school-g8-cli.XXXXXX"
)
probe_cache=$probe_directory/cache.tsv
mix50_probe_cache=$probe_directory/mix50-cache.tsv

run_cli --score-probes --value-generation 8 \
    --probe-worlds 2 --probe-horizon 0 \
    --train-games 1 --train-seed 424242 \
    --probe-cache "$probe_cache" --refresh-probe-cache
if [ "$cli_status" -ne 0 ]; then
    printf 'Value G8 checkpoint probe smoke failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"Value G8 artifact cache: loaded $g8_cache"*\
"Value checkpoint transitions (compact)"*\
"  Value G0:"*\
"  Value G8 base:"*\
"  Value G1:"*\
"  Value G2:"*\
"  Value G3:"*\
"  Value G4:"*\
"  Value G5:"*\
"  Value G6:"*\
"  Value G7:"*\
"  Value G8:"*\
"5 policy views"*) ;;
    *)
        printf 'ordered G0/base/G1-G8 checkpoint attribution missing\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac
g8_training_count=$(
    printf '%s\n' "$cli_output" |
        sed -n '/^Training immutable Value G8 /p' |
        wc -l |
        tr -d ' '
)
if [ "$g8_training_count" -ne 0 ]; then
    printf 'loaded Value G8 was trained %s times; expected zero\n' \
        "$g8_training_count" >&2
    exit 1
fi
combined_training_count=$(
    printf '%s\n%s\n' "$g8_generated_output" "$cli_output" |
        sed -n '/^Training immutable Value G8 /p' |
        wc -l |
        tr -d ' '
)
if [ "$combined_training_count" -ne 1 ]; then
    printf 'generated+loaded routes trained %s times; expected one\n' \
        "$combined_training_count" >&2
    exit 1
fi
g8_final_fingerprint=$(
    printf '%s\n' "$cli_output" |
        sed -n 's/^  Value G8 final fingerprint: //p'
)
g8_checkpoint_fingerprint=$(
    printf '%s\n' "$cli_output" |
        sed -n 's/^  Value G8: fingerprint \([^,]*\),.*/\1/p'
)
if [ -z "$g8_final_fingerprint" ] ||
    [ "$g8_final_fingerprint" != "$g8_checkpoint_fingerprint" ]; then
    printf 'trained G8 and scored G8 fingerprints differ\n' >&2
    printf 'trained: %s\nscored:  %s\n' \
        "$g8_final_fingerprint" "$g8_checkpoint_fingerprint" >&2
    exit 1
fi

run_cli --score-probes --value-generation 0 \
    --probe-worlds 2 --probe-horizon 0 \
    --train-games 8 --train-seed 424242 \
    --probe-cache "$mix50_probe_cache" --refresh-probe-cache
if [ "$cli_status" -ne 0 ]; then
    printf 'Mix50 Actor-owned probe cache setup failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"Cache: generated"*"5 policy views"*) ;;
    *)
        printf 'Mix50 probe setup did not publish legacy labels\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac
mix50_probe_checksum_before=$(
    cksum "$mix50_probe_cache" |
        awk '{print $1 ":" $2}'
)

run_cli --score-probes --value-recipe mix50 --value-generation 8 \
    --probe-worlds 2 --probe-horizon 0 \
    --train-games 8 --train-seed 424242 \
    --probe-cache "$mix50_probe_cache"
if [ "$cli_status" -ne 0 ]; then
    printf 'Value Mix50 checkpoint probe smoke failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
mix50_probe_output=$cli_output
case $mix50_probe_output in
    *"Value G8 Late-Mix50 artifact cache: loaded $mix50_cache"*\
"Cache: loaded"*\
"bit-identical for 14 policy views"*\
"Value checkpoint transitions (compact)"*\
"  Value G0:"*\
"  Value Mix50 base:"*\
"  Value Mix50 G1:"*\
"  Value Mix50 G2:"*\
"  Value Mix50 G3:"*\
"  Value Mix50 G4:"*\
"  Value Mix50 G5:"*\
"  Value Mix50 G6:"*\
"  Value Mix50 G7:"*\
"  Value Mix50 G8:"*\
"5 policy views"*) ;;
    *)
        printf 'ordered Mix50 base/G1-G8 attribution missing\n%s\n' \
            "$mix50_probe_output" >&2
        exit 1
        ;;
esac
mix50_probe_checksum_after=$(
    cksum "$mix50_probe_cache" |
        awk '{print $1 ":" $2}'
)
if [ "$mix50_probe_checksum_before" != \
    "$mix50_probe_checksum_after" ]; then
    printf 'Mix50 scoring rewrote the Actor-owned probe cache\n' >&2
    exit 1
fi
mix50_checkpoint_fingerprint=$(
    printf '%s\n' "$mix50_probe_output" |
        sed -n 's/^  Value Mix50 G8: fingerprint \([^,]*\),.*/\1/p'
)
if [ "$mix50_checkpoint_fingerprint" != \
    "$mix50_final_fingerprint" ]; then
    printf 'trained and probe-scored Mix50 G8 fingerprints differ\n' >&2
    printf 'trained: %s\nscored:  %s\n' \
        "$mix50_final_fingerprint" \
        "$mix50_checkpoint_fingerprint" >&2
    exit 1
fi

printf 'corrupt' >>"$mix50_cache"
expect_error "--refresh-value-mix50-cache" \
    --benchmark --games 1 --seed 1 --train-games 8 \
    --train-seed 424242 --challenger learned-value-mix50-g8 \
    --baseline random
case $cli_output in
    *"Training immutable Value G8 Late-Mix50"*)
        printf 'corrupt Mix50 cache silently retrained\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac

run_cli --benchmark --games 1 --seed 1 --train-games 8 \
    --train-seed 424242 --challenger learned-value-mix50-g8 \
    --baseline random --refresh-value-mix50-cache
if [ "$cli_status" -eq 2 ]; then
    printf 'Mix50 cache refresh failed\n%s\n' "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"Training immutable Value G8 Late-Mix50"*\
"Value G8 Late-Mix50 artifact cache: generated $mix50_cache"*) ;;
    *)
        printf 'Mix50 refresh did not regenerate atomically\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac
refreshed_mix50_fingerprint=$(
    printf '%s\n' "$cli_output" |
        sed -n 's/^  Value G8 Late-Mix50 final fingerprint: //p'
)
if [ "$refreshed_mix50_fingerprint" != \
    "$mix50_final_fingerprint" ]; then
    printf 'refreshed Mix50 fingerprint changed\n' >&2
    printf 'loaded: %s\nrefreshed: %s\n' \
        "$mix50_final_fingerprint" \
        "$refreshed_mix50_fingerprint" >&2
    exit 1
fi

printf 'corrupt' >>"$g8_cache"
expect_error "--refresh-value-g8-cache" \
    --benchmark --games 1 --seed 1 --train-games 1 \
    --train-seed 424242 --challenger learned-value-g8 \
    --baseline learned
case $cli_output in
    *"Training immutable Value G8"*)
        printf 'corrupt Value G8 cache silently retrained\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac

run_cli --benchmark --games 1 --seed 1 --train-games 1 \
    --train-seed 424242 --challenger learned-value-g8 \
    --baseline learned --refresh-value-g8-cache
if [ "$cli_status" -eq 2 ]; then
    printf 'Value G8 cache refresh failed\n%s\n' "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"Training immutable Value G8"*\
"Value G8 artifact cache: generated $g8_cache"*) ;;
    *)
        printf 'Value G8 refresh did not regenerate atomically\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac
refreshed_fingerprint=$(
    printf '%s\n' "$cli_output" |
        sed -n 's/^  Value G8 final fingerprint: //p'
)
if [ "$refreshed_fingerprint" != "$g8_final_fingerprint" ]; then
    printf 'refreshed Value G8 fingerprint changed\n' >&2
    printf 'loaded: %s\nrefreshed: %s\n' \
        "$g8_final_fingerprint" "$refreshed_fingerprint" >&2
    exit 1
fi

run_cli --score-probes --value-generation 0 \
    --probe-worlds 2 --probe-horizon 0 \
    --train-games 1 --train-seed 424242 \
    --probe-cache "$probe_cache"
if [ "$cli_status" -ne 0 ]; then
    printf 'legacy Value G0 probe smoke failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"Cache: loaded"*\
"5 policy views"*\
"Value G0 deployed policy"*) ;;
    *)
        printf 'legacy Value G0 probe output changed\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac
case $cli_output in
    *"Training immutable Value G8"*|\
*"Value checkpoint transitions (compact)"*)
        printf 'legacy Value G0 unexpectedly trained/scored G8\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac

printf 'CLI tests passed\n'
