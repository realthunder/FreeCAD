# ninja-guard -- a sample Claude Code hook, opt-in per box

Ninja takes no lock on a build directory. Two ninja processes in one
tree interleave their writes to `.ninja_log` and `.ninja_deps` and
corrupt them; the next run says

    ninja: warning: premature end of file; recovering

then discards the records it could not read and rebuilds everything
whose dependency entry it lost. That has cost this project a
from-scratch rebuild more than once. When it happens, the repair is

    ninja -C <build dir> -t recompact

after which the following build is clean. Run that after ANY
interrupted or stacked build before trusting an incremental one.

This directory holds a `PreToolUse` hook that refuses to start a second
ninja in a directory that already has one. **It is a sample, not a
policy.** It was enforced repo-wide for a while and that was a mistake:
what it can observe differs so much per platform that the same script
is a hard `deny` on one box, a name-only `ask` on another, and blind to
the way a third box builds. It is worth having where it works; it is
not worth forcing on anyone.

## To use it on your box

Copy the four files into `.claude/hooks/` in your checkout (that
directory is git-ignored, so it stays yours) and register the hook in
`.claude/settings.json`:

    {
      "hooks": {
        "PreToolUse": [
          {
            "matcher": "Bash",
            "hooks": [
              {
                "type": "command",
                "if": "Bash(*ninja*)",
                "command": "bash \"${CLAUDE_PROJECT_DIR:-.}/.claude/hooks/ninja-guard.sh\"",
                "timeout": 20,
                "statusMessage": "Checking for a running ninja build"
              }
            ]
          }
        ]
      }
    }

`selftest.sh` exercises the command-line parser and needs no build.
The blocking path needs a live build and a platform the guard can
enumerate.

## What it can and cannot see, per platform

The verdict follows the strength of the evidence:

- **Linux, macOS** -- a running process's working directory is
  readable (`/proc/<pid>/cwd`, or `lsof`), and ninja `chdir()`s into
  its build directory, so the match is on resolved real paths and the
  verdict is `deny`. Verified on macOS against a live build: a second
  build of the same directory is denied, another build directory in
  the same repo is allowed, and the same directory NAME in a different
  repo is allowed.
- **Windows** -- `Win32_Process` carries the command line but not the
  working directory, so the match falls back to comparing the build
  directory's BASENAME and the verdict is `ask`. That will also fire
  for a build of a same-named directory in a DIFFERENT repository.
- **Anywhere it cannot list processes** -- it prints a message saying
  it is not checking, rather than failing open in silence.

## Known holes (all verified, none fixed)

1. `cmake --build <dir>` is not recognized on either side. The hook's
   parser only knows `ninja`; and `cmake --build` does not pass `-C`,
   it chdirs and runs ninja there, so on Windows -- where the cwd is
   unreachable -- the running build's directory cannot be recovered at
   all. Since that is how this project is normally built there, the
   guard is inert on the Windows box. Reading `--build <dir>` off the
   parent `cmake.exe` command line would recover it, still only as an
   `ask`.
2. `cd <build dir> && ninja` is not matched: the segment after `&&` is
   a bare `ninja`, whose directory reads as `.`, which resolves against
   the shell's starting directory rather than the one it just entered.
3. The Windows basename match is a cross-repository false positive, as
   above.

Anyone adopting this should read those first and decide whether the
protection is worth the noise on their platform.
