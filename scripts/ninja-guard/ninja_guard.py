"""Refuse a `ninja -C <dir>` when another ninja is already building there.

Ninja takes no lock on its build directory. Two invocations interleave
writes to .ninja_log and .ninja_deps; the next run then reports
"premature end of file; recovering" and redoes work it had already done.
The macOS box has paid for that twice, once with a from-scratch rebuild.

Three things about HOW it decides, all of which were bugs first:

1. `ninja` counts only in COMMAND POSITION -- first token of a pipeline
   segment, after VAR=value assignments and a whitelist of wrappers. A
   build command MENTIONED IN A STRING is not a build. The first version
   matched the word after any whitespace, so
   `grep -c "ninja -C build/x" log` parsed as an invocation and was
   blocked; it blocked its own test command, and it would have blocked
   exactly the diagnostics you reach for while a build runs.

2. It never greps the process table for the command text. The natural
   `pgrep -f "ninja -C <dir>"` matches the checking process's OWN
   command line and so can never report "nothing running" -- that cost
   six hours. Discovery is by executable NAME, after which each
   candidate PID's own arguments are read, and the command under test
   arrives on stdin so it appears in no argv.

3. A running ninja CHDIRS INTO its -C target, so its cwd IS the build
   directory whatever -C said. Re-resolving its -C string against that
   cwd yields .../build/x/build/x and matches nothing -- measured, it
   let a live build through.

stdin : the PreToolUse hook payload
stdout: nothing (allow), or a PreToolUse decision
"""
import json
import os
import re
import shlex
import shutil
import subprocess
import sys

ASSIGN = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*=")
OPERATORS = {";", "&&", "||", "|", "&", "(", ")", "\n"}
# Commands that RUN another command, so ninja after them is still a
# build. Anything not listed leads its own segment -- grep, echo, ps,
# rg, awk and friends land there, which is the point.
WRAPPERS = {"env", "sudo", "doas", "nice", "time", "command", "exec",
            "stdbuf", "nohup", "setarch", "taskset", "ionice"}
SHELLS = {"sh", "bash", "zsh", "dash", "ksh"}
NINJA = {"ninja", "ninja.exe"}
TIMEOUT = 8


def out(decision, reason):
    json.dump({"hookSpecificOutput": {
        "hookEventName": "PreToolUse",
        "permissionDecision": decision,
        "permissionDecisionReason": reason,
    }}, sys.stdout)
    sys.stdout.write("\n")
    sys.exit(0)


def run(argv):
    try:
        return subprocess.run(argv, capture_output=True, text=True,
                              timeout=TIMEOUT).stdout
    except Exception:
        return None


def tokenize(s):
    lex = shlex.shlex(s, posix=True, punctuation_chars=True)
    lex.whitespace_split = True
    return list(lex)


def build_dir_of(args):
    """The -C of a ninja argument list, or "." for a build in the cwd."""
    i = 0
    while i < len(args):
        a = args[i]
        if a in ("-C", "--directory"):
            return args[i + 1] if i + 1 < len(args) else "."
        if a.startswith("-C") and len(a) > 2:
            return a[2:]
        if a.startswith("--directory="):
            return a.split("=", 1)[1]
        i += 1
    return "."


def ninja_dirs(cmd, depth=0):
    """Build directories this command line would actually BUILD in."""
    if depth > 3:
        return []
    try:
        tokens = tokenize(cmd)
    except ValueError:
        return []            # unbalanced quotes: cannot judge, do not block
    found, segment = [], []
    for tok in tokens + [";"]:
        if tok in OPERATORS:
            found += _segment_dirs(segment, depth)
            segment = []
        else:
            segment.append(tok)
    return found


def _segment_dirs(t, depth):
    i = 0
    while i < len(t) and ASSIGN.match(t[i]):
        i += 1
    while i < len(t):
        base = os.path.basename(t[i]).lower()
        if base in NINJA:
            return [build_dir_of(t[i + 1:])]
        if base in SHELLS:
            # sh -c "<command>": the real command is inside the string.
            j, saw_c = i + 1, False
            while j < len(t) and t[j].startswith("-"):
                if "c" in t[j]:
                    saw_c = True
                j += 1
            if saw_c and j < len(t):
                return ninja_dirs(t[j], depth + 1)
            return []
        if base in WRAPPERS or base.endswith(".sh"):
            i += 1
            while i < len(t) and (t[i].startswith("-") or ASSIGN.match(t[i])):
                i += 1
            continue
        return []            # something else leads this segment
    return []


# ---------------------------------------------------------------------
# Finding the builds that are already running.
#
# Three arms, because the exactness differs and that changes the verdict.
# Linux and macOS can name a process's CWD, which IS the build directory
# for a running ninja, so they can match exactly and DENY. Windows cannot
# reach another process's CWD without opening it, so it falls back to
# comparing the -C basename and can only ASK. Saying "ask" where the
# evidence is weaker is the honest answer; pretending to a deny is not.
# ---------------------------------------------------------------------

def running_builds_posix():
    pgrep = shutil.which("pgrep") or "/usr/bin/pgrep"
    o = run([pgrep, "-x", "ninja"])
    if o is None:
        return None                      # cannot tell
    found = []
    for pid in o.split():
        if not pid.isdigit():
            continue
        args = cwd = None
        if sys.platform.startswith("linux"):
            try:
                with open("/proc/%s/cmdline" % pid, "rb") as fh:
                    args = fh.read().replace(b"\0", b" ").decode(
                        "utf-8", "replace").strip()
                cwd = os.readlink("/proc/%s/cwd" % pid)
            except Exception:
                pass
        if args is None:
            ps = shutil.which("ps") or "/bin/ps"
            a = run([ps, "-o", "args=", "-p", pid])
            args = a.strip() if a else None
        if cwd is None:
            lsof = shutil.which("lsof") or "/usr/sbin/lsof"
            o2 = run([lsof, "-a", "-d", "cwd", "-Fn", "-p", pid])
            if o2:
                for line in o2.splitlines():
                    if line.startswith("n"):
                        cwd = line[1:]
                        break
        if args:
            found.append((pid, args, cwd))
    return found


def running_builds_windows():
    """Win32_Process carries the full command line; CWD is not reachable."""
    ps = shutil.which("powershell") or shutil.which("pwsh")
    if ps:
        o = run([ps, "-NoProfile", "-NonInteractive", "-Command",
                 "Get-CimInstance Win32_Process -Filter "
                 "\"Name='ninja.exe'\" | "
                 "ForEach-Object { \"$($_.ProcessId)`t$($_.CommandLine)\" }"])
        if o is not None:
            found = []
            for line in o.splitlines():
                if "\t" not in line:
                    continue
                pid, args = line.split("\t", 1)
                if pid.strip().isdigit() and args.strip():
                    found.append((pid.strip(), args.strip(), None))
            return found
    wmic = shutil.which("wmic")
    if wmic:
        o = run([wmic, "process", "where", "name='ninja.exe'",
                 "get", "ProcessId,CommandLine", "/format:csv"])
        if o is not None:
            found = []
            for line in o.splitlines()[1:]:
                parts = line.strip().split(",")
                if len(parts) >= 3 and parts[-1].strip().isdigit():
                    found.append((parts[-1].strip(),
                                  ",".join(parts[1:-1]).strip(), None))
            return found
    return None                          # cannot tell


def main():
    try:
        payload = json.load(sys.stdin)
    except Exception:
        return 0                         # unreadable payload: never block
    cmd = (payload.get("tool_input") or {}).get("command") or ""
    wanted = ninja_dirs(cmd)
    if not wanted:
        return 0
    cwd = payload.get("cwd") or os.getcwd()

    running = (running_builds_windows() if os.name == "nt"
               else running_builds_posix())
    if running is None:
        # No way to look. Say so rather than allow in silence -- a guard
        # that quietly stops guarding is worse than no guard, because
        # nothing tells you the protection is gone.
        json.dump({"systemMessage":
                   "ninja guard: cannot list processes on this system, so"
                   " it is NOT checking for a second build in the same"
                   " directory. Stop a running build before starting"
                   " another."}, sys.stdout)
        sys.stdout.write("\n")
        return 0

    for want in wanted:
        mine = os.path.realpath(os.path.join(cwd, want))
        for pid, args, their_cwd in running:
            theirs = build_dir_of(args.split()[1:])
            other = os.path.realpath(their_cwd) if their_cwd else None
            exact = other is not None and mine == other
            loose = other is None and \
                os.path.basename(theirs.rstrip("/\\")).lower() == \
                os.path.basename(want.rstrip("/\\")).lower()
            if exact or loose:
                out("deny" if exact else "ask",
                    "A ninja build is ALREADY RUNNING in this build"
                    " directory (pid %s: %s).%s\n\n"
                    "Ninja takes no lock on a build dir. A second"
                    " invocation interleaves writes to .ninja_log and"
                    " .ninja_deps, which corrupts them -- the next run"
                    " says \"premature end of file; recovering\" and"
                    " redoes work. This has happened twice on the macOS"
                    " box.\n\n"
                    "If you edited sources mid-build and want them picked"
                    " up: STOP the running build first (TaskStop, then"
                    " confirm with the process list -- the task row"
                    " disappearing is not evidence the process died),"
                    " then start ONE build."
                    % (pid, args[:200],
                       "" if exact else
                       "\n\nMatched on the directory NAME only: this"
                       " platform cannot read another process's working"
                       " directory, so it may be a different tree."))
    return 0


if __name__ == "__main__":
    sys.exit(main())
