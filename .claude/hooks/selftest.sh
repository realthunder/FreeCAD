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
DIR="build/mac-relwithdebinfo-801"
ABS="$PWD/$DIR"

if /usr/bin/pgrep -x ninja >/dev/null 2>&1; then
    for p in $(/usr/bin/pgrep -x ninja); do
        echo "a build IS running: pid $p  $(/bin/ps -o args= -p "$p")"
    done
    LIVE=1
else
    echo "NO build is running -- every case below must come back 'allow',"
    echo "which tests the parser but not the blocking. Re-run during a build."
    LIVE=0
fi
echo

pass=0; fail=0
check() {   # check <expected> <command>
    local want="$1" cmd="$2" got
    got=$(python3 -c '
import json, sys
print(json.dumps({"tool_name": "Bash",
                  "tool_input": {"command": sys.argv[1]},
                  "cwd": sys.argv[2]}))' "$cmd" "$3" \
        | bash "$GUARD" \
        | python3 -c '
import json, sys
s = sys.stdin.read().strip()
print(json.loads(s)["hookSpecificOutput"]["permissionDecision"] if s
      else "allow")')
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
