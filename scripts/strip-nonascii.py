#!/usr/bin/env python3
"""Force the no-non-ASCII rule on newly written code, docs and commit messages.

Installed as a ``post-commit`` hook (see ``scripts/install-hooks.sh``) it looks at
the commit that was just made, transliterates every non-ASCII character on the
lines that commit *added* -- plus the commit message itself -- and amends the
commit in place.

Only added lines are touched.  Over a thousand tracked files carry non-ASCII
from upstream FreeCAD (author names in copyright headers, Qt translations); a
whole-file pass would bury every real change under unrelated churn.

Escape hatches:
  * a line containing the token ``nonascii-ok`` is left alone;
  * paths matching EXCLUDED_GLOBS are never touched;
  * set ``NO_STRIP_NONASCII=1`` to skip a commit entirely.

Manual use:
    strip-nonascii.py --check          # report what hook mode would rewrite
    strip-nonascii.py --files a.cpp    # clean whole files, no git involvement
"""

import fnmatch
import os
import re
import subprocess
import sys
import unicodedata

# Files whose non-ASCII content is the point: translations, test fixtures for
# encoding, and anything already encoding-sensitive.
EXCLUDED_GLOBS = [
    "*.ts",  # Qt translation sources
    "*.po",
    "*.pot",
    "*.qm",
    "*.qrc",
    "*.patch",
    "*.diff",
    "src/3rdParty/*",
    "*/translations/*",
    "*/Resources/translations/*",
]

ALLOW_TOKEN = "nonascii-ok"

# Deliberate transliterations.  Anything not listed falls through to NFKD accent
# folding (e-acute becomes e), and whatever survives is dropped with a warning.
TRANSLITERATIONS = {
    # dashes and hyphens
    "\u2014": "--",  # EM DASH
    "\u2013": "-",  # EN DASH
    "\u2012": "-",  # FIGURE DASH
    "\u2010": "-",  # HYPHEN
    "\u2011": "-",  # NON-BREAKING HYPHEN
    "\u2212": "-",  # MINUS SIGN
    "\u00ad": "",  # SOFT HYPHEN
    # quotes
    "\u2018": "'",  # LEFT SINGLE QUOTATION MARK
    "\u2019": "'",  # RIGHT SINGLE QUOTATION MARK
    "\u201a": "'",  # SINGLE LOW-9 QUOTATION MARK
    "\u201b": "'",  # SINGLE HIGH-REVERSED-9 QUOTATION MARK
    "\u201c": '"',  # LEFT DOUBLE QUOTATION MARK
    "\u201d": '"',  # RIGHT DOUBLE QUOTATION MARK
    "\u201e": '"',  # DOUBLE LOW-9 QUOTATION MARK
    "\u201f": '"',  # DOUBLE HIGH-REVERSED-9 QUOTATION MARK
    "\u2032": "'",  # PRIME
    "\u2033": '"',  # DOUBLE PRIME
    "\u00ab": '"',  # LEFT-POINTING DOUBLE ANGLE QUOTATION MARK
    "\u00bb": '"',  # RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK
    "\u2039": "<",  # SINGLE LEFT-POINTING ANGLE QUOTATION MARK
    "\u203a": ">",  # SINGLE RIGHT-POINTING ANGLE QUOTATION MARK
    # punctuation
    "\u2026": "...",  # HORIZONTAL ELLIPSIS
    "\u00a7": "sec ",  # SECTION SIGN
    "\u00b6": "para ",  # PILCROW SIGN
    "\u2022": "-",  # BULLET
    "\u00b7": "*",  # MIDDLE DOT
    "\u2043": "-",  # HYPHEN BULLET
    "\u2020": "+",  # DAGGER
    "\u2021": "++",  # DOUBLE DAGGER
    # arrows
    "\u2190": "<-",  # LEFTWARDS ARROW
    "\u2192": "->",  # RIGHTWARDS ARROW
    "\u2194": "<->",  # LEFT RIGHT ARROW
    "\u21d0": "<=",  # LEFTWARDS DOUBLE ARROW
    "\u21d2": "=>",  # RIGHTWARDS DOUBLE ARROW
    "\u21d4": "<=>",  # LEFT RIGHT DOUBLE ARROW
    "\u2191": "^",  # UPWARDS ARROW
    "\u2193": "v",  # DOWNWARDS ARROW
    # math and units
    "\u00d7": "x",  # MULTIPLICATION SIGN
    "\u00f7": "/",  # DIVISION SIGN
    "\u00b1": "+/-",  # PLUS-MINUS SIGN
    "\u2264": "<=",  # LESS-THAN OR EQUAL TO
    "\u2265": ">=",  # GREATER-THAN OR EQUAL TO
    "\u2260": "!=",  # NOT EQUAL TO
    "\u2248": "~",  # ALMOST EQUAL TO
    "\u2261": "==",  # IDENTICAL TO
    "\u221e": "inf",  # INFINITY
    "\u2211": "sum",  # N-ARY SUMMATION
    "\u220f": "prod",  # N-ARY PRODUCT
    "\u221a": "sqrt",  # SQUARE ROOT
    "\u2206": "delta",  # INCREMENT
    "\u2202": "d",  # PARTIAL DIFFERENTIAL
    "\u2229": "&",  # INTERSECTION
    "\u222a": "|",  # UNION
    "\u2208": " in ",  # ELEMENT OF
    "\u2205": "{}",  # EMPTY SET
    "\u00b0": "deg",  # DEGREE SIGN
    "\u00b5": "u",  # MICRO SIGN
    "\u03bc": "u",  # GREEK SMALL LETTER MU
    "\u03b1": "alpha",  # GREEK SMALL LETTER ALPHA
    "\u03b2": "beta",  # GREEK SMALL LETTER BETA
    "\u03b3": "gamma",  # GREEK SMALL LETTER GAMMA
    "\u03b8": "theta",  # GREEK SMALL LETTER THETA
    "\u03bb": "lambda",  # GREEK SMALL LETTER LAMDA
    "\u03c0": "pi",  # GREEK SMALL LETTER PI
    "\u03c3": "sigma",  # GREEK SMALL LETTER SIGMA
    "\u03c9": "omega",  # GREEK SMALL LETTER OMEGA
    # fractions and superscripts
    "\u00bc": "1/4",  # VULGAR FRACTION ONE QUARTER
    "\u00bd": "1/2",  # VULGAR FRACTION ONE HALF
    "\u00be": "3/4",  # VULGAR FRACTION THREE QUARTERS
    "\u2153": "1/3",  # VULGAR FRACTION ONE THIRD
    "\u2154": "2/3",  # VULGAR FRACTION TWO THIRDS
    "\u215b": "1/8",  # VULGAR FRACTION ONE EIGHTH
    "\u00b2": "^2",  # SUPERSCRIPT TWO
    "\u00b3": "^3",  # SUPERSCRIPT THREE
    "\u00b9": "^1",  # SUPERSCRIPT ONE
    "\u2070": "^0",  # SUPERSCRIPT ZERO
    "\u2074": "^4",  # SUPERSCRIPT FOUR
    "\u2075": "^5",  # SUPERSCRIPT FIVE
    "\u2076": "^6",  # SUPERSCRIPT SIX
    "\u2077": "^7",  # SUPERSCRIPT SEVEN
    "\u2078": "^8",  # SUPERSCRIPT EIGHT
    "\u2079": "^9",  # SUPERSCRIPT NINE
    # marks used as emphasis in docs
    "\u2b50": "*",  # WHITE MEDIUM STAR
    "\u26a0": "!",  # WARNING SIGN
    "\u26d4": "X",  # NO ENTRY
    "\u2705": "[x]",  # WHITE HEAVY CHECK MARK
    "\u2713": "[x]",  # CHECK MARK
    "\u2714": "[x]",  # HEAVY CHECK MARK
    "\u274c": "[ ]",  # CROSS MARK
    "\u2717": "x",  # BALLOT X
    "\u2718": "x",  # HEAVY BALLOT X
    "\u2757": "!",  # HEAVY EXCLAMATION MARK SYMBOL
    "\u2753": "?",  # BLACK QUESTION MARK ORNAMENT
    # legal and currency
    "\u00a9": "(c)",  # COPYRIGHT SIGN
    "\u00ae": "(R)",  # REGISTERED SIGN
    "\u2122": "(TM)",  # TRADE MARK SIGN
    "\u20ac": "EUR",  # EURO SIGN
    "\u00a3": "GBP",  # POUND SIGN
    "\u00a5": "JPY",  # YEN SIGN
    # invisible characters -- deleted outright
    "\u00a0": " ",  # NO-BREAK SPACE
    "\u2007": " ",  # FIGURE SPACE
    "\u2009": " ",  # THIN SPACE
    "\u200a": " ",  # HAIR SPACE
    "\u202f": " ",  # NARROW NO-BREAK SPACE
    "\u205f": " ",  # MEDIUM MATHEMATICAL SPACE
    "\u3000": " ",  # IDEOGRAPHIC SPACE
    "\u200b": "",  # ZERO WIDTH SPACE
    "\u200c": "",  # ZERO WIDTH NON-JOINER
    "\u200d": "",  # ZERO WIDTH JOINER
    "\ufeff": "",  # ZERO WIDTH NO-BREAK SPACE
    "\ufe0e": "",  # VARIATION SELECTOR-15
    "\ufe0f": "",  # VARIATION SELECTOR-16
    "\u2028": "",  # LINE SEPARATOR
    "\u2029": "",  # PARAGRAPH SEPARATOR
}

for _sp in "\u2000\u2001\u2002\u2003\u2004\u2005\u2006\u2008":  # EN/EM/THIN QUADS
    TRANSLITERATIONS.setdefault(_sp, " ")

NON_ASCII = re.compile(r"[^\x00-\x7f]")


def transliterate(line):
    """Return (ascii_line, [chars dropped without a mapping])."""
    if not NON_ASCII.search(line):
        return line, []
    out = []
    dropped = []
    for ch in line:
        if ord(ch) < 128:
            out.append(ch)
            continue
        mapped = TRANSLITERATIONS.get(ch)
        if mapped is not None:
            out.append(mapped)
            continue
        # Accented Latin letters fold cleanly: e-acute -> e, u-umlaut -> u.
        folded = unicodedata.normalize("NFKD", ch)
        ascii_folded = "".join(c for c in folded if ord(c) < 128)
        if ascii_folded:
            out.append(ascii_folded)
        else:
            dropped.append(ch)
    return "".join(out), dropped


def git(*args):
    """Run a git command and return its stdout as text."""
    res = subprocess.run(
        ("git", "-c", "core.quotePath=false") + args,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if res.returncode != 0:
        raise RuntimeError(
            "git %s failed: %s" % (" ".join(args), res.stderr.decode("utf-8", "replace"))
        )
    return res.stdout.decode("utf-8", "surrogateescape")


def is_excluded(path):
    return any(fnmatch.fnmatch(path, pat) for pat in EXCLUDED_GLOBS)


def added_lines_of_head():
    """Map path -> set of 1-based line numbers the HEAD commit added."""
    diff = git("show", "--format=", "--unified=0", "--no-color", "--no-renames", "HEAD")
    result = {}
    path = None
    newno = 0
    prev = ""
    for line in diff.split("\n"):
        # A real "+++ b/path" header is always preceded by "--- a/path"; an added
        # line reading "++ foo" also renders as "+++ foo" and must not match.
        if line.startswith("+++ ") and prev.startswith("--- "):
            target = line[4:]
            path = None if target == "/dev/null" else target[2:]  # strip "b/"
            prev = line
            continue
        prev = line
        if line.startswith("@@"):
            m = re.match(r"@@ -\d+(?:,\d+)? \+(\d+)", line)
            newno = int(m.group(1)) if m else 0
            continue
        if path is None or not line:
            continue
        if line[0] == "+":
            result.setdefault(path, set()).add(newno)
            newno += 1
        elif line[0] == " ":
            newno += 1
    return result


def clean_file(path, linenos=None, write=True):
    """Rewrite `path` in place. Returns (changed, [(lineno, dropped_chars)])."""
    with open(path, "rb") as fh:
        data = fh.read()
    if b"\x00" in data:
        return False, []  # binary
    text = data.decode("utf-8", "surrogateescape")
    lines = text.split("\n")  # git counts lines by \n only
    changed = False
    warnings = []
    for idx, line in enumerate(lines):
        lineno = idx + 1
        if linenos is not None and lineno not in linenos:
            continue
        if ALLOW_TOKEN in line:
            continue
        crlf = line.endswith("\r")
        body = line[:-1] if crlf else line
        new, dropped = transliterate(body)
        if new != body:
            lines[idx] = new + "\r" if crlf else new
            changed = True
            if dropped:
                warnings.append((lineno, dropped))
    if changed and write:
        with open(path, "wb") as fh:
            fh.write("\n".join(lines).encode("utf-8", "surrogateescape"))
    return changed, warnings


def repo_state_blocks_amend(git_dir):
    """Amending mid-rebase/merge/cherry-pick would corrupt the operation."""
    for marker in (
        "rebase-merge",
        "rebase-apply",
        "MERGE_HEAD",
        "CHERRY_PICK_HEAD",
        "REVERT_HEAD",
        "BISECT_LOG",
    ):
        if os.path.exists(os.path.join(git_dir, marker)):
            return marker
    return None


def hook_main(check_only):
    if os.environ.get("NO_STRIP_NONASCII"):
        return 0
    if os.environ.get("STRIP_NONASCII_RUNNING"):
        return 0  # our own amend re-triggered the hook

    git_dir = git("rev-parse", "--absolute-git-dir").strip()
    toplevel = git("rev-parse", "--show-toplevel").strip()
    os.chdir(toplevel)

    if not check_only:
        blocker = repo_state_blocks_amend(git_dir)
        if blocker:
            return 0  # silently stand down during rebase/merge/cherry-pick

    if len(git("rev-list", "--parents", "-n", "1", "HEAD").split()) > 2:
        return 0  # merge commit: no meaningful "added lines"

    # Files differing between HEAD and the working tree cannot be safely staged
    # into an amend -- doing so would swallow unrelated edits.
    dirty = set(p for p in git("diff", "--name-only", "-z", "HEAD").split("\0") if p)

    added = added_lines_of_head()
    touched = []
    skipped = []
    all_warnings = []
    for path, linenos in sorted(added.items()):
        if is_excluded(path) or not os.path.isfile(path):
            continue
        if path in dirty:
            skipped.append(path)
            continue
        changed, warnings = clean_file(path, linenos, write=not check_only)
        if changed:
            touched.append(path)
        for lineno, dropped in warnings:
            all_warnings.append((path, lineno, dropped))

    message = git("log", "-1", "--format=%B", "HEAD")
    new_message, msg_dropped = transliterate(message)
    message_changed = new_message != message
    if msg_dropped:
        all_warnings.append(("<commit message>", 0, msg_dropped))

    if not touched and not message_changed:
        if skipped:
            sys.stderr.write(
                "strip-nonascii: skipped %d file(s) modified since the commit: %s\n"
                % (len(skipped), ", ".join(skipped))
            )
        return 0

    label = "would rewrite" if check_only else "rewrote"
    for path in touched:
        sys.stderr.write("strip-nonascii: %s %s\n" % (label, path))
    if message_changed:
        sys.stderr.write("strip-nonascii: %s the commit message\n" % label)
    for path, lineno, dropped in all_warnings:
        sys.stderr.write(
            "strip-nonascii: dropped unmappable %s at %s:%d\n"
            % (" ".join("U+%04X" % ord(c) for c in dropped), path, lineno)
        )
    if skipped:
        sys.stderr.write(
            "strip-nonascii: NOT cleaned (modified since the commit): %s\n"
            % ", ".join(skipped)
        )

    if check_only:
        return 1

    env = dict(os.environ, STRIP_NONASCII_RUNNING="1")
    if touched:
        subprocess.run(["git", "add", "--"] + touched, check=True, env=env)
    amend = ["git", "commit", "--amend", "--no-verify", "--cleanup=whitespace",
             "--file=-"]
    proc = subprocess.run(
        amend,
        input=new_message.encode("utf-8", "surrogateescape"),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
    )
    if proc.returncode != 0:
        sys.stderr.write(
            "strip-nonascii: amend failed, commit left as-is:\n%s\n"
            % proc.stderr.decode("utf-8", "replace")
        )
        return 0  # never fail the commit that already happened
    sys.stderr.write("strip-nonascii: amended %s\n" % git("rev-parse", "--short", "HEAD").strip())
    return 0


def main(argv):
    if "--files" in argv:
        paths = argv[argv.index("--files") + 1:]
        if not paths:
            sys.stderr.write("usage: strip-nonascii.py --files <path>...\n")
            return 2
        for path in paths:
            changed, warnings = clean_file(path)
            if changed:
                sys.stderr.write("strip-nonascii: rewrote %s\n" % path)
            for lineno, dropped in warnings:
                sys.stderr.write(
                    "strip-nonascii: dropped unmappable %s at %s:%d\n"
                    % (" ".join("U+%04X" % ord(c) for c in dropped), path, lineno)
                )
        return 0
    return hook_main(check_only="--check" in argv)


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except Exception as exc:  # a hook must never wedge the user's workflow
        sys.stderr.write("strip-nonascii: %s\n" % exc)
        sys.exit(0)
