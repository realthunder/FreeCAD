#!/bin/sh
# Install the no-non-ASCII post-commit hook into this repo, and optionally into
# sibling repos (coin, occt, the feedstocks).
#
#   scripts/install-hooks.sh                     # this repo
#   scripts/install-hooks.sh ~/works/sw/coin ..  # this repo plus the named ones
#
# The hook is copied (not symlinked) into each target's .git/hooks/post-commit so
# the sibling repos stay self-contained.  .git/hooks/pre-commit is left alone --
# that slot belongs to the pre-commit framework, and setting core.hooksPath would
# make `pre-commit install` refuse to run.
set -eu

src=$(cd "$(dirname "$0")" && pwd)/strip-nonascii.py
[ -f "$src" ] || { echo "missing $src" >&2; exit 1; }

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
    cp "$src" "$hooks/post-commit"
    chmod +x "$hooks/post-commit"
    echo "installed $hooks/post-commit"
}

install_into "$(git -C "$(dirname "$src")" rev-parse --show-toplevel)"
for repo in "$@"; do
    install_into "$repo"
done
