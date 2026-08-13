#!/usr/bin/env python3
"""Refuse a commit that flips a file's line endings wholesale.

The tree is a deliberate mix of CRLF and LF (see .gitattributes), so there is
no single convention to enforce. What there is, is a failure mode: an editing
tool rewrites every line of a file, the real change is five lines, and the
commit reads "3856 insertions, 3763 deletions". `sed -i` under Git Bash is the
usual culprit, and Git Bash's grep and file both hide CR, so it is easy to
"fix" it in the direction that caused it.

This compares each staged file against HEAD and fails if the dominant ending
changed. Content is not touched -- the repair is the committer's call, because
only they know which way the file is supposed to go.

Usage: check-line-endings.py [FILE...]
With no arguments, checks everything currently staged.
"""

# Keeps the `list[str]` and `bytes | None` annotations below from being
# evaluated at import, so this still runs on the 3.8/3.9 interpreters some
# distributions still ship. Without it PEP 604 unions are a SyntaxError.
from __future__ import annotations

import subprocess
import sys


def git(*args: str) -> bytes:
    return subprocess.run(["git", *args], stdout=subprocess.PIPE, check=True).stdout


def staged_files() -> list[str]:
    out = git("diff", "--cached", "--name-only", "--diff-filter=M", "-z")
    return [name for name in out.decode("utf-8", "replace").split("\0") if name]


def blob(rev: str, path: str) -> bytes | None:
    """Contents of path at rev, or None when it is absent or unreadable there."""
    proc = subprocess.run(
        ["git", "show", f"{rev}:{path}"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL
    )
    return proc.stdout if proc.returncode == 0 else None


def dominant_ending(data: bytes) -> str | None:
    """"crlf", "lf", or None when there is nothing to judge by."""
    if b"\0" in data[:8000]:
        return None  # binary
    crlf = data.count(b"\r\n")
    lf = data.count(b"\n") - crlf
    if crlf == 0 and lf == 0:
        return None
    return "crlf" if crlf > lf else "lf"


def main(argv: list[str]) -> int:
    paths = argv[1:] or staged_files()
    flipped = []

    for path in paths:
        before, after = blob("HEAD", path), blob("", path)  # "" == the index
        if before is None or after is None:
            continue
        was, now = dominant_ending(before), dominant_ending(after)
        if was and now and was != now:
            flipped.append((path, was, now))

    if not flipped:
        return 0

    print("Line endings flipped wholesale -- this is almost never intended:\n")
    for path, was, now in flipped:
        print(f"  {path}: {was.upper()} -> {now.upper()}")
    print(
        "\nThe diff for these files will show as a full rewrite, burying the real change.\n"
        "Restore the original ending and stage again:\n"
        "    sed -i 's/\\r*$/\\r/' <file>   # back to CRLF\n"
        "    sed -i 's/\\r$//'    <file>   # back to LF\n"
        "Confirm with:  git diff --cached --numstat -- <file>\n"
        "(Git Bash's grep and file hide CR; ask git instead:\n"
        "    git show HEAD:<file> | cat -A | grep -c '\\^M\\$' )"
    )
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
