#!/bin/sh
# Print a Python interpreter that has been PROVED to run, or nothing.
#
# Same reasoning as scripts/post-commit-hook.sh, and the same trap: on
# Windows `python3` and `python` are the Microsoft Store app-execution
# aliases, which print "Python was not found; run without arguments to
# install from the Microsoft Store" and exit 49. A shebang or a bare
# name is therefore not evidence of an interpreter -- each candidate is
# TRIED, not trusted. "py -3" is the Windows launcher, which finds a
# real installation where the aliases do not.
#
# Set FC_HOOK_PYTHON to name one yourself (a conda env's python.exe).
#
# The repo's own conda env comes FIRST after that override, ahead of the
# names on PATH. It is the interpreter the build itself runs, so every
# developer with a build has it, and on Windows it is the only candidate
# in this list that can succeed -- FC_HOOK_PYTHON unset, python3/python
# the Store stub and no "py -3" installed is a live configuration on the
# Windows box, and there the guard announced itself dead rather than
# guarding anything. Trying it first also keeps the Store stub from
# winning. It is still PROVED like every other candidate, so a checkout
# without that env costs one failed exec and moves on.
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
for py in ${FC_HOOK_PYTHON:-} \
        "$root/.conda/freecad/bin/python" \
        "$root/.conda/freecad/python.exe" \
        python3 python "py -3"; do
    [ -n "$py" ] || continue
    if $py -c "import sys" >/dev/null 2>&1; then
        echo "$py"
        exit 0
    fi
done
exit 1
