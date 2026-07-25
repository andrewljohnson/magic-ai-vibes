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
g8_cache=build/model-cache/old-school-value-g8-v2-t1-s424242.bin
g8_t8_cache=build/model-cache/old-school-value-g8-v2-t8-s424242.bin
mix50_cache=build/model-cache/old-school-value-g8-mix50-v2-t8-s424242.bin
challenger_c1_cache=build/model-cache/old-school-value-challenger-v2-c1-t1-s424242.bin
challenger_c2_cache=build/model-cache/old-school-value-challenger-v2-c2-t1-s424242.bin
context_challenger_c1_cache=build/model-cache/old-school-value-context-s1-v2-c1-t1-s424242.bin
probe_cache=
mix50_probe_cache=
validation_probe_cache=
probe_directory=

cleanup() {
    rm -f "$g8_cache" "$g8_t8_cache" "$mix50_cache" \
        "$challenger_c1_cache" "$challenger_c2_cache" \
        "$context_challenger_c1_cache"
    if [ -n "$probe_cache" ]; then
        rm -f "$probe_cache"
    fi
    if [ -n "$mix50_probe_cache" ]; then
        rm -f "$mix50_probe_cache"
    fi
    if [ -n "$validation_probe_cache" ]; then
        rm -f "$validation_probe_cache"
    fi
    if [ -n "$probe_directory" ]; then
        rmdir "$probe_directory" 2>/dev/null || true
    fi
    cd "$original_directory"
    rm -rf "$cli_workspace"
}
trap cleanup EXIT HUP INT TERM
rm -f "$g8_cache" "$g8_t8_cache" "$mix50_cache" \
    "$challenger_c1_cache" "$challenger_c2_cache" \
    "$context_challenger_c1_cache"

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
    *"Red: 15 Mountain, 9 Lightning Bolt, 7 Ironclaw Orcs, 4 Gray Ogre, 3 Hill Giant, 2 Fire Elemental"*\
"Blue: 15 Island, 1 Mox Sapphire, 1 Sol Ring, 1 Ancestral Recall, 1 Time Walk, 1 Braingeyser, 4 Flying Men, 4 Force Spike, 8 Counterspell, 4 Air Elemental"*\
"RU Aggro: 13 Mountain, 4 Island, 3 Flying Men, 5 Ironclaw Orcs, 2 Gray Ogre, 8 Hill Giant, 3 Lightning Bolt, 2 Disintegrate"*\
"--interactive"*"learned-value-g0..g8"*"learned-value-cN"*\
"learned-value-context-cN"*\
"learned-value-mix50-g8"*\
"--value-generation N"*"--value-recipe NAME"*\
"--actor-policy-epochs N"*"--actor-policy-rate X"*\
"--refresh-value-challenger-cache"*\
"--refresh-value-g8-cache"*"--refresh-value-mix50-cache"*\
"--evolve-pilot BOT"*"learned-value-context-cN"*) ;;
    *)
        printf 'learned-generation options missing from --help\n' >&2
        exit 1
        ;;
esac
for learned_option in "--learned-generations N" "--learned-rollouts N"
do
    case $help_output in
        *"$learned_option"*) ;;
        *)
            printf 'missing %s from --help\n' "$learned_option" >&2
            exit 1
            ;;
    esac
done
case $help_output in
    *"--probe-corpus NAME"*"validation-v1"*) ;;
    *)
        printf 'probe-corpus selector missing from --help\n' >&2
        exit 1
        ;;
esac
case $help_output in
    *"--value-continuation-epsilon X"*\
"Value-mirror continuation priority actions in [0,1]"*\
"the deployed root remains greedy (default: 0)"*) ;;
    *)
        printf 'Value continuation epsilon contract missing from --help\n' \
            >&2
        exit 1
        ;;
esac
case $help_output in
    *"--diagnose-value-context"*\
"Audit phase/pass context omitted from the current Value observation"*\
"accepts no other options"*) ;;
    *)
        printf 'Value context diagnostic contract missing from --help\n' \
            >&2
        exit 1
        ;;
esac

run_cli --diagnose-value-context
if [ "$cli_status" -ne 0 ]; then
    printf 'Value context diagnostic failed\n%s\n' "$cli_output" >&2
    exit 1
fi
value_context_output=$cli_output
run_cli --diagnose-value-context
if [ "$cli_status" -ne 0 ] ||
    [ "$cli_output" != "$value_context_output" ]; then
    printf 'Value context diagnostic was not deterministic\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"Value Context Alias Audit"*\
"Complete legal action sets identical: yes"*\
"Critic state features bit-identical: yes"*\
"Neutral policy/action features differ: yes"*\
"Pass with prior count 0: Passed; next player 1; pass count 1; stack 1; root life 3"*\
"Pass with prior count 1: StackObjectResolved; next player 0; pass count 0; stack 0; root life 0 (lethal)"*\
"FirstMain / SecondMain action sets identical: yes"*\
"Opponent hidden-card substitution bit-identical: yes"*\
"Result: context alias demonstrated"*) ;;
    *)
        printf 'Value context diagnostic evidence missing\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac
case $cli_output in
    *"Training"*)
        printf 'Value context diagnostic unexpectedly trained a model\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac

expect_error "--diagnose-value-context accepts no other options" \
    --diagnose-value-context --seed 1
expect_error "cannot be combined" \
    --diagnose-value-context --benchmark

run_cli_input "q" --interactive --seed 1 \
    --train-games 1 --train-seed 424242
if [ "$cli_status" -ne 0 ]; then
    printf 'interactive quit failed\n%s\n' "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"Old School Magic Interactive"*\
"Match: Human Red vs Learned Value G0 RU Aggro"*\
"Game seed: 1"*\
"Training seed: 424242"*\
"Learned search worlds per legal action: 2"*\
"Board layout: 120 columns"*\
"HAND (DEBUG REVEAL)"*\
"|GRAVEYARD"*\
"#== YOU"*\
"YOUR HAND"*\
"Game abandoned."*) ;;
    *)
        printf 'interactive banner or opponent-hand reveal missing\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac
case $cli_output in
    *"(none)"*|*"|EMPTY"*|*"STACK | TOP FIRST"*)
        printf 'interactive empty zones were rendered noisily\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac
interactive_width=$(
    printf '%s\n' "$cli_output" |
        awk '{ if (length($0) > maximum) maximum = length($0) }
             END { print maximum }'
)
if [ "$interactive_width" -ne 120 ]; then
    printf 'interactive layout width was %s, expected 120\n%s\n' \
        "$interactive_width" "$cli_output" >&2
    exit 1
fi

interactive_tap_input=$(printf '1\n1\nq')
run_cli_input "$interactive_tap_input" --interactive --seed 1 \
    --train-games 1 --train-seed 424242
if [ "$cli_status" -ne 0 ]; then
    printf 'interactive tapped-card rendering failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"STACK | TOP FIRST"*"STACK #0 | TOP"*\
"<<< TAPPED >>>"*\
"|GRAVEYARD"*"|1 CARD"*\
"GRAVEYARD CARDS:"*"Lightning Bolt"*\
"Game abandoned."*) ;;
    *)
        printf 'interactive tapped card or graveyard tile missing\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac
interactive_stack_width=$(
    printf '%s\n' "$cli_output" |
        awk '{ if (length($0) > maximum) maximum = length($0) }
             END { print maximum }'
)
if [ "$interactive_stack_width" -ne 180 ]; then
    printf 'interactive stack width was %s, expected 180\n%s\n' \
        "$interactive_stack_width" "$cli_output" >&2
    exit 1
fi

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

run_cli_input "q" --interactive --seed 1 \
    --train-games 1 --train-seed 424242 \
    --learned-generations 0 --learned-rollouts 1 \
    --value-continuation-epsilon 0
if [ "$cli_status" -ne 0 ]; then
    printf 'interactive legacy-generation sentinel failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"Match: Human Red vs Learned Value G0 RU Aggro"*\
"Learned search worlds per legal action: 1"*\
"Training frozen Value G0"*\
"Game abandoned."*) ;;
    *)
        printf 'interactive generation zero did not preserve G0\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac
case $cli_output in
    *"Value Challenger"*)
        printf 'interactive generation zero selected challenger\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac
case $cli_output in
    *"Value continuation priority-action epsilon:"*)
        printf 'explicit zero epsilon changed interactive reporting\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac

run_cli_input "q" --interactive --seed 1 \
    --train-games 1 --train-seed 424242 \
    --learned-generations 1 --learned-rollouts 1 \
    --value-continuation-epsilon 1
if [ "$cli_status" -ne 0 ]; then
    printf 'interactive Value Challenger C1 failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"Match: Human Red vs Learned Value Challenger C1 RU Aggro"*\
"Learned search worlds per legal action: 1"*\
"Value continuation priority-action epsilon: 1 (root remains greedy)"*\
"Training frozen Value Challenger C1"*\
"Value Challenger C1 fingerprint:"*\
"Value Challenger C1 artifact cache: generated $challenger_c1_cache"*\
"Game abandoned."*) ;;
    *)
        printf 'interactive challenger route/reporting missing\n%s\n' \
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
"Red — 15 Mountain / 9 Lightning Bolt / 7 Ironclaw Orcs / 4 Gray Ogre / 3 Hill Giant / 2 Fire Elemental"*\
"Blue — 15 Island / 1 Mox Sapphire / 1 Sol Ring / 1 Ancestral Recall / 1 Time Walk / 1 Braingeyser / 4 Flying Men / 4 Force Spike / 8 Counterspell / 4 Air Elemental"*\
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
expect_error "require --score-probes" \
    --probe-corpus validation-v1
expect_error "--probe-corpus must be dev-v3 or validation-v1" \
    --score-probes --probe-corpus unknown
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
for invalid_challenger in \
    learned-value-c learned-value-c0 learned-value-c-1 \
    learned-value-c1x learned-value-c18446744073709551616
do
    expect_error "invalid bot name" \
        --benchmark --games 1 --challenger "$invalid_challenger" \
        --baseline random
done
for invalid_context_challenger in \
    learned-value-context-c learned-value-context-c0 \
    learned-value-context-c-1 learned-value-context-c1x \
    learned-value-context-c18446744073709551616
do
    expect_error "invalid bot name" \
        --benchmark --games 1 \
        --challenger "$invalid_context_challenger" \
        --baseline random
done
expect_error "invalid bot name" \
    --benchmark --games 1 --challenger learned-value-mix50-g7 \
    --baseline random
expect_error "--challenger requires --benchmark or --score-probes" \
    --games 1 --challenger learned-value-context-c1
expect_error "--baseline requires --benchmark" \
    --games 1 --baseline learned-value-context-c1
expect_error "requires learned-value-context-cN" \
    --score-probes --challenger learned-value-c1
expect_error "requires --learned-generations N" \
    --score-probes --challenger learned-value-context-c1
expect_error "generation must match --learned-generations N" \
    --score-probes --learned-generations 1 \
    --challenger learned-value-context-c2
expect_error "--learned-rollouts must be greater than zero" \
    --games 1 --learned-rollouts 0
expect_error "--score-probes requires --learned-rollouts of at least two" \
    --score-probes --learned-rollouts 1
learned_generation_scope="--learned-generations requires"
expect_error "$learned_generation_scope" \
    --games 1 --bots random --learned-generations 1
expect_error "$learned_generation_scope" \
    --benchmark --games 1 --challenger learned-value-g0 \
    --baseline random --learned-generations 1
expect_error "$learned_generation_scope" \
    --variance-study --games 1 --learned-generations 1
learned_rollout_scope="--learned-rollouts requires"
expect_error "$learned_rollout_scope" \
    --games 1 --bots random --learned-rollouts 1
value_epsilon_error="must be a finite number in [0, 1]"
for invalid_epsilon in -0.01 1.0001 nan inf 0.05x 0x1p-1
do
    expect_error "$value_epsilon_error" \
        --interactive \
        --value-continuation-epsilon "$invalid_epsilon"
done
value_epsilon_scope="--value-continuation-epsilon requires"
expect_error "$value_epsilon_scope" \
    --games 1 --bots random --value-continuation-epsilon 0.05
expect_error "$value_epsilon_scope" \
    --games 1 --bots learned-actor \
    --value-continuation-epsilon 0.05
expect_error "$value_epsilon_scope" \
    --benchmark --games 1 --challenger learned-actor-g0 \
    --baseline random --value-continuation-epsilon 0.05
expect_error "$value_epsilon_scope" \
    --benchmark --games 1 --challenger random \
    --baseline learned-value-g0 \
    --value-continuation-epsilon 0.05
expect_error "$value_epsilon_scope" \
    --variance-study --games 1 \
    --value-continuation-epsilon 0.05
expect_error "$value_epsilon_scope" \
    --evolve-deck --games 1 \
    --value-continuation-epsilon 0.05
expect_error "--evolve-pilot requires --evolve-deck" \
    --evolve-pilot handcrafted
expect_error "Learned --evolve-pilot currently requires learned-value-context-cN" \
    --evolve-deck --evolve-pilot learned-value-c1
expect_error "$learned_rollout_scope" \
    --evolve-deck --evolve-pilot handcrafted --learned-rollouts 1
expect_error "$value_epsilon_scope" \
    --diagnose-white-plan \
    --value-continuation-epsilon 0.05
expect_error "--refresh-value-challenger-cache requires" \
    --games 1 --refresh-value-challenger-cache
expect_error "--refresh-value-challenger-cache requires" \
    --benchmark --games 1 --challenger learned-value-g0 \
    --baseline random --refresh-value-challenger-cache
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

run_cli --evolve-deck --generations 1 --population 5 \
    --games 1 --seed 7
if [ "$cli_status" -ne 0 ]; then
    printf 'default deck-evolution pilot failed\n%s\n' "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"Old School Magic Deck Evolution"*\
"Pilot: Handcrafted Policy"*) ;;
    *)
        printf 'default deck-evolution pilot changed\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac

context_evolve_args="--evolve-deck --evolve-pilot \
learned-value-context-c1 --generations 1 --population 5 --games 1 \
--seed 7 --learned-rollouts 1 --train-games 1 --train-seed 424242"

# shellcheck disable=SC2086
run_cli $context_evolve_args
if [ "$cli_status" -ne 0 ]; then
    printf 'Learned context deck-evolution pilot failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
context_evolve_first_output=$cli_output
case $context_evolve_first_output in
    *"Training frozen Value Context C1"*\
"Value Context C1 fingerprint:"*\
"Value Context C1 artifact cache: generated $context_challenger_c1_cache"*\
"S1 decision roots: total="*\
"S1 roots by deck: Green="*\
"Pilot: Learned Value Context C1 (K=1, training seed 424242, 1 initial games)"*) ;;
    *)
        printf 'Learned context evolution generation/reporting missing\n%s\n' \
            "$context_evolve_first_output" >&2
        exit 1
        ;;
esac
context_evolve_first_fingerprint=$(
    printf '%s\n' "$context_evolve_first_output" |
        sed -n 's/^  Value Context C1 fingerprint: //p'
)
context_evolve_first_report=$(
    printf '%s\n' "$context_evolve_first_output" |
        sed -n '/^Old School Magic Deck Evolution/,$p'
)

# shellcheck disable=SC2086
run_cli $context_evolve_args
if [ "$cli_status" -ne 0 ]; then
    printf 'repeated Learned context deck evolution failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"Loading immutable Value Context C1 artifact"*\
"Value Context C1 artifact cache: loaded $context_challenger_c1_cache"*\
"S1 decision roots: total="*\
"Pilot: Learned Value Context C1 (K=1, training seed 424242, 1 initial games)"*) ;;
    *)
        printf 'Learned context evolution did not reuse its artifact\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac
context_evolve_second_fingerprint=$(
    printf '%s\n' "$cli_output" |
        sed -n 's/^  Value Context C1 fingerprint: //p'
)
context_evolve_second_report=$(
    printf '%s\n' "$cli_output" |
        sed -n '/^Old School Magic Deck Evolution/,$p'
)
if [ -z "$context_evolve_first_fingerprint" ] ||
    [ "$context_evolve_first_fingerprint" != \
      "$context_evolve_second_fingerprint" ] ||
    [ -z "$context_evolve_first_report" ] ||
    [ "$context_evolve_first_report" != \
      "$context_evolve_second_report" ]; then
    printf 'Learned context evolution was not fixed-seed deterministic\n' >&2
    printf 'fingerprints: %s / %s\n' \
        "$context_evolve_first_fingerprint" \
        "$context_evolve_second_fingerprint" >&2
    exit 1
fi

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
    *"Challenger: Learned Value G0"*\
"Challenger frozen model: Learned Value G0, seed 424242, 1 training games, K=2"*\
"Challenger: Learned Value G0"*\
"Challenger frozen model: Learned Value G0, seed 424242, 1 training games, K=2"*) ;;
    *)
        printf 'legacy Value aliases changed routing\n%s\n%s\n' \
            "$learned_alias_output" "$learned_value_alias_output" >&2
        exit 1
        ;;
esac
case "$learned_alias_output
$learned_value_alias_output" in
    *"immutable Value G8"*|*"Value G8 artifact cache:"*|\
*"Value Challenger"*)
        printf 'legacy Value aliases unexpectedly used another family\n%s\n%s\n' \
            "$learned_alias_output" "$learned_value_alias_output" >&2
        exit 1
        ;;
esac

run_cli --benchmark --games 1 --seed 1 --train-games 1 \
    --train-seed 424242 --challenger learned-value-g0 \
    --baseline learned-value-g0 --learned-rollouts 1 \
    --value-continuation-epsilon 0.05
if [ "$cli_status" -eq 2 ]; then
    printf 'continuation-epsilon causal benchmark failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"Challenger: Learned Value G0 (continuation epsilon=0.05)"*\
"Baseline: Learned Value G0"*\
"Challenger frozen model: Learned Value G0 (continuation epsilon=0.05), seed 424242, 1 training games, K=1"*\
"Baseline frozen model: Learned Value G0, seed 424242, 1 training games, K=1"*) ;;
    *)
        printf 'continuation-epsilon benchmark reporting missing\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac
case $cli_output in
    *"Baseline: Learned Value G0 (continuation epsilon="*)
        printf 'benchmark epsilon leaked from challenger to baseline\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac

challenger_args="--benchmark --games 1 --seed 1 --train-games 1 \
--train-seed 424242 --challenger learned-value-c1 \
--baseline learned-value-g0 --learned-rollouts 1"

# shellcheck disable=SC2086
run_cli $challenger_args
if [ "$cli_status" -eq 2 ]; then
    printf 'Value Challenger C1 benchmark route failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
challenger_first_output=$cli_output
case $challenger_first_output in
    *"Loading immutable Value Challenger C1 artifact"*\
"Value Challenger C1 fingerprint:"*\
"Value Challenger C1 artifact cache: loaded $challenger_c1_cache"*\
"Challenger: Learned Value Challenger C1"*\
"Baseline: Learned Value G0"*\
"Challenger frozen model: Learned Value Challenger C1, seed 424242, 1 training games, K=1"*\
"Baseline frozen model: Learned Value G0, seed 424242, 1 training games, K=1"*) ;;
    *)
        printf 'Value Challenger C1 routing/reporting missing\n%s\n' \
            "$challenger_first_output" >&2
        exit 1
        ;;
esac
challenger_first_fingerprint=$(
    printf '%s\n' "$challenger_first_output" |
        sed -n 's/^  Value Challenger C1 fingerprint: //p'
)
challenger_first_record=$(
    printf '%s\n' "$challenger_first_output" |
        sed -n 's/^  Challenger record: //p'
)

# shellcheck disable=SC2086
run_cli $challenger_args
if [ "$cli_status" -eq 2 ]; then
    printf 'repeated Value Challenger C1 benchmark failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
challenger_second_fingerprint=$(
    printf '%s\n' "$cli_output" |
        sed -n 's/^  Value Challenger C1 fingerprint: //p'
)
challenger_second_record=$(
    printf '%s\n' "$cli_output" |
        sed -n 's/^  Challenger record: //p'
)
case $cli_output in
    *"Loading immutable Value Challenger C1 artifact"*\
"Value Challenger C1 artifact cache: loaded $challenger_c1_cache"*) ;;
    *)
        printf 'repeated challenger did not reuse its artifact\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac
if [ -z "$challenger_first_fingerprint" ] ||
    [ "$challenger_first_fingerprint" != \
      "$challenger_second_fingerprint" ] ||
    [ -z "$challenger_first_record" ] ||
    [ "$challenger_first_record" != "$challenger_second_record" ]; then
    printf 'Value Challenger C1 was not fixed-seed deterministic\n' >&2
    printf 'fingerprints: %s / %s\nrecords: %s / %s\n' \
        "$challenger_first_fingerprint" \
        "$challenger_second_fingerprint" \
        "$challenger_first_record" "$challenger_second_record" >&2
    exit 1
fi

printf 'corrupt' >>"$challenger_c1_cache"
# shellcheck disable=SC2086
expect_error "--refresh-value-challenger-cache" $challenger_args
case $cli_output in
    *"Training frozen Value Challenger C1"*)
        printf 'corrupt challenger cache silently retrained\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac

# shellcheck disable=SC2086
run_cli $challenger_args --refresh-value-challenger-cache
if [ "$cli_status" -eq 2 ]; then
    printf 'Value Challenger C1 cache refresh failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"Training frozen Value Challenger C1"*\
"Value Challenger C1 artifact cache: generated $challenger_c1_cache"*) ;;
    *)
        printf 'challenger refresh did not regenerate atomically\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac
refreshed_challenger_fingerprint=$(
    printf '%s\n' "$cli_output" |
        sed -n 's/^  Value Challenger C1 fingerprint: //p'
)
if [ "$refreshed_challenger_fingerprint" != \
     "$challenger_first_fingerprint" ]; then
    printf 'refreshed challenger fingerprint changed\n' >&2
    exit 1
fi

run_cli --benchmark --games 1 --seed 1 --train-games 1 \
    --train-seed 424242 --challenger learned-value-c1 \
    --baseline learned-value-c2 --learned-rollouts 1
if [ "$cli_status" -eq 2 ]; then
    printf 'distinct challenger-generation benchmark failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"Loading immutable Value Challenger C1 artifact"*\
"Value Challenger C1 artifact cache: loaded $challenger_c1_cache"*\
"Training frozen Value Challenger C2"*\
"Value Challenger C2 artifact cache: generated $challenger_c2_cache"*\
"Challenger: Learned Value Challenger C1"*\
"Baseline: Learned Value Challenger C2"*) ;;
    *)
        printf 'challenger generation keys were conflated\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac
c1_fingerprint=$(
    printf '%s\n' "$cli_output" |
        sed -n 's/^  Value Challenger C1 fingerprint: //p'
)
c2_fingerprint=$(
    printf '%s\n' "$cli_output" |
        sed -n 's/^  Value Challenger C2 fingerprint: //p'
)
if [ -z "$c1_fingerprint" ] || [ -z "$c2_fingerprint" ] ||
    [ "$c1_fingerprint" = "$c2_fingerprint" ]; then
    printf 'challenger C1/C2 fingerprints were not distinct\n%s\n' \
        "$cli_output" >&2
    exit 1
fi

run_cli --games 1 --seed 1 --bots learned-value \
    --train-games 1 --train-seed 424242 \
    --learned-generations 1 --learned-rollouts 1
if [ "$cli_status" -ne 0 ]; then
    printf 'learned-only challenger simulation failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"Loading immutable Value Challenger C1 artifact"*\
"Value Challenger C1 artifact cache: loaded $challenger_c1_cache"*\
"Bot field: Learned Value Challenger C1 only"*\
"Frozen learned model: Learned Value Challenger C1, seed 424242, 1 training games, K=1"*\
"Learned Value Challenger C1"*) ;;
    *)
        printf 'learned-only challenger route/reporting missing\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac

run_cli --games 1 --seed 1 --bots mixed \
    --train-games 1 --train-seed 424242 \
    --rollouts 1 --deep-rollouts 2 \
    --learned-generations 1 --learned-rollouts 1
if [ "$cli_status" -ne 0 ]; then
    printf 'mixed-field challenger cache reuse failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"Loading immutable Value Challenger C1 artifact"*\
"Value Challenger C1 artifact cache: loaded $challenger_c1_cache"*\
"Bot field: mixed Random, Monte Carlo, Deep Monte Carlo, Handcrafted Policy, and Learned Value Challenger C1"*) ;;
    *)
        printf 'mixed-field route did not reuse challenger cache\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac

run_cli --stability --stability-runs 1 --games 1 --seed 1 \
    --train-games 1 --train-seed 424242 \
    --rollouts 1 --deep-rollouts 2 \
    --learned-generations 1 --learned-rollouts 1
if [ "$cli_status" -eq 2 ]; then
    printf 'stability challenger cache reuse failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"Learned Value Challenger All-Policy Stability Panel"*\
"Loading immutable Value Challenger C1 artifact"*\
"Value Challenger C1 artifact cache: loaded $challenger_c1_cache"*) ;;
    *)
        printf 'stability route did not reuse challenger cache\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac

run_cli --games 1 --seed 1 --bots learned-actor \
    --train-games 1 --train-seed 424242 \
    --learned-rollouts 1
if [ "$cli_status" -ne 0 ]; then
    printf 'learned-actor rollout selection failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"Bot field: Learned Unified Actor only"*\
"Frozen learned model: Learned Actor, seed 424242, 1 training games, K=1"*) ;;
    *)
        printf 'learned-actor rollout routing/reporting missing\n%s\n' \
            "$cli_output" >&2
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
validation_probe_cache=$probe_directory/validation-cache.tsv

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
    --probe-cache "$probe_cache" --learned-generations 1
if [ "$cli_status" -ne 0 ]; then
    printf 'legacy Value G0 probe smoke failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"Cache: loaded"*\
"6 policy views"*\
"Value G0 deployed policy"*\
"Value Challenger C1"*) ;;
    *)
        printf 'legacy G0 reference/challenger probe output changed\n%s\n' \
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

run_cli --score-probes --probe-corpus validation-v1 \
    --probe-worlds 2 --probe-horizon 0 \
    --learned-rollouts 3 \
    --train-games 1 --train-seed 424242 \
    --probe-cache "$validation_probe_cache" --refresh-probe-cache
if [ "$cli_status" -ne 0 ]; then
    printf 'harvested validation-v1 probe smoke failed\n%s\n' \
        "$cli_output" >&2
    exit 1
fi
case $cli_output in
    *"Probe Validation-v1 Offline Score"*\
*"cannot be used for policy promotion"*\
*"validation.ru.disintegrate-hold-x0.v1"*\
*"Focused cached Actor-reference candidate pairs"*\
*"Actor reference Q(Pass) - Q(X=0)"*\
*"Focused Value-policy candidate pairs"*\
*"Value G0 Q(Pass) - Q(X=0)"*\
*"paired SE"*\
*"95% CI"*\
*"K=3"*) ;;
    *)
        printf 'validation-v1 behavioral pair report missing\n%s\n' \
            "$cli_output" >&2
        exit 1
        ;;
esac

printf 'CLI tests passed\n'
