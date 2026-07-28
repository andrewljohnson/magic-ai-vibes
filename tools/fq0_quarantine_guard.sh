#!/bin/zsh

# Fail-closed memory guard for the one-shot FQ0 quarantine audit.
# The limits are intentionally fixed: changing them requires a new declaration.

set -u
unsetopt BG_NICE

readonly PHYS_LIMIT_BYTES=$((20 * 1024 * 1024 * 1024))
readonly RSS_LIMIT_BYTES=$((16 * 1024 * 1024 * 1024))
readonly PUBLICATION_DELTA_LIMIT_BYTES=$((1024 * 1024 * 1024))
readonly SAMPLE_SECONDS=2
readonly MAX_BLIND_SAMPLES=1
readonly MAX_STARTUP_BLIND_SAMPLES=3
readonly STARTUP_RETRY_SECONDS=0.25
readonly MARKER='FQ0 publication: report validation complete; streaming evidence'

normalize_peak() {
    local current=$1
    local reported_peak=$2
    local page_bytes=$3
    (( current >= 0 && reported_peak >= 0 && page_bytes > 0 )) || return 1
    if (( reported_peak >= current )); then
        print -r -- "$reported_peak"
        return 0
    fi
    (( current - reported_peak <= page_bytes )) || return 1
    print -r -- "$current"
}

physical_limit_reached() {
    local current=$1
    local effective_peak=$2
    (( current >= PHYS_LIMIT_BYTES || effective_peak >= PHYS_LIMIT_BYTES ))
}

physical_limit_status() {
    if physical_limit_reached "$1" "$2"; then
        print -r -- 90
    else
        print -r -- 0
    fi
}

publication_delta_value() {
    local baseline_current=$1
    local baseline_peak=$2
    local later_current=$3
    local later_peak=$4
    local delta_current=$((later_current - baseline_current))
    local delta_peak=$((later_peak - baseline_peak))
    (( delta_current < 0 )) && delta_current=0
    (( delta_peak < 0 )) && delta_peak=0
    if (( delta_peak > delta_current )); then
        print -r -- "$delta_peak"
    else
        print -r -- "$delta_current"
    fi
}

publication_delta_status() {
    local delta
    delta=$(publication_delta_value "$1" "$2" "$3" "$4") || return 1
    if (( delta > PUBLICATION_DELTA_LIMIT_BYTES )); then
        print -r -- 94
    else
        print -r -- 0
    fi
}

guard_self_test() {
    local failures=0
    local actual
    local gib=$((1024 * 1024 * 1024))
    local row_header=5658325184
    local row_current=5591003392
    local row_peak=5590987008
    local page=16384
    local parsed_current
    local parsed_peak
    local baseline_effective
    local later_effective
    local fixture_dir
    local fixture_output
    local fixture_error

    actual=$(normalize_peak 100 120 "$page") || failures=$((failures + 1))
    [[ $actual == 120 ]] || failures=$((failures + 1))
    actual=$(normalize_peak 100 100 "$page") || failures=$((failures + 1))
    [[ $actual == 100 ]] || failures=$((failures + 1))
    actual=$(normalize_peak "$row_current" "$row_peak" "$page") ||
        failures=$((failures + 1))
    [[ $actual == "$row_current" ]] || failures=$((failures + 1))
    if normalize_peak "$row_current" $((row_current - 2 * page)) \
        "$page" >/dev/null; then
        failures=$((failures + 1))
    fi

    actual=$(normalize_peak "$PHYS_LIMIT_BYTES" \
        $((PHYS_LIMIT_BYTES - page)) "$page") ||
        failures=$((failures + 1))
    [[ $(physical_limit_status "$PHYS_LIMIT_BYTES" "$actual") == 90 ]] ||
        failures=$((failures + 1))

    baseline_effective=$(
        normalize_peak $((4 * gib)) $((4 * gib - page)) "$page"
    ) || failures=$((failures + 1))
    later_effective=$(
        normalize_peak $((5 * gib)) $((5 * gib - page)) "$page"
    ) || failures=$((failures + 1))
    actual=$(
        publication_delta_value \
            100 "$baseline_effective" 100 "$later_effective"
    )
    [[ $actual == "$gib" ]] || failures=$((failures + 1))
    [[ $(
        publication_delta_status \
            100 "$baseline_effective" 100 "$later_effective"
    ) == 0 ]] ||
        failures=$((failures + 1))
    later_effective=$(
        normalize_peak \
            $((5 * gib + 1)) \
            $((5 * gib + 1 - page)) \
            "$page"
    ) || failures=$((failures + 1))
    actual=$(
        publication_delta_value \
            100 "$baseline_effective" 100 "$later_effective"
    )
    [[ $actual == $((gib + 1)) ]] || failures=$((failures + 1))
    [[ $(
        publication_delta_status \
            100 "$baseline_effective" 100 "$later_effective"
    ) == 94 ]] ||
        failures=$((failures + 1))

    fixture_dir=$(mktemp -d /private/tmp/fq0-guard-selftest.XXXXXX) ||
        return 1
    fixture_output="$fixture_dir/footprint.out"
    fixture_error="$fixture_dir/footprint.err"
    {
        print -r -- \
            "old-school-fq0-quarantine-a5-bounded-publication-20260727 [70092]: 64-bit    Footprint: $row_header B ($page bytes per page)"
        print -r -- "    phys_footprint: $row_current B"
        print -r -- "    phys_footprint_peak: $row_peak B"
    } >"$fixture_output"
    : >"$fixture_error"
    footprint_output_clean "$fixture_output" "$fixture_error" ||
        failures=$((failures + 1))
    parsed_current=$(extract_metric 'phys_footprint:' "$fixture_output") ||
        failures=$((failures + 1))
    [[ $parsed_current == "$row_current" ]] ||
        failures=$((failures + 1))
    parsed_peak=$(extract_metric 'phys_footprint_peak:' "$fixture_output") ||
        failures=$((failures + 1))
    [[ $parsed_peak == "$row_peak" ]] ||
        failures=$((failures + 1))
    actual=$(extract_page_size "$fixture_output") ||
        failures=$((failures + 1))
    [[ $actual == "$page" ]] || failures=$((failures + 1))
    actual=$(normalize_peak "$parsed_current" "$parsed_peak" "$actual") ||
        failures=$((failures + 1))
    [[ $actual == "$row_current" ]] || failures=$((failures + 1))

    {
        print -r -- \
            "old-school-fq0-quarantine-a5-bounded-publication-20260727 [70092]: 64-bit    Footprint: $row_header B ($page bytes per page)"
        print -r -- "    phys_footprint: $row_current B"
        print -r -- \
            "    phys_footprint_peak: $((row_current - 2 * page)) B"
    } >"$fixture_output"
    parsed_current=$(extract_metric 'phys_footprint:' "$fixture_output") ||
        failures=$((failures + 1))
    parsed_peak=$(extract_metric 'phys_footprint_peak:' "$fixture_output") ||
        failures=$((failures + 1))
    actual=$(extract_page_size "$fixture_output") ||
        failures=$((failures + 1))
    if normalize_peak "$parsed_current" "$parsed_peak" "$actual" >/dev/null; then
        failures=$((failures + 1))
    fi

    {
        print -r -- \
            "test [1]: 64-bit    Footprint: $row_current B ($page bytes per page)"
        print -r -- "    phys_footprint_peak: $row_peak B"
    } >"$fixture_output"
    if extract_metric 'phys_footprint:' "$fixture_output" >/dev/null; then
        failures=$((failures + 1))
    fi

    {
        print -r -- \
            "test [1]: 64-bit    Footprint: $row_current B ($page bytes per page)"
        print -r -- "    phys_footprint: $row_current B"
        print -r -- "    phys_footprint: $row_current B"
        print -r -- "    phys_footprint_peak: $row_peak B"
    } >"$fixture_output"
    if extract_metric 'phys_footprint:' "$fixture_output" >/dev/null; then
        failures=$((failures + 1))
    fi

    {
        print -r -- \
            "test [1]: 64-bit    Footprint: $row_current B ($page bytes per page)"
        print -r -- \
            "test [1]: 64-bit    Footprint: $row_current B ($page bytes per page)"
        print -r -- "    phys_footprint: $row_current B"
        print -r -- "    phys_footprint_peak: $row_peak B"
    } >"$fixture_output"
    if extract_page_size "$fixture_output" >/dev/null; then
        failures=$((failures + 1))
    fi

    print -r -- 'Warnings were encountered while examining the process.' \
        >"$fixture_output"
    if footprint_output_clean "$fixture_output" "$fixture_error"; then
        failures=$((failures + 1))
    fi
    print -r -- 'unexpected stderr' >"$fixture_error"
    : >"$fixture_output"
    if footprint_output_clean "$fixture_output" "$fixture_error"; then
        failures=$((failures + 1))
    fi
    command rm -f "$fixture_output" "$fixture_error"
    rmdir "$fixture_dir"

    if (( failures != 0 )); then
        print -u2 -- "FQ0 quarantine guard self-test failures: $failures"
        return 1
    fi
    print -r -- 'FQ0 quarantine guard self-tests: 14/14 cases passed'
}

usage() {
    print -u2 \
        'Usage: tools/fq0_quarantine_guard.sh LOG_DIR -- ABSOLUTE_COMMAND [ARG ...]'
    exit 95
}

event() {
    print -r -- "$(date -u '+%Y-%m-%dT%H:%M:%SZ')	$*" >>"$events"
}

extract_metric() {
    local label=$1
    local input_path=$2
    awk -v label="$label" '
        $1 == label && NF == 3 && $3 == "B" &&
            $2 ~ /^[0-9]+$/ {
            count += 1
            value = $2
        }
        END {
            if (count != 1) {
                exit 1
            }
            print value
        }
    ' "$input_path"
}

extract_page_size() {
    local input_path=$1
    sed -nE 's/.*\(([0-9]+) bytes per page\).*/\1/p' "$input_path" |
        awk '
            /^[0-9]+$/ {
                count += 1
                value = $1
            }
            END {
                if (count != 1 || value <= 0) {
                    exit 1
                }
                print value
            }
        '
}

footprint_output_clean() {
    local output_path=$1
    local error_path=$2
    [[ ! -s $error_path ]] || return 1
    ! grep -Eiq \
        'warnings were encountered|errors were encountered|results may be incomplete|bailing out|unable to' \
        "$output_path" "$error_path"
}

sample_footprint() {
    /usr/bin/perl -e 'alarm shift; exec @ARGV' 6 \
        /usr/bin/footprint -p "$1" -f bytes --noCategories \
        --swapped --wired >"$footprint_current" 2>"$footprint_error"
    local sample_status=$?
    {
        print -r -- \
            "=== $(date -u '+%Y-%m-%dT%H:%M:%SZ') rc=$sample_status ==="
        command cat "$footprint_current"
        command cat "$footprint_error"
    } >>"$footprint_raw"
    (( sample_status == 0 )) || return 1
    footprint_output_clean "$footprint_current" "$footprint_error"
}

sample_rss() {
    /bin/ps -o rss= -p "$1" >"$ps_current" 2>"$ps_error"
    local sample_status=$?
    {
        print -r -- \
            "=== $(date -u '+%Y-%m-%dT%H:%M:%SZ') rc=$sample_status ==="
        command cat "$ps_current"
        command cat "$ps_error"
    } >>"$ps_raw"
    (( sample_status == 0 )) || return 1
    [[ ! -s $ps_error ]] || return 1
    local value
    value=$(awk '
        NF == 1 && $1 ~ /^[0-9]+$/ {
            count += 1
            value = $1
        }
        END {
            if (count != 1) {
                exit 1
            }
            print value
        }
    ' "$ps_current") || return 1
    print -r -- $((value * 1024))
}

hash_file() {
    local digest
    digest=$(shasum -a 256 "$1" 2>/dev/null | awk '
        NF == 2 && $1 ~ /^[0-9a-f]+$/ {
            count += 1
            value = $1
        }
        END {
            if (count != 1 || length(value) != 64) {
                exit 1
            }
            print value
        }
    ') || return 1
    [[ $digest != *[^0-9a-f]* ]] || return 1
    print -r -- "$digest"
}

if [[ ${1-} == '--self-test' ]]; then
    (( $# == 1 )) || exit 95
    guard_self_test
    exit $?
fi

(( $# >= 3 )) || usage
log_dir=$1
shift
[[ $1 == '--' ]] || usage
shift
command=("$@")
target=${command[1]}
[[ $target == /* && -f $target && -x $target ]] || usage

mkdir -m 700 "$log_dir" 2>/dev/null || {
    print -u2 "Refusing to reuse or create log directory: $log_dir"
    exit 95
}

events="$log_dir/events.log"
samples="$log_dir/samples.tsv"
stdout_log="$log_dir/target.stdout"
stderr_log="$log_dir/target.stderr"
footprint_current="$log_dir/footprint.current"
footprint_error="$log_dir/footprint.current.stderr"
footprint_raw="$log_dir/footprint.raw.log"
ps_current="$log_dir/ps.current"
ps_error="$log_dir/ps.current.stderr"
ps_raw="$log_dir/ps.raw.log"
page_size_file="$log_dir/page-size.txt"
page_size_error="$log_dir/page-size.stderr"

# Prove the page-size source and both samplers work before the target can start.
host_page_bytes=$(/usr/bin/getconf PAGESIZE 2>"$page_size_error") ||
    {
        event 'sampler preflight failed: host page size'
        exit 95
    }
[[ ! -s $page_size_error &&
    $host_page_bytes == <-> &&
    host_page_bytes -gt 0 ]] ||
    {
        event 'sampler preflight failed: invalid host page size'
        exit 95
    }
print -r -- "$host_page_bytes" >"$page_size_file"

sample_footprint $$ ||
    {
        event 'sampler preflight failed: footprint'
        exit 95
    }
preflight_current=$(extract_metric 'phys_footprint:' "$footprint_current") ||
    {
        event 'sampler preflight failed: current footprint parse'
        exit 95
    }
preflight_peak=$(extract_metric 'phys_footprint_peak:' "$footprint_current") ||
    {
        event 'sampler preflight failed: peak footprint parse'
        exit 95
    }
preflight_page_bytes=$(extract_page_size "$footprint_current") ||
    {
        event 'sampler preflight failed: footprint page-size parse'
        exit 95
    }
(( preflight_page_bytes == host_page_bytes )) ||
    {
        event 'sampler preflight failed: footprint/host page-size mismatch'
        exit 95
    }
preflight_effective_peak=$(
    normalize_peak "$preflight_current" "$preflight_peak" "$host_page_bytes"
) ||
    {
        event 'sampler preflight failed: peak/current skew exceeds one page'
        exit 95
    }
sample_rss $$ >/dev/null ||
    {
        event 'sampler preflight failed: RSS'
        exit 95
    }
event 'sampler preflight passed; target not yet launched'

target_hash_before=$(hash_file "$target") ||
    {
        event 'target hash failed before launch'
        exit 95
    }
print -r -- "${(q+)command[@]}" >"$log_dir/command.shell-quoted"

guard_status=0
termination_reason=''
target_pid=0
blind_samples=0
sample_index=0
observed_current_peak=0
observed_reported_peak=0
observed_effective_peak=0
observed_rss_peak=0
peak_normalizations=0
max_peak_skew=0
marker_seen=0
marker_baseline_current=0
marker_baseline_peak=0
last_sample_index=0
last_current=0
last_peak=0
post_marker_samples=0
post_marker_current_peak=0
post_marker_reported_peak=0

terminate_target() {
    guard_status=$1
    termination_reason=$2
    event "HARD TERMINATION: $termination_reason"
    if (( target_pid > 0 )); then
        kill -KILL "$target_pid" 2>/dev/null || true
    fi
}

count_markers() {
    {
        grep -F -c "$MARKER" "$stdout_log" 2>/dev/null || true
        grep -F -c "$MARKER" "$stderr_log" 2>/dev/null || true
    } | awk '{total += $1} END {print total + 0}'
}

scan_marker() {
    local marker_count
    marker_count=$(count_markers)
    if (( marker_count > 1 )); then
        terminate_target 93 "publication marker appeared $marker_count times"
        return
    fi
    if (( marker_count == 1 && ! marker_seen )); then
        if (( last_sample_index == 0 )); then
            terminate_target 93 \
                'publication marker appeared before a complete prior sample'
            return
        fi
        marker_seen=1
        marker_baseline_current=$last_current
        marker_baseline_peak=$last_peak
        event \
            "publication marker detected baseline_sample=$last_sample_index baseline_current=$last_current baseline_peak=$last_peak"
    fi
}

handle_interrupt() {
    if (( target_pid > 0 )); then
        terminate_target 96 'guard interrupted'
        wait "$target_pid"
    fi
    exit 96
}

trap handle_interrupt INT TERM HUP

"${command[@]}" >"$stdout_log" 2>"$stderr_log" &
target_pid=$!
print -r -- "$target_pid" >"$log_dir/target.pid"
event "target launched directly pid=$target_pid sha256=$target_hash_before"

print -r -- \
    'sample	utc	phys_current_bytes	phys_reported_peak_bytes	phys_effective_peak_bytes	peak_skew_bytes	rss_bytes	marker_seen	blind' \
    >"$samples"

while kill -0 "$target_pid" 2>/dev/null; do
    sample_index=$((sample_index + 1))
    current=''
    peak=''
    effective_peak=''
    peak_skew=''
    page_bytes=''
    rss=''
    current_ok=0
    peak_ok=0
    effective_peak_ok=0
    page_ok=0
    rss_ok=0
    scan_marker
    (( guard_status == 0 )) || break
    marker_known_at_sample_start=$marker_seen

    rss=$(sample_rss "$target_pid") && rss_ok=1
    if (( rss_ok )); then
        (( rss > observed_rss_peak )) &&
            observed_rss_peak=$rss
        if (( rss >= RSS_LIMIT_BYTES )); then
            terminate_target 91 "RSS $rss reached $RSS_LIMIT_BYTES"
        fi
    fi
    if (( guard_status != 0 )); then
        print -r -- \
            "$sample_index	$(date -u '+%Y-%m-%dT%H:%M:%SZ')					$rss	$marker_seen	$blind_samples" \
            >>"$samples"
        break
    fi
    if sample_footprint "$target_pid"; then
        current=$(extract_metric 'phys_footprint:' "$footprint_current") &&
            current_ok=1
        peak=$(extract_metric 'phys_footprint_peak:' "$footprint_current") &&
            peak_ok=1
        page_bytes=$(extract_page_size "$footprint_current") &&
            page_ok=1
        if (( page_ok && page_bytes != host_page_bytes )); then
            page_ok=0
        fi
    fi
    if (( current_ok && peak_ok && page_ok )); then
        effective_peak=$(
            normalize_peak "$current" "$peak" "$host_page_bytes"
        ) && effective_peak_ok=1
        if (( effective_peak_ok )); then
            peak_skew=$((current - peak))
            (( peak_skew < 0 )) && peak_skew=0
            if (( peak_skew > 0 )); then
                peak_normalizations=$((peak_normalizations + 1))
                (( peak_skew > max_peak_skew )) &&
                    max_peak_skew=$peak_skew
                event \
                    "normalized one-page peak skew sample=$sample_index current=$current reported_peak=$peak effective_peak=$effective_peak skew=$peak_skew page=$host_page_bytes"
            fi
        fi
    fi

    if (( current_ok )); then
        (( current > observed_current_peak )) &&
            observed_current_peak=$current
        if (( $(physical_limit_status "$current" 0) == 90 )); then
            terminate_target 90 \
                "physical footprint current=$current reached $PHYS_LIMIT_BYTES"
        fi
    fi
    if (( peak_ok )); then
        (( peak > observed_reported_peak )) &&
            observed_reported_peak=$peak
        if (( $(physical_limit_status 0 "$peak") == 90 &&
            guard_status == 0 )); then
            terminate_target 90 \
                "physical footprint peak=$peak reached $PHYS_LIMIT_BYTES"
        fi
    fi
    if (( effective_peak_ok )); then
        (( effective_peak > observed_effective_peak )) &&
            observed_effective_peak=$effective_peak
        if (( $(physical_limit_status 0 "$effective_peak") == 90 &&
            guard_status == 0 )); then
            terminate_target 90 \
                "effective physical footprint peak=$effective_peak reached $PHYS_LIMIT_BYTES"
        fi
    fi
    if (( guard_status != 0 )); then
        print -r -- \
            "$sample_index	$(date -u '+%Y-%m-%dT%H:%M:%SZ')	$current	$peak	$effective_peak	$peak_skew	$rss	$marker_seen	$blind_samples" \
            >>"$samples"
        break
    fi

    scan_marker
    (( guard_status == 0 )) || break
    sample_ok=$((
        current_ok &&
        peak_ok &&
        effective_peak_ok &&
        page_ok &&
        rss_ok
    ))
    if (( ! sample_ok )); then
        if ! kill -0 "$target_pid" 2>/dev/null; then
            print -r -- \
                "$sample_index	$(date -u '+%Y-%m-%dT%H:%M:%SZ')	$current	$peak	$effective_peak	$peak_skew	$rss	$marker_seen	$blind_samples" \
                >>"$samples"
            break
        fi
        blind_samples=$((blind_samples + 1))
        blind_limit=$MAX_BLIND_SAMPLES
        if (( last_sample_index == 0 )); then
            blind_limit=$MAX_STARTUP_BLIND_SAMPLES
        fi
        print -r -- \
            "$sample_index	$(date -u '+%Y-%m-%dT%H:%M:%SZ')	$current	$peak	$effective_peak	$peak_skew	$rss	$marker_seen	$blind_samples" \
            >>"$samples"
        if (( blind_samples >= blind_limit )); then
            terminate_target 92 \
                "monitoring blind for $blind_samples consecutive samples"
            break
        fi
        if (( last_sample_index == 0 )); then
            sleep "$STARTUP_RETRY_SECONDS"
        fi
        continue
    fi

    blind_samples=0
    if (( marker_known_at_sample_start )); then
        post_marker_samples=$((post_marker_samples + 1))
        (( current > post_marker_current_peak )) &&
            post_marker_current_peak=$current
        (( effective_peak > post_marker_reported_peak )) &&
            post_marker_reported_peak=$effective_peak
    fi

    print -r -- \
        "$sample_index	$(date -u '+%Y-%m-%dT%H:%M:%SZ')	$current	$peak	$effective_peak	$peak_skew	$rss	$marker_seen	0" \
        >>"$samples"

    if (( marker_seen && post_marker_samples > 0 )); then
        publication_delta=$(
            publication_delta_value \
                "$marker_baseline_current" \
                "$marker_baseline_peak" \
                "$post_marker_current_peak" \
                "$post_marker_reported_peak"
        )
        publication_status=$(
            publication_delta_status \
                "$marker_baseline_current" \
                "$marker_baseline_peak" \
                "$post_marker_current_peak" \
                "$post_marker_reported_peak"
        )
        if (( publication_status == 94 )); then
            terminate_target 94 \
                "publication delta $publication_delta exceeded $PUBLICATION_DELTA_LIMIT_BYTES"
            break
        fi
    fi
    last_sample_index=$sample_index
    last_current=$current
    last_peak=$effective_peak
    sleep "$SAMPLE_SECONDS"
done

wait "$target_pid"
target_status=$?
final_marker_count=$(count_markers)
if (( guard_status == 0 )); then
    scan_marker
fi
target_hash_after=''
if ! target_hash_after=$(hash_file "$target"); then
    if (( guard_status == 0 )); then
        guard_status=93
        termination_reason='target hash failed after run'
    fi
fi

if (( guard_status == 0 )); then
    if (( final_marker_count != 1 ||
        marker_seen != 1 ||
        post_marker_samples == 0 )); then
        guard_status=93
        termination_reason='publication marker was not sampled exactly once on both sides'
    elif (( target_status != 0 && target_status != 1 )); then
        guard_status=93
        termination_reason="target exit $target_status was not 0 or 1"
    elif [[ $target_hash_after != $target_hash_before ]]; then
        guard_status=93
        termination_reason='target executable changed during run'
    fi
fi

publication_delta=$(
    publication_delta_value \
        "$marker_baseline_current" \
        "$marker_baseline_peak" \
        "$post_marker_current_peak" \
        "$post_marker_reported_peak"
)

{
    print -r -- "guard_status=$guard_status"
    print -r -- "target_status=$target_status"
    print -r -- "termination_reason=$termination_reason"
    print -r -- "target_pid=$target_pid"
    print -r -- "target_sha256_before=$target_hash_before"
    print -r -- "target_sha256_after=$target_hash_after"
    print -r -- "samples=$sample_index"
    print -r -- "host_page_bytes=$host_page_bytes"
    print -r -- "observed_phys_current_peak_bytes=$observed_current_peak"
    print -r -- "observed_reported_phys_peak_bytes=$observed_reported_peak"
    print -r -- "observed_effective_phys_peak_bytes=$observed_effective_peak"
    print -r -- "observed_rss_peak_bytes=$observed_rss_peak"
    print -r -- "peak_normalizations=$peak_normalizations"
    print -r -- "max_peak_skew_bytes=$max_peak_skew"
    print -r -- "marker_seen=$marker_seen"
    print -r -- "marker_occurrences=$final_marker_count"
    print -r -- "marker_baseline_current_bytes=$marker_baseline_current"
    print -r -- "marker_baseline_effective_peak_bytes=$marker_baseline_peak"
    print -r -- "post_marker_samples=$post_marker_samples"
    print -r -- "post_marker_current_peak_bytes=$post_marker_current_peak"
    print -r -- "post_marker_effective_peak_bytes=$post_marker_reported_peak"
    print -r -- "publication_delta_bytes=$publication_delta"
} >"$log_dir/summary.txt"

event \
    "guard_status=$guard_status target_status=$target_status reason=$termination_reason"
command cat "$log_dir/summary.txt"

(( guard_status == 0 )) || exit "$guard_status"
exit "$target_status"
