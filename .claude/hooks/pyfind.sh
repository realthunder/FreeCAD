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
for py in ${FC_HOOK_PYTHON:-} python3 python "py -3"; do
    [ -n "$py" ] || continue
    if $py -c "import sys" >/dev/null 2>&1; then
        echo "$py"
        exit 0
    fi
done
exit 1
