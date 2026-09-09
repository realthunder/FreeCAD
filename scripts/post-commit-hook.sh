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
#
# The repo's own conda env comes FIRST after the override, because on the
# Windows box it is the only candidate that can succeed -- STRIP_NONASCII_PYTHON
# unset, python3/python the Store stub and no "py -3" installed is a live
# configuration there, and every commit printed SKIPPED while enforcing nothing.
# It is the interpreter the build itself runs, so anyone with a build has it,
# and trying it first also stops the Store stub from winning. Still PROVED like
# every other candidate, so a checkout without that env costs one failed exec.
# The repo root from GIT, not from $0. This script is INSTALLED as
# .git/hooks/post-commit -- a copy, not a symlink -- so relative to $0
# the parent is .git and the conda path below became .git/.conda/...,
# which exists nowhere. The hook then still reported SKIPPED and the
# fix looked like it had not worked.
root=$(git rev-parse --show-toplevel 2>/dev/null) || root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
for py in ${STRIP_NONASCII_PYTHON:-} \
        "$root/.conda/freecad/bin/python" \
        "$root/.conda/freecad/python.exe" \
        python3 python "py -3"; do
    [ -n "$py" ] || continue
    if $py -c "import sys" >/dev/null 2>&1; then
        exec $py "$script" "$@"
    fi
done

echo "post-commit: no working Python found -- the non-ASCII check was SKIPPED." >&2
echo "post-commit: set STRIP_NONASCII_PYTHON to an interpreter to fix this." >&2
exit 0
