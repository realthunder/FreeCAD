#!/bin/sh
# Install the no-non-ASCII post-commit hook into this repo, and optionally into
# sibling repos (coin, occt, the feedstocks).
#
#   scripts/install-hooks.sh                     # this repo
#   scripts/install-hooks.sh ~/works/sw/coin ..  # this repo plus the named ones
#
# Two files are copied (not symlinked) into each target's hooks directory, so the
# sibling repos stay self-contained: strip-nonascii.py, and post-commit-hook.sh as
# the post-commit hook itself. The wrapper is there because the script's own
# "#!/usr/bin/env python3" shebang finds the Microsoft Store stub on Windows and
# the check then silently does not run; the wrapper proves an interpreter works
# first and warns when none does.  .git/hooks/pre-commit is left alone -- that
# slot belongs to the pre-commit framework, and setting core.hooksPath would make
# `pre-commit install` refuse to run.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
src=$here/strip-nonascii.py
wrapper=$here/post-commit-hook.sh
[ -f "$src" ] || { echo "missing $src" >&2; exit 1; }
[ -f "$wrapper" ] || { echo "missing $wrapper" >&2; exit 1; }

install_into() {
    repo=$1
    if ! common=$(git -C "$repo" rev-parse --git-common-dir 2>/dev/null); then
        echo "skip $repo (not a git repo)" >&2
        return 0
    fi
    # --git-common-dir may be relative to the repo; hooks live there, not in a
    # linked worktree's gitdir.  core.hooksPath, if set, wins over both.
    case "$common" in /*) ;; *) common=$(cd "$repo" && cd "$common" && pwd) ;; esac
    if hooks=$(git -C "$repo" config --get core.hooksPath 2>/dev/null); then
        case "$hooks" in /*) ;; *) hooks=$(cd "$repo" && echo "$(pwd)/$hooks") ;; esac
    else
        hooks="$common/hooks"
    fi
    mkdir -p "$hooks"
    cp "$src" "$hooks/strip-nonascii.py"
    cp "$wrapper" "$hooks/post-commit"
    chmod +x "$hooks/strip-nonascii.py" "$hooks/post-commit"
    echo "installed $hooks/post-commit"
}

install_into "$(git -C "$(dirname "$src")" rev-parse --show-toplevel)"
for repo in "$@"; do
    install_into "$repo"
done
