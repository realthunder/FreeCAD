#!/bin/sh
# Run the no-non-ASCII backstop (strip-nonascii.py, installed beside this file)
# under whatever Python this machine actually has.
#
# The script used to BE the hook and relied on its own "#!/usr/bin/env python3"
# shebang. On Windows that resolves to the Microsoft Store app-execution alias,
# which prints "Python was not found; run without arguments to install from the
# Microsoft Store" and exits -- so every commit ended with a stub's error
# message and no ASCII enforcement at all, in a line that reads like unrelated
# tooling noise. Hence this wrapper: it PROVES an interpreter runs before using
# it, and says so out loud when none does.
#
# Installed by scripts/install-hooks.sh. Set STRIP_NONASCII_PYTHON to name an
# interpreter yourself (a conda env's python.exe, say).
set -eu

dir=$(dirname "$0")
script="$dir/strip-nonascii.py"
[ -f "$script" ] || exit 0

# Each candidate is tried, not trusted: the Store stub is on PATH under both
# usual names and answers nothing. "py -3" is the Windows launcher, which finds
# a real installation where the aliases do not.
for py in ${STRIP_NONASCII_PYTHON:-} python3 python "py -3"; do
    [ -n "$py" ] || continue
    if $py -c "import sys" >/dev/null 2>&1; then
        exec $py "$script" "$@"
    fi
done

echo "post-commit: no working Python found -- the non-ASCII check was SKIPPED." >&2
echo "post-commit: set STRIP_NONASCII_PYTHON to an interpreter to fix this." >&2
exit 0
