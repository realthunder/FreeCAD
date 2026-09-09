#!/usr/bin/env bash
#
# Self-test for the build guard. Kept as a FILE, and deliberately named
# without the word this repo's guard filters on, so that running it does
# not itself contain a build command the guard has to judge.
#
# The interesting cases are the ones that merely MENTION a build command
# inside a string. A guard that blocks `grep -c "<build command>" log`
# is worse than no guard: it blocks the very diagnostics you reach for
# while a build is running.
set -u
cd "$(dirname "$0")/../.." || exit 1
GUARD=".claude/hooks/ninja-guard.sh"
DIR="${FC_GUARD_TEST_DIR:-build/mac-relwithdebinfo-801}"
ABS="$PWD/$DIR"
# The payloads are built with Python too, so the self-test has the same
# interpreter problem the guard had -- resolve it the same proved way
# rather than trusting the name.
PY=$(sh .claude/hooks/pyfind.sh 2>/dev/null) || PY=""
if [ -z "$PY" ]; then
    echo "no working Python found; set FC_HOOK_PYTHON. Cannot self-test." >&2
    exit 2
fi

if command -v pgrep >/dev/null 2>&1 && pgrep -x ninja >/dev/null 2>&1; then
    for p in $(pgrep -x ninja); do
        echo "a build IS running: pid $p  $(ps -o args= -p "$p" 2>/dev/null)"
    done
    LIVE=1
elif command -v pgrep >/dev/null 2>&1; then
    echo "NO build is running -- every case below must come back 'allow',"
    echo "which tests the parser but not the blocking. Re-run during a build."
    LIVE=0
else
    # Windows, or any box without pgrep. The PARSER cases are the ones
    # that matter most anyway -- they are what stops the guard blocking
    # a grep -- and they do not need a live process.
    echo "no pgrep here; testing the parser only. The blocking path needs"
    echo "a live build and a platform this can enumerate."
    LIVE=0
fi
echo

pass=0; fail=0
check() {   # check <expected> <command>
    local want="$1" cmd="$2" got
    got=$($PY -c '
import json, sys
print(json.dumps({"tool_name": "Bash",
                  "tool_input": {"command": sys.argv[1]},
                  "cwd": sys.argv[2]}))' "$cmd" "$3" \
        | bash "$GUARD" \
        | $PY -c '
import json, sys
s = sys.stdin.read().strip()
# A systemMessage-only reply means the guard ran but could not look --
# it allows, and says so. Not a decision, so it reads as "allow".
d = json.loads(s) if s else {}
print(d.get("hookSpecificOutput", {}).get("permissionDecision", "allow"))')
    if [ "$got" = "$want" ]; then
        pass=$((pass + 1)); printf "  ok    %-6s %s\n" "$got" "$cmd"
    else
        fail=$((fail + 1))
        printf "  FAIL  got %-6s want %-6s %s\n" "$got" "$want" "$cmd"
    fi
}

echo "-- a build command only MENTIONED in a string is not a build --"
check allow "ps aux | grep ninja" "$PWD"
check allow "pgrep -x ninja" "$PWD"
check allow "grep -c \"ninja -C $DIR\" /tmp/relink.log" "$PWD"
check allow "ps -ax -o command | grep \"[n]inja -C build\"" "$PWD"
check allow "echo ninja -C $DIR" "$PWD"
check allow "rg 'ninja -C' docs/" "$PWD"
check allow "cat /tmp/claude-ninja-guard-check.txt" "$PWD"
check allow "sed -i '' 's/ninja -C x/y/' notes.txt" "$PWD"

echo
echo "-- a different build directory is not this build --"
check allow "ninja -C build/some-other-tree all" "$PWD"

echo
echo "-- real invocations of the running build --"
WANT=allow; [ "$LIVE" = 1 ] && WANT=deny
check "$WANT" "ninja -C $DIR -j 4 FreeCADGui" "$PWD"
check "$WANT" "ninja -C$DIR" "$PWD"
check "$WANT" "./.conda/run.sh ninja -C $DIR -j 4" "$PWD"
check "$WANT" "cd $PWD && ninja -C $DIR" "$PWD"
check "$WANT" "env FOO=1 ninja -C $DIR" "$PWD"
check "$WANT" "bash -c \"ninja -C $DIR\"" "$PWD"
check "$WANT" "ninja -C $ABS" "/tmp"
check "$WANT" "ninja --directory=$DIR" "$PWD"

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
