#!/usr/bin/env bash
# Diagnose the CTest failures of the last test run (CI runs this only when the
# test step failed). For each failed test:
#   1. rerun it on its own REPEAT times: failing every time points at a real
#      regression, failing sometimes at a timing-sensitive (flaky) test;
#   2. when it passes on its own, rerun it right after each test that ran
#      before it, to find the one whose leftovers make it fail.
# Writes a Markdown report to $GITHUB_STEP_SUMMARY (stdout outside CI) and the
# output of failing reruns to build/diagnose/.
#
# Usage: tests/diagnose_test_failures.sh [ctest-dir ...]
#   (default: build/cmake and build/plugins)
# Environment: REPEAT (default 5) reruns on its own, PAIR_TRIES (default 1)
# runs after each earlier test, BISECT=0 skips step 2.
set -u
cd "$(dirname "$0")/.."

REPEAT=${REPEAT:-5}
PAIR_TRIES=${PAIR_TRIES:-1}
BISECT=${BISECT:-1}
LOG_DIR=build/diagnose
REPORT=${GITHUB_STEP_SUMMARY:-/dev/stdout}
mkdir -p "$LOG_DIR"

if [ $# -gt 0 ]; then dirs="$*"; else dirs="build/cmake build/plugins"; fi

# Runs the tests matching a regex; prints "pass" or "fail" for `test`.
# $1 ctest dir, $2 regex, $3 test name, $4 log file kept when it fails.
run_and_check() {
    local out
    out=$(ctest --test-dir "$1" -R "$2" --output-on-failure 2>&1)
    if printf '%s\n' "$out" | grep -Eq "Test +#[0-9]+: $3 \.+ +Passed"; then
        echo pass
    else
        printf '%s\n' "$out" > "$4"
        echo fail
    fi
}

found=0
for dir in $dirs; do
    failed_log="$dir/Testing/Temporary/LastTestsFailed.log"
    [ -s "$failed_log" ] || continue
    found=1
    # Reruns overwrite CTest's logs; keep the failed run's for the artifact.
    keep="$LOG_DIR/original-$(echo "$dir" | tr / -)"
    mkdir -p "$keep" && cp "$dir"/Testing/Temporary/*.log "$keep"/ 2>/dev/null
    # Every test in run order, one name per line.
    order=$(ctest --test-dir "$dir" -N | sed -nE 's/^ *Test +#[0-9]+: ([^ ]+).*/\1/p')
    failed=$(cut -d: -f2 "$failed_log")
    {
        echo "## Test failure diagnosis: \`$dir\`"
        echo
        echo "| Test | Passes on its own | Verdict | Fails after |"
        echo "|---|---|---|---|"
    } >> "$REPORT"
    for test in $failed; do
        echo "Diagnosing $test ($dir)" >&2
        passes=0
        for i in $(seq 1 "$REPEAT"); do
            result=$(run_and_check "$dir" "^${test}\$" "$test" "$LOG_DIR/$test-alone-$i.log")
            [ "$result" = pass ] && passes=$((passes + 1))
        done
        suspects=""
        if [ "$passes" -eq 0 ]; then
            verdict="fails on its own every time: likely a real regression"
        elif [ "$passes" -lt "$REPEAT" ]; then
            verdict="flaky: fails on its own sometimes (timing-sensitive)"
        else
            verdict="passes on its own"
        fi
        if [ "$passes" -gt 0 ] && [ "$BISECT" = 1 ]; then
            for previous in $order; do
                [ "$previous" = "$test" ] && break
                fails=0
                for i in $(seq 1 "$PAIR_TRIES"); do
                    # CTest runs the pair in its own order: the earlier test first.
                    result=$(run_and_check "$dir" "^(${previous}|${test})\$" "$test" "$LOG_DIR/$test-after-$previous-$i.log")
                    [ "$result" = fail ] && fails=$((fails + 1))
                done
                [ "$fails" -gt 0 ] && suspects="$suspects \`$previous\` ($fails/$PAIR_TRIES)"
            done
            if [ -n "$suspects" ]; then
                verdict="$verdict; fails after an earlier test (test interference)"
            else
                suspects="none found"
                [ "$passes" -eq "$REPEAT" ] && verdict="did not reproduce: a rare flake, or it needs more than one earlier test"
            fi
        fi
        echo "| \`$test\` | $passes/$REPEAT | $verdict | ${suspects:-not checked} |" >> "$REPORT"
    done
    echo >> "$REPORT"
done

if [ "$found" = 0 ]; then
    echo "No failed CTest tests recorded (LastTestsFailed.log is missing or empty); a build or unit-test step may have failed instead." >> "$REPORT"
fi
