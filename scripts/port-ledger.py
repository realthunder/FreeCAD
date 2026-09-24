#!/usr/bin/env python3
"""Build an upstream-port ledger: one TSV row per upstream commit that touches
the given paths, with a guess at its kind and whether its patch applies
forward or in reverse against the working tree.

    port-ledger.py --base a662fbb2ff --tip upstream/main --strip src/Mod/PartDesign/ \
        --gizmo src/Gui/Inventor/Draggers,src/Mod/Part/App/GizmoHelper \
        -o docs/PartDesignPort-ledger.tsv src/Mod/PartDesign tests/src/Mod/PartDesign

This is the generator behind docs/PartDesignPort-ledger.tsv. The Sketcher
ledger (docs/SketcherPort-ledger.tsv) was built the same way by hand; its
columns are kept, with a `family` column added.
"""

import argparse
import re
import subprocess
import sys
import unicodedata

KINDS = [
    ("noise", r"typo|reformat|whitespace|translat|spelling|pre-commit|clang-tidy|lint|"
              r"warning|header|copyright|licen[cs]e|include|precompiled|\bpch\b|comment"),
    ("fix", r"\bfix|crash|\bbug|issue|regression|correct|prevent|avoid|wrong|broken|"
            r"segfault|missing|\bnull"),
    ("refactor", r"refactor|clean|\bmove|rename|modern|replace|simplif|remove unused|"
                 r"\bstd::|use .* instead|convert|deprecat|port to"),
    ("feature", r"\badd|implement|support|allow|\bnew\b|enable|improve|introduce|option|"
                r"\bshow|gizmo"),
]


def git(*args, stdin=None, check=True):
    p = subprocess.run(["git", *args], input=stdin, capture_output=True, check=False)
    if check and p.returncode:
        sys.exit(p.stderr.decode(errors="replace"))
    return p


def ascii_of(text):
    """Upstream names and subjects carry non-ASCII; the docs are ASCII only."""
    text = text.translate({0xF8: "o", 0xD8: "O", 0xE6: "ae", 0xC6: "AE", 0xDF: "ss", 0x142: "l"})
    text = unicodedata.normalize("NFKD", text)
    return text.encode("ascii", "ignore").decode()


def kind_of(subject):
    for kind, pattern in KINDS:
        if re.search(pattern, subject, re.I):
            return kind
    return "?"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", required=True)
    ap.add_argument("--tip", required=True)
    ap.add_argument("--strip", default="", help="path prefix dropped in the files column")
    ap.add_argument("--gizmo", default="", help="comma list of path prefixes that mark family=gizmo")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("paths", nargs="+")
    a = ap.parse_args()

    gizmo_paths = [g for g in a.gizmo.split(",") if g]
    paths = a.paths + gizmo_paths
    log = git("log", "--reverse", "--no-merges", "--full-history",
              "--format=%x00%h%x09%ad%x09%an%x09%s", "--date=short", "--numstat",
              f"{a.base}..{a.tip}", "--", *paths).stdout.decode("utf-8", errors="replace")

    rows = []
    for rec in log.split("\x00")[1:]:
        lines = [l for l in rec.splitlines() if l.strip()]
        h, date, author, subject = lines[0].split("\t", 3)
        files, total = [], 0
        for l in lines[1:]:
            add, rem, f = l.split("\t", 2)
            if "=>" in f:  # rename: keep the destination
                f = re.sub(r"\{[^}]*=> ([^}]*)\}", r"\1", f).replace("//", "/")
                f = f.split(" => ")[-1]
            files.append(f)
            total += (int(add) if add.isdigit() else 0) + (int(rem) if rem.isdigit() else 0)
        if all("/translations/" in f or f.endswith((".ts", ".qm")) for f in files):
            continue
        patch = git("diff", "--binary", f"{h}^", h, "--", *paths, check=False).stdout
        fwd = int(git("apply", "--check", "-", stdin=patch, check=False).returncode == 0)
        rev = int(git("apply", "--check", "-R", "-", stdin=patch, check=False).returncode == 0)
        status = "have(rev)" if rev and not fwd else "open"
        family = ""
        if any(f.startswith(g) for f in files for g in gizmo_paths) or re.search(
            r"gizmo|dragger", subject, re.I
        ):
            family = "gizmo"
        shown = [f[len(a.strip):] if a.strip and f.startswith(a.strip) else f for f in files]
        rows.append([h, date, author, kind_of(subject), family, str(fwd), str(rev), status,
                     str(total), "", subject.replace("\t", " "), " ".join(shown)])
        print(f"{len(rows)} {h} {status} {subject[:60]}", file=sys.stderr)

    with open(a.output, "w", encoding="utf-8", newline="\n") as out:
        out.write(f"# Upstream commits since {a.base} touching {' '.join(paths)}, "
                  f"tip {git('rev-parse', '--short', a.tip).stdout.decode().strip()}.\n")
        out.write("# kind: subject-regex guess (fix/feature/refactor/noise/?). family: gizmo = "
                  "touches the gizmo sources or says gizmo/dragger.\n")
        out.write("# fwd/rev: git apply --check against the branch at creation. "
                  "status: have(rev)=reverse-applies, open.\n")
        out.write("# decision: taken <fork hash> | adapted <fork hash> | have | n/a | superseded "
                  "| declined <why> | deferred\n")
        out.write("hash\tdate\tauthor\tkind\tfamily\tfwd\trev\tstatus\tlines\tdecision\tsubject\tfiles\n")
        for r in rows:
            out.write(ascii_of("\t".join(r)) + "\n")


if __name__ == "__main__":
    main()
