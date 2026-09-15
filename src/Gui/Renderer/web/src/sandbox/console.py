# The console's interpreter in the page's guest (docs/Sandbox.md 7.20, C4).
#
# Pushed into the guest once per boot as the module `fcx_console` (the exec
# op's `module`, web/src/sandbox/session.ts) and driven one line at a time
# through the eval op, so every statement enters the guest through fcx_call
# -- the entry the host bridge's suspending import needs -- and ends as one
# bridge statement.  pyodide's own pyodide.console.Console is not used: it
# runs code as an asyncio task on the web loop, which is not that entry.
#
# stdlib only: codeop for incomplete-input detection, rlcompleter for
# completion, traceback for the report.  Output goes to the guest's
# sys.stdout / sys.stderr, which the page shows as it is written.

import builtins
import codeop
import re
import rlcompleter
import sys
import traceback

FILENAME = "<console>"

# The desktop console's names.  FreeCADGui is the image's stand-in module;
# what a remote client may do with it is the host's catalog's business.
namespace = {"__name__": "__main__", "__doc__": None, "__builtins__": builtins}
for _name, _alias in (("FreeCAD", "App"), ("FreeCADGui", "Gui")):
    try:
        _mod = __import__(_name)
    except Exception:
        continue
    namespace[_name] = namespace[_alias] = _mod


class _Completer(rlcompleter.Completer):
    """rlcompleter, plus a host object's properties.  A proxy's dir() lists
    the members its facade declares; a property is not one of them -- it is
    read through __getattr__, answered by the host's property system -- so
    `obj.Leng` would complete to nothing.  PropertiesList names them."""

    def attr_matches(self, text):
        found = super().attr_matches(text)
        # The same shape rlcompleter evaluates: dotted names, no calls.
        m = re.match(r"(\w+(\.\w+)*)\.(\w*)$", text)
        if not m:
            return found
        expr, attr = m.group(1), m.group(3)
        try:
            names = eval(expr, self.namespace).PropertiesList
        except Exception:
            return found
        have = {f.rstrip("(") for f in found}
        for name in names:
            word = "%s.%s" % (expr, name)
            if isinstance(name, str) and name.startswith(attr) and word not in have:
                found.append(word)
        return found


_compile = codeop.CommandCompiler()
_completer = _Completer(namespace)
_buffer = []

# Word breaks for completion: readline's set, all non-alphanumerics but '.'.
_BREAKS = " \t\n`~!@#$%^&*()-=+[{]}\\|;:'\",<>/?"


def _report(skip_frames):
    """Print the exception being handled to stderr, without this module's
    own frames, as the desktop console shows it."""
    kind, value, tb = sys.exc_info()
    sys.last_type, sys.last_value, sys.last_traceback = kind, value, tb
    sys.last_exc = value
    for _ in range(skip_frames):
        if tb is not None:
            tb = tb.tb_next
    sys.stderr.write("".join(traceback.format_exception(kind, value, tb)))
    sys.stderr.flush()


def push(line):
    """One line of input.  Returns "more" while the statement is incomplete,
    "syntax" when it does not compile, "error" when it raised, "ok" when it
    ran.  A line may carry newlines of its own (a paste)."""
    _buffer.append(line)
    source = "\n".join(_buffer)
    try:
        code = _compile(source, FILENAME, "single")
    except (OverflowError, SyntaxError, ValueError):
        _buffer.clear()
        kind, value, _ = sys.exc_info()
        sys.stderr.write("".join(traceback.format_exception_only(kind, value)))
        sys.stderr.flush()
        return "syntax"
    if code is None:
        return "more"
    _buffer.clear()
    try:
        exec(code, namespace)
    except SystemExit:
        sys.stderr.write("SystemExit is ignored in the console\n")
        return "error"
    except BaseException:
        _report(1)
        return "error"
    finally:
        sys.stdout.flush()
        sys.stderr.flush()
    return "ok"


def reset():
    """Drop a half-typed statement."""
    _buffer.clear()


def complete(source):
    """Completions for the word at the end of `source`: (list, start) where
    `start` is the index in `source` the completions replace from."""
    start = max(map(source.rfind, _BREAKS)) + 1
    word = source[start:]
    try:
        if "." in word:
            found = _completer.attr_matches(word)
        else:
            found = _completer.global_matches(word)
    except Exception:
        found = []
    return [sorted(set(found)), start]
