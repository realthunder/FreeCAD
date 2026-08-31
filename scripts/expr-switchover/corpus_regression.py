#!/usr/bin/env python3
"""The compatibility gate for the expression evaluation switch-over.

Opens real .FCStd files, and for every stored expression in them
evaluates the SAME source twice -- in-process and through the sandbox
image -- then compares.  What it produces is the gap list: the shapes
the image cannot evaluate yet, ranked by how often they occur in the
field.  Migration constraint (docs/ExpressionSandbox.md sec 11): old
files must evaluate identically.

Run under FreeCADCmd, wrapped, with a scratch user home.  Note the
`-c exec(...)` form: passing a script PATH to FreeCADCmd runs nothing
on this build, and does so silently.

    SP=<scratchpad>; mkdir -p $SP/fchome
    QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=$SP/fchome \
    FCX_IMAGE=$PWD/build/wasi-image/fcx_image.wasm \
    FCX_STDLIB=$HOME/works/sw/cpython-wasi/Python-3.12.13/Lib \
      .conda/limited.sh .conda/run.sh \
      build/conda-relwithdebinfo-801/bin/FreeCADCmd -c \
      "import sys; sys.argv=['gate','--max-mb','8','--out','$SP/gate.jsonl']; \
       exec(open('scripts/expr-switchover/corpus_regression.py').read())"

Bounded on purpose ([[oom-pcurve-walk]]): files are taken smallest
first and anything over --max-mb is skipped, because one 179 MB
assembly can take the box down and adds nothing to a parity question.
"""

import argparse
import json
import math
import os
import sys
import time
import traceback

import FreeCAD as App


class PrefGuard:
    """Set preferences for the run and put them back afterwards.

    This rig changes GLOBAL preferences -- enforcement, routing, the
    image path.  That is fine while it runs and poisons the box
    afterwards: a left-behind `Enforce=0` silently disarms the
    permission tests, which then pass for the wrong reason.  It cost a
    debugging detour once already.

    WARNING: FREECAD_USER_HOME is NOT a safety net.
    Application::getCustomPaths CLEARS it when the directory does not
    exist, without a word, and the run writes to the user's real config.
    run_gate.sh `mkdir -p`s it; restore anyway, for the callers that do
    not.
    """

    def __init__(self):
        self._undo = []

    def set_bool(self, group, key, value):
        params = App.ParamGet(group)
        had = key in params.GetBools()
        prior = params.GetBool(key, False) if had else None
        self._undo.append((group, key, "bool", had, prior))
        params.SetBool(key, value)

    def set_string(self, group, key, value):
        params = App.ParamGet(group)
        had = key in params.GetStrings()
        prior = params.GetString(key, "") if had else None
        self._undo.append((group, key, "string", had, prior))
        params.SetString(key, value)

    def restore(self):
        for group, key, kind, had, prior in reversed(self._undo):
            params = App.ParamGet(group)
            if kind == "bool":
                params.SetBool(key, prior) if had else params.RemBool(key)
            else:
                params.SetString(key, prior) if had else params.RemString(key)
        self._undo = []


def parse_args(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--roots", nargs="*", default=[os.path.expanduser("~/works")])
    ap.add_argument("--max-mb", type=float, default=8.0,
                    help="skip .FCStd files larger than this")
    ap.add_argument("--max-files", type=int, default=0, help="0 = no limit")
    ap.add_argument("--only", default=os.environ.get("FCX_GATE_ONLY", ""),
                    help="only files whose path contains this substring; "
                         "defaults to $FCX_GATE_ONLY, which is how the "
                         "driver passes paths that contain quotes or "
                         "backslashes (real corpus paths do)")
    ap.add_argument("--start", type=int, default=0,
                    help="index into the size-sorted file list to start at")
    ap.add_argument("--list-only", default="",
                    help="write the file list (index, size, path) to this "
                         "FILE and stop.  A file, not stdout: FreeCAD's "
                         "startup and restore chatter shares stdout and "
                         "silently truncated the list.")
    ap.add_argument("--max-exprs-per-file", type=int, default=2000)
    ap.add_argument("--enforce", action="store_true",
                    help="leave permission enforcement ON (default off: this "
                         "rig measures EVALUATION parity, not policy)")
    ap.add_argument("--out", default="corpus_gate.jsonl")
    return ap.parse_args(argv)


def find_files(roots, max_bytes, limit):
    # build_artifacts/installed trees hold COPIES of the same source
    # fixtures; counting them would triple-weight test data that is not
    # field content at all.
    skip = {".git", "build", "install", "node_modules", ".conda",
            "build_artifacts", "AddonManagerTest", "__pycache__"}
    found = []
    for root in roots:
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if d not in skip]
            for fn in filenames:
                if not fn.endswith(".FCStd"):
                    continue
                full = os.path.join(dirpath, fn)
                if "fcad-conda" in full or "freecad-rt-feedstock" in full:
                    continue
                try:
                    size = os.path.getsize(full)
                except OSError:
                    continue
                found.append((size, full))
    found.sort()
    kept = [(s, f) for s, f in found if s <= max_bytes]
    skipped = len(found) - len(kept)
    if limit:
        kept = kept[:limit]
    return kept, skipped


def collect_expressions(doc, sandbox):
    """[(owner, source, where, options)] for every stored expression.

    The options matter: each site evaluates with its own EvalOption
    mask, and comparing a cell without them compares something the
    sheet never does.  PropertyExpressionEngine uses OptionCallFrame;
    a spreadsheet adds OptionPythonMode when the sheet is in python
    mode, which changes the LEXER as well as name binding.
    """
    call_frame = getattr(sandbox, "OptionCallFrame", 1)
    python_mode = getattr(sandbox, "OptionPythonMode", 2)
    out = []
    for obj in doc.Objects:
        try:
            engine = obj.ExpressionEngine
        except Exception:
            engine = None
        if engine:
            for path, expr in engine:
                out.append((obj, expr, "engine:" + str(path), call_frame))
        if obj.isDerivedFrom("Spreadsheet::Sheet"):
            opts = call_frame
            try:
                if obj.PythonMode:
                    opts |= python_mode
            except Exception:
                pass
            try:
                cells = obj.cells.getUsedCells()
            except Exception:
                cells = []
            for addr in cells:
                try:
                    content = obj.getContents(addr)
                except Exception:
                    continue
                if content and content.startswith("="):
                    out.append((obj, content[1:], "cell:" + addr, opts))
    return out


def describe(value):
    """A comparable, printable form of an evaluated value."""
    if isinstance(value, float):
        return "float:%.12g" % value
    if isinstance(value, bool):
        return "bool:%r" % value
    if isinstance(value, int):
        return "int:%d" % value
    if isinstance(value, str):
        return "str:%s" % value
    try:
        import FreeCAD
        if isinstance(value, FreeCAD.Units.Quantity):
            return "quantity:%.12g:%s" % (value.Value, value.Unit)
        if isinstance(value, FreeCAD.Vector):
            return "vector:%.12g,%.12g,%.12g" % (value.x, value.y, value.z)
    except Exception:
        pass
    return "%s:%s" % (type(value).__name__, repr(value)[:120])


def error_text(exc):
    """An exception's MESSAGE, without the C++ source location.

    A FreeCAD exception's str() is a dict repr carrying sfile / iline /
    sfunction -- the throwing binary's own source path.  The image is a
    different binary and can never reproduce those, and they are not
    evaluation behaviour; comparing them would report a permanent false
    difference.  sErrMsg is the parity question.
    """
    args = getattr(exc, "args", None)
    if args and isinstance(args[0], dict):
        msg = args[0].get("sErrMsg")
        if msg:
            return "%s: %s" % (type(exc).__name__, msg)
    return "%s: %s" % (type(exc).__name__, exc)


def same(a, b):
    if a == b:
        return True
    # numeric tolerance: the two engines run the same C++ code, so this
    # only absorbs formatting, not real drift
    for prefix in ("float:", "int:", "quantity:"):
        if a.startswith(prefix) and b.startswith(prefix):
            try:
                fa = float(a.split(":")[1])
                fb = float(b.split(":")[1])
                return math.isclose(fa, fb, rel_tol=1e-12, abs_tol=1e-12)
            except (IndexError, ValueError):
                return False
    return False


def main(argv):
    args = parse_args(argv)
    sandbox = getattr(App, "ExpressionSandbox", None)
    if sandbox is None:
        print("FATAL: this build has no FreeCAD.ExpressionSandbox module")
        return 2
    # The host resolves the image from preferences; FCX_IMAGE/FCX_STDLIB
    # are the dev-box convention the C++ tests use, so honour them here
    # too rather than making every caller edit a parameter file.
    prefs = PrefGuard()
    sandboxGroup = "User parameter:BaseApp/Preferences/Expression/Sandbox"
    if os.environ.get("FCX_IMAGE"):
        prefs.set_string(sandboxGroup, "ImagePath", os.environ["FCX_IMAGE"])
    if os.environ.get("FCX_STDLIB"):
        prefs.set_string(sandboxGroup, "StdlibPath", os.environ["FCX_STDLIB"])
    if not sandbox.available():
        prefs.restore()
        print("FATAL: no sandbox image (set FCX_IMAGE / FCX_STDLIB, or the "
              "ImagePath/StdlibPath preferences)")
        return 2

    prefs.set_bool("User parameter:BaseApp/Preferences/Document",
                   "AutoSaveEnabled", False)
    prefs.set_bool("User parameter:BaseApp/Preferences/Expression/Security",
                   "Enforce", bool(args.enforce))
    prefs.set_bool(sandboxGroup, "Evaluate", True)
    if not sandbox.routed():
        prefs.restore()
        print("FATAL: routing did not switch on")
        return 2
    try:
        return sweep(args, sandbox)
    finally:
        prefs.restore()


def sweep(args, sandbox):

    files, skipped_big = find_files(
        args.roots, args.max_mb * 1024 * 1024, 0)
    if args.only:
        files = [f for f in files if args.only in f[1]]
    files = files[args.start:]
    if args.max_files:
        files = files[:args.max_files]
    print("corpus: %d files (%d skipped over %.1f MB)"
          % (len(files), skipped_big, args.max_mb), flush=True)
    if args.list_only:
        with open(args.list_only, "w") as f:
            for i, (size, path) in enumerate(files):
                f.write("%d\t%d\t%s\n" % (i + args.start, size, path))
        print("wrote %d entries to %s" % (len(files), args.list_only))
        return 0

    counts = {"same": 0, "differ": 0, "both_error": 0,
              "both_error_text_differs": 0, "image_only_error": 0,
              "native_only_error": 0, "expressions": 0, "files": 0,
              "files_failed": 0}
    gaps = {}
    started = time.time()
    with open(args.out, "w") as out:
        for size, path in files:
            doc = None
            try:
                doc = App.openDocument(path, True)
            except Exception as exc:
                counts["files_failed"] += 1
                out.write(json.dumps({"file": path, "open_error": str(exc)}) + "\n")
                continue
            counts["files"] += 1
            try:
                exprs = collect_expressions(doc, sandbox)[:args.max_exprs_per_file]
                for owner, source, where, opts in exprs:
                    counts["expressions"] += 1
                    rec = {"file": os.path.basename(path), "where": where,
                           "expr": source, "opts": opts}
                    try:
                        nat = describe(
                            sandbox.evaluateNative(owner, source, opts))
                        nat_err = None
                    except Exception as exc:
                        nat, nat_err = None, error_text(exc)
                    try:
                        img = describe(sandbox.evaluate(owner, source, opts))
                        img_err = None
                    except Exception as exc:
                        img, img_err = None, error_text(exc)

                    if nat_err and img_err:
                        # Both refusing is parity of BEHAVIOUR.  Parity
                        # of the reason is a separate question, and it
                        # is a real one: the image cannot see foreign
                        # documents, so it used to answer "Document not
                        # found" where the host knew a property was
                        # missing.  Count the mismatches instead of
                        # leaving them to be noticed by eye.
                        verdict = "both_error"
                        if nat_err != img_err:
                            counts["both_error_text_differs"] += 1
                            gaps["TEXT " + img_err.split("\n")[0][:150]] = \
                                gaps.get("TEXT " + img_err.split("\n")[0][:150], 0) + 1
                    elif img_err:
                        verdict = "image_only_error"
                        key = img_err.split("\n")[0][:160]
                        gaps[key] = gaps.get(key, 0) + 1
                    elif nat_err:
                        verdict = "native_only_error"
                    elif same(nat, img):
                        verdict = "same"
                    else:
                        verdict = "differ"
                    counts[verdict] += 1
                    if verdict != "same":
                        rec.update({"verdict": verdict, "native": nat,
                                    "image": img, "native_error": nat_err,
                                    "image_error": img_err})
                        out.write(json.dumps(rec) + "\n")
            except Exception:
                counts["files_failed"] += 1
                traceback.print_exc()
            finally:
                if doc is not None:
                    App.closeDocument(doc.Name)

    elapsed = time.time() - started
    lines = ["=== switch-over compatibility gate ==="]
    for key in ("files", "files_failed", "expressions", "same", "differ",
                "both_error", "both_error_text_differs", "image_only_error",
                "native_only_error"):
        lines.append("  %-18s %d" % (key, counts[key]))
    lines.append("  %-18s %.1f s" % ("elapsed", elapsed))
    if gaps:
        lines.append("")
        lines.append("image-only failures by message (the gap list):")
        for msg, n in sorted(gaps.items(), key=lambda kv: -kv[1])[:25]:
            lines.append("  %5d  %s" % (n, msg))
    lines.append("")
    lines.append("detail: %s" % args.out)
    report = "\n".join(lines)
    # FreeCADCmd drowns stdout in restore progress bars and does not
    # always flush at exit: the summary goes to a FILE as well.
    summary = args.out + ".summary"
    with open(summary, "w") as f:
        f.write(report + "\n")
    print(report, flush=True)
    return 0


if __name__ == "__main__":
    argv = sys.argv[1:]
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    # Flush first: SystemExit out of a FreeCADCmd `-c` skips the
    # shutdown that flushes a block-buffered stdout, and a piped run
    # then prints nothing at all while still exiting 0.
    _code = main(argv)
    sys.stdout.flush()
    sys.exit(_code)
