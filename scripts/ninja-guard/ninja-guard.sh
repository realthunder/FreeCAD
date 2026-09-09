#!/bin/sh
# PreToolUse wrapper for ninja_guard.py: run it under whatever Python
# this machine actually has, and SAY SO OUT LOUD when there is none.
#
# The guard used to be a `#!/usr/bin/env python3 -c` one-liner. On
# Windows that resolves to the Microsoft Store app-execution alias,
# which prints "Python was not found; run without arguments to install
# from the Microsoft Store" and exits 49. A hook error is non-blocking,
# so the guard FAILED OPEN -- builds ran unguarded and nothing said the
# protection was gone. That is worse than having no guard at all, and it
# is the same trap scripts/post-commit-hook.sh was written to defeat;
# the fix is the same prove-it-runs loop rather than a shebang.
#
# Reported from the Windows box by a peer session, 2026-09-09.
#
# Set FC_HOOK_PYTHON to name an interpreter yourself.
set -u

# Parameter expansion, not dirname: this must still work on a box
# where the guard is the thing telling you something is missing.
dir=${0%/*}
[ "$dir" = "$0" ] && dir=.
py=$(sh "$dir/pyfind.sh" 2>/dev/null) || py=""

if [ -z "$py" ]; then
    # A systemMessage rather than silence: the user is told the guard is
    # not guarding, the way post-commit says SKIPPED. Still exit 0 --
    # denying every build on a machine without Python would be a worse
    # trade than saying plainly that the check is off.
    printf '%s\n' '{"systemMessage":"ninja guard: no working Python found, so it is NOT checking for a second build in the same directory. Set FC_HOOK_PYTHON to an interpreter to restore it."}'
    exit 0
fi

exec $py "$dir/ninja_guard.py"
