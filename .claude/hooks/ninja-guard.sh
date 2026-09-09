#!/usr/bin/env bash
#
# PreToolUse guard: refuse a `ninja -C <dir>` when another ninja is
# already building in that same directory.
#
# Ninja takes no lock on its build directory. Two invocations interleave
# writes to .ninja_log and .ninja_deps; the next run then reports
# "premature end of file; recovering" and redoes work it had already
# done. This box has paid for that twice, once with a from-scratch
# rebuild.
#
# Two things about HOW it decides, both of which were bugs first:
#
# 1. It only treats `ninja` as an invocation when the word is in COMMAND
#    POSITION -- first token of a pipeline segment, after shell wrappers
#    and VAR=value assignments. A build command mentioned inside a
#    string is not a build. The first version matched `ninja` after any
#    whitespace, so `grep -c "ninja -C build/x" log` parsed as an
#    invocation and got blocked; it blocked its own test command.
#
# 2. It never greps the process table for the command text. The natural
#    way to look for a running build -- `pgrep -f "ninja -C <dir>"` --
#    matches the checking process's OWN command line and so can never
#    report "nothing running". That mistake cost this project six hours.
#    Discovery here is `pgrep -x ninja`, matching the executable NAME,
#    after which each candidate PID's own arguments are read. The
#    command under test arrives on stdin and never appears in any argv.
#
# stdin : the PreToolUse hook payload
# stdout: nothing (allow), or a PreToolUse deny/ask decision
exec /usr/bin/env python3 -c '
import json, os, re, shlex, subprocess, sys

ASSIGN = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*=")
OPERATORS = {";", "&&", "||", "|", "&", "(", ")", "\n"}
# Commands that RUN another command, so ninja after them is still a
# build. Anything not listed leads its segment itself -- grep, echo, ps,
# rg, awk and friends land here, which is the point.
WRAPPERS = {"env", "sudo", "doas", "nice", "time", "command", "exec",
            "stdbuf", "nohup", "setarch", "taskset", "ionice"}
SHELLS = {"sh", "bash", "zsh", "dash", "ksh"}


def out(decision, reason):
    json.dump({"hookSpecificOutput": {
        "hookEventName": "PreToolUse",
        "permissionDecision": decision,
        "permissionDecisionReason": reason,
    }}, sys.stdout)
    sys.stdout.write("\n")
    sys.exit(0)


def tokenize(s):
    lex = shlex.shlex(s, posix=True, punctuation_chars=True)
    lex.whitespace_split = True
    return list(lex)


def build_dir_of(args):
    """The -C of a ninja argument list, or "." for a build in the cwd."""
    i = 0
    while i < len(args):
        a = args[i]
        if a == "-C" or a == "--directory":
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
        base = os.path.basename(t[i])
        if base == "ninja":
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


try:
    payload = json.load(sys.stdin)
except Exception:
    sys.exit(0)              # unreadable payload: never block
cmd = (payload.get("tool_input") or {}).get("command") or ""
wanted = ninja_dirs(cmd)
if not wanted:
    sys.exit(0)

cwd = payload.get("cwd") or os.getcwd()


def proc_cwd(pid):
    # A running ninja CHDIRS INTO its -C target, so its cwd IS the build
    # directory whatever -C said and wherever it was launched from. That
    # is the authoritative answer, and it is why this asks lsof rather
    # than re-resolving the -C string: resolving "-C build/x" against a
    # cwd that is already .../build/x yields .../build/x/build/x, which
    # matches nothing. Measured -- that let a live build through.
    try:
        o = subprocess.run(["/usr/sbin/lsof", "-a", "-d", "cwd", "-Fn",
                            "-p", str(pid)], capture_output=True,
                           text=True, timeout=5).stdout
    except Exception:
        return None
    for line in o.splitlines():
        if line.startswith("n"):
            return line[1:]
    return None


try:
    pids = subprocess.run(["/usr/bin/pgrep", "-x", "ninja"],
                          capture_output=True, text=True,
                          timeout=5).stdout.split()
except Exception:
    sys.exit(0)              # cannot tell: do not block

running = []
for pid in pids:
    if not pid.isdigit():
        continue
    try:
        args = subprocess.run(["/bin/ps", "-o", "args=", "-p", pid],
                              capture_output=True, text=True,
                              timeout=5).stdout.strip()
    except Exception:
        continue
    if not args:
        continue
    d = build_dir_of(args.split()[1:])
    running.append((pid, d, proc_cwd(pid) or "", args))

for want in wanted:
    mine = os.path.realpath(os.path.join(cwd, want))
    for pid, theirs, their_cwd, args in running:
        other = os.path.realpath(their_cwd) if their_cwd else None
        exact = other is not None and mine == other
        loose = other is None and \
            os.path.basename(theirs.rstrip("/")) == \
            os.path.basename(want.rstrip("/"))
        if exact or loose:
            out("deny" if exact else "ask",
                "A ninja build is ALREADY RUNNING in this build directory "
                "(pid %s: %s).\n\n"
                "Ninja takes no lock on a build dir. A second invocation "
                "interleaves writes to .ninja_log and .ninja_deps, which "
                "corrupts them -- the next run says \"premature end of "
                "file; recovering\" and redoes work. This has happened "
                "twice on this machine.\n\n"
                "If you edited sources mid-build and want them picked up: "
                "STOP the running build first (TaskStop, then confirm "
                "with ps -- the task row disappearing is not evidence the "
                "process died), then start ONE build."
                % (pid, args[:200]))
sys.exit(0)
'
