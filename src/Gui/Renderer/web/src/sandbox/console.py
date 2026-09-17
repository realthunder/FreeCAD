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
import keyword
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
    """rlcompleter's namespace and decoration, with our own match.

    rlcompleter matches a case-sensitive PREFIX: `app.` and `fre` answer
    nothing while `App.` works, and from a phone an empty answer is
    indistinguishable from a trigger that never fired (docs/Sandbox.md
    7.25).  Ruled 2026-09-17: any keyword, ignoring case -- so the
    candidates are enumerated here and matched anywhere in the name,
    prefixes first then alphabetical, the ranking the page's own
    narrowing (widgets/complete.ts filterSet) already uses.  The
    decoration is rlcompleter's, reproduced: a callable gets `(`, and
    `)` too when it takes nothing; a keyword a trailing space, except
    the ones that end a statement; `try` and `finally` a colon.  Names
    starting with `_` stay shelved unless the typed word starts with one
    -- with contains-matching, `init` would otherwise surface `__init__`.

    Plus a host object's properties: a proxy's dir() lists the members its
    facade declares; a property is not one of them -- it is read through
    __getattr__, answered by the host's property system -- so `obj.Leng`
    would complete to nothing.  PropertiesList names them."""

    def _rank(self, text, names):
        """The names matching `text` anywhere, case-insensitive: prefix
        matches first, then the rest, each run alphabetical."""
        needle = text.lower()
        shelved = not text.startswith("_")
        head, tail = [], []
        for name in sorted(set(names)):
            if not isinstance(name, str):
                continue
            if shelved and name.startswith("_"):
                continue
            low = name.lower()
            if low.startswith(needle):
                head.append(name)
            elif needle in low:
                tail.append(name)
        return head + tail

    def _keyword(self, word):
        if word in {"False", "None", "True", "break", "continue", "pass", "else", "_"}:
            return word
        if word in {"try", "finally"}:
            return word + ":"
        return word + " "

    def global_matches(self, text):
        found = [self._keyword(k) for k in self._rank(text, keyword.kwlist)]
        found += [self._keyword(k) for k in self._rank(text, keyword.softkwlist)]
        names = []
        for nspace in (self.namespace, builtins.__dict__):
            names.extend(nspace.keys())
        for word in self._rank(text, names):
            val = self.namespace.get(word, builtins.__dict__.get(word))
            found.append(self._callable_postfix(val, word))
        return found

    def attr_matches(self, text):
        # The same shape rlcompleter evaluates: dotted names, no calls.
        m = re.match(r"(\w+(\.\w+)*)\.(\w*)$", text)
        if not m:
            return []
        expr, attr = m.group(1), m.group(3)
        try:
            obj = eval(expr, self.namespace)
        except Exception:
            return []
        names = set()
        try:
            names.update(dir(obj))
        except Exception:
            pass
        if isinstance(obj, type):
            names.update(rlcompleter.get_class_members(obj))
        elif hasattr(obj, "__class__"):
            names.add("__class__")
            try:
                names.update(rlcompleter.get_class_members(obj.__class__))
            except Exception:
                pass
        try:
            for name in obj.PropertiesList:
                names.add(name)
        except Exception:
            pass
        found = []
        for word in self._rank(attr, names):
            try:
                val = getattr(obj, word)
            except Exception:
                found.append("%s.%s" % (expr, word))
                continue
            found.append(self._callable_postfix(val, "%s.%s" % (expr, word)))
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
    `start` is the index in `source` the completions replace from.  The
    list is ranked (prefix matches first), not sorted: the order IS the
    answer, and the page keeps it."""
    start = max(map(source.rfind, _BREAKS)) + 1
    word = source[start:]
    try:
        if "." in word:
            found = _completer.attr_matches(word)
        else:
            found = _completer.global_matches(word)
    except Exception:
        found = []
    seen = set()
    ranked = []
    for item in found:
        if item not in seen:
            seen.add(item)
            ranked.append(item)
    return [ranked, start]
