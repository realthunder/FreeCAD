# SPDX-License-Identifier: LGPL-2.1-or-later
"""MCP-conformant debug console server for FreeCAD.

Exposes a ``run_python`` tool over the Model Context Protocol (Streamable
HTTP transport) so an AI agent can drive a live, running FreeCAD process as a
persistent Python REPL, plus a ``search_api`` tool that keyword-searches the
live Python API surface (names + docstrings) so the agent can DISCOVER entry
points it does not know exist instead of guessing.

Everything in FreeCAD is already reachable from Python, so this server does not
try to wrap individual operations as separate tools.  The console tool's
description teaches the main entry points; the search tool covers the long
tail.

Design constraints:

* All document / OCCT / Coin work MUST run on FreeCAD's main thread; the HTTP
  server necessarily accepts requests on a background thread.  Each tool call is
  therefore marshalled onto the main thread through the Qt event loop (the same
  approach ``Web::AppServer`` uses in ``src/Mod/Web/App/Server.cpp``) and the
  worker thread blocks until the main thread has produced a result.
* The server (uvicorn) runs on its own daemon thread with its own asyncio loop,
  leaving FreeCAD's Qt event loop untouched.

Usage from the FreeCAD Python console (main thread)::

    from freecad import mcp_console
    mcp_console.start()            # http://127.0.0.1:8765/mcp
    mcp_console.stop()

The Tools menu carries a checkable "MCP Server" action driving the same two
calls, and remembers the state in ``App::DocumentParams::MCPServerAutoStart`` so
the server comes back up on the next start.
"""

import ast
import collections
import io
import os
import sys
import threading
import time
import traceback
from typing import Optional, TypedDict

__all__ = ["start", "stop", "is_running", "url", "log_path", "get_log"]


class RunResult(TypedDict):
    """Structured result of a ``run_python`` call (drives the MCP outputSchema)."""

    stdout: str
    stderr: str
    console: list             # FreeCAD console messages emitted during the run,
                              # each "Level: message" (Error/Warning/Message/Log/...)
    result: Optional[str]     # repr of the final expression, if code was one
    exception: Optional[str]  # traceback string if the code raised


class ApiMatch(TypedDict):
    """One hit of a ``search_api`` call."""

    name: str  # qualified name, e.g. "FreeCADGui.Selection.setPreselection"
    kind: str  # module / class / method / function / property / ...
    doc: str   # first lines of the docstring (usually the signature)


class SearchResult(TypedDict):
    """Structured result of a ``search_api`` call."""

    matches: list[ApiMatch]
    truncated: bool  # more matches existed than the limit


class LogResult(TypedDict):
    """Structured result of a ``get_log`` call."""

    lines: list[str]        # captured console lines, newest last
    total_captured: int     # lines currently in the ring buffer
    matched: int            # lines matching the filters, before the limit
    truncated: bool         # the limit dropped some matches
    path: Optional[str]     # file the same lines are being appended to

_DEFAULT_HOST = "127.0.0.1"
_DEFAULT_PORT = 8765

# Persistent REPL namespace, shared across every run_python call.
_namespace: dict = {}

_executor = None
_server_thread = None
_mcp = None
_uvicorn = None          # the uvicorn.Server, when we were able to drive it ourselves
_bound = (_DEFAULT_HOST, _DEFAULT_PORT)

# --- session-long console capture -------------------------------------------
# The per-run observer in _exec_code() only sees what a run_python call itself
# emits. Everything interesting during a document load, a recompute kicked off
# from the GUI, or a crash happens outside any call -- so keep a second observer
# attached for as long as the server runs.
_LOG_RING_SIZE = 20000
# Disk is bounded at (_LOG_KEEP + 1) * _LOG_MAX_BYTES. Each start() rolls the
# current file down one slot, so the last _LOG_KEEP sessions stay readable --
# which is the point: the session you want to look at is usually the one that
# just died, not the one you are in.
_LOG_MAX_BYTES = 10 * 1024 * 1024
_LOG_KEEP = 5
_log_ring: collections.deque = collections.deque(maxlen=_LOG_RING_SIZE)
_log_lock = threading.Lock()
_log_observer = None
_log_fh = None
_log_path = None
_log_unflushed = 0
# Monotonic count of every line ever captured. The ring drops old entries, so
# indices into it are not stable; this is what run_python uses to work out which
# lines its own code produced.
_log_total = 0
# Levels worth paying the flush syscall for -- a crash right after one of these
# is exactly when the tail of the file matters.
_LOG_FLUSH_LEVELS = ("Error", "Critical", "Warning")


# --------------------------------------------------------------------------
# Main-thread execution
# --------------------------------------------------------------------------

class _Job:
    __slots__ = ("fn", "value", "error", "done")

    def __init__(self, fn):
        self.fn = fn
        self.value = None
        self.error = None
        self.done = threading.Event()


def _make_executor():
    """Build a QObject that runs callables on the Qt main thread.

    Imported lazily so the module can be imported outside a Qt process.
    """
    from PySide6 import QtCore

    def _app():
        return QtCore.QCoreApplication.instance()

    class _MainThreadExecutor(QtCore.QObject):
        _post = QtCore.Signal(object)

        def __init__(self):
            super().__init__()
            app = _app()
            if app is not None:
                self.moveToThread(app.thread())
            self._post.connect(self._on_job, QtCore.Qt.ConnectionType.QueuedConnection)

        @QtCore.Slot(object)
        def _on_job(self, job):
            try:
                job.value = job.fn()
            except BaseException as exc:  # noqa: BLE001 - forwarded to caller
                job.error = exc
            finally:
                job.done.set()

        def run_on_main(self, fn, timeout=None):
            app = _app()
            if app is not None and QtCore.QThread.currentThread() == app.thread():
                return fn()  # already on the main thread
            job = _Job(fn)
            self._post.emit(job)
            if not job.done.wait(timeout):
                raise TimeoutError("main-thread execution timed out")
            if job.error is not None:
                raise job.error
            return job.value

    return _MainThreadExecutor()


# --------------------------------------------------------------------------
# The console
# --------------------------------------------------------------------------

def _seed_namespace():
    ns = _namespace
    ns.setdefault("__name__", "__mcp_console__")
    try:
        import FreeCAD
        ns["FreeCAD"] = ns["App"] = FreeCAD
    except Exception:
        pass
    try:
        import FreeCADGui
        ns["FreeCADGui"] = ns["Gui"] = FreeCADGui
    except Exception:
        pass


def _exec_code(code: str) -> RunResult:
    """Run *code* in the persistent namespace, capturing output.

    Runs the statements for their side effects; if the LAST top-level node is an
    expression (REPL convention), its value's ``repr`` is returned and bound to
    ``_``. Exceptions are captured as a traceback string, never raised out.
    """
    out, err = io.StringIO(), io.StringIO()
    old_out, old_err = sys.stdout, sys.stderr
    sys.stdout, sys.stderr = out, err
    result_repr = None
    exception = None

    # FreeCAD.Console output (PrintError & co., and C++ Base::Console messages)
    # does not pass through sys.stdout/stderr — it fans out to Base::ILogger
    # observers. The session-long observer attached by start() is already
    # capturing all of it, so take a slice of that rather than attaching a
    # second observer per call.
    #
    # Attaching and detaching around every call used to be how this worked, and
    # it is what turned an unsynchronised observer list in Base/Console.cpp into
    # a reliable crash: TechDraw logs OCCT failures from QtConcurrent worker
    # threads, and detaching freed the observer under a thread that was already
    # calling into it. Console.cpp now defers that destruction (RetireObserver),
    # but not churning the observer list per call is the better fix.
    with _log_lock:
        console_mark = _log_total
    try:
        module = ast.parse(code, "<mcp-console>", "exec")
        last_expr = None
        if module.body and isinstance(module.body[-1], ast.Expr):
            last_expr = ast.Expression(module.body.pop().value)
        if module.body:
            exec(compile(module, "<mcp-console>", "exec"), _namespace)
        if last_expr is not None:
            value = eval(compile(last_expr, "<mcp-console>", "eval"), _namespace)
            if value is not None:
                _namespace["_"] = value
                try:
                    result_repr = repr(value)
                except Exception:
                    result_repr = object.__repr__(value)
    except BaseException:  # noqa: BLE001 - reported to the agent, not raised
        exception = traceback.format_exc()
    finally:
        sys.stdout, sys.stderr = old_out, old_err

    with _log_lock:
        added = _log_total - console_mark
        console_msgs = list(_log_ring)[-added:] if added > 0 else []
    return {
        "stdout": out.getvalue(),
        "stderr": err.getvalue(),
        "console": console_msgs,
        "result": result_repr,
        "exception": exception,
    }


# --------------------------------------------------------------------------
# API search
# --------------------------------------------------------------------------

# Roots always offered to search_api; workbench modules join once imported
# (searching imports the common ones on demand).
_API_ROOTS = ("FreeCAD", "FreeCADGui")
_API_COMMON = (
    "Part", "PartDesign", "Sketcher", "Draft", "Mesh", "TechDraw",
    "Spreadsheet", "Import", "Points",
)


def _first_doc(obj, limit=200) -> str:
    doc = getattr(obj, "__doc__", None) or ""
    doc = doc.strip()
    if len(doc) > limit:
        doc = doc[:limit].rsplit(None, 1)[0] + "..."
    return doc


def _search_api(query: str, modules=None, limit: int = 30) -> SearchResult:
    """Keyword search over the live FreeCAD Python API.

    Walks module attributes (one submodule level deep, e.g.
    ``FreeCADGui.Selection``) and class members, matching every
    whitespace-separated term of *query* case-insensitively against the
    qualified name plus the docstring head.  Name hits rank before
    doc-only hits.
    """
    import importlib

    terms = [t.lower() for t in query.split() if t]
    roots = []
    seen_roots = set()

    def add_root(name):
        if name in seen_roots:
            return
        seen_roots.add(name)
        mod = sys.modules.get(name)
        if mod is None:
            try:
                mod = importlib.import_module(name)
            except Exception:
                return
        roots.append((name, mod))

    for name in _API_ROOTS:
        add_root(name)
    for name in modules or _API_COMMON:
        add_root(name)

    scored = []  # (rank, qualified name, kind, doc)
    seen_objs = set()
    # Keep every visited child alive: dedup is by id(), and a freed
    # transient attribute (dynamic module attrs like Gui.ActiveDocument
    # create one per access) would let a later object reuse its id and
    # be skipped wrongly.
    keepalive = []
    # Pre-mark the root modules so aliases inside other roots (e.g.
    # FreeCAD.Gui is the FreeCADGui module) don't consume the module at
    # a depth too shallow to walk its members — the root visit below
    # bypasses this set.
    for _, mod in roots:
        seen_objs.add(id(mod))

    def consider(qual, obj, kind):
        leaf = qual.rsplit(".", 1)[-1].lower()
        doc = _first_doc(obj)
        hay = qual.lower() + "\n" + doc.lower()
        if not all(t in hay for t in terms):
            return
        rank = 0 if all(t in leaf for t in terms) else \
            1 if all(t in qual.lower() for t in terms) else 2
        scored.append((rank, qual, kind, doc))

    def visit_class(qual, cls):
        consider(qual, cls, "class")
        for attr, member in vars(cls).items():
            if attr.startswith("_"):
                continue
            kind = "property" if isinstance(member, property) else \
                "method" if callable(member) or isinstance(
                    member, (classmethod, staticmethod)) else "attribute"
            consider(qual + "." + attr, member, kind)

    def visit_module(qual, mod, depth):
        import types
        consider(qual, mod, "module")
        for attr in dir(mod):
            if attr.startswith("_"):
                continue
            try:
                child = getattr(mod, attr)
            except Exception:
                continue
            key = id(child)
            if key in seen_objs:
                continue
            seen_objs.add(key)
            keepalive.append(child)
            cqual = qual + "." + attr
            if isinstance(child, types.ModuleType):
                if depth < 1:
                    visit_module(cqual, child, depth + 1)
                else:
                    consider(cqual, child, "module")
            elif isinstance(child, type):
                visit_class(cqual, child)
            elif callable(child):
                consider(cqual, child, "function")
            else:
                consider(cqual, child, "attribute")

    for name, mod in roots:
        visit_module(name, mod, 0)

    # Live-instance types: much of the GUI/document API hangs off
    # instances (the 3D view's camera methods, document methods, view
    # providers) whose classes are not module attributes — index the
    # types of the well-known live objects when they exist.
    def live_types():
        out = []
        try:
            import FreeCAD
            doc = FreeCAD.ActiveDocument
            if doc is not None:
                out.append(("App.ActiveDocument", type(doc)))
                if doc.Objects:
                    obj = doc.Objects[0]
                    out.append(("App.ActiveDocument.Objects[0]", type(obj)))
                    if getattr(obj, "ViewObject", None) is not None:
                        out.append(("Objects[0].ViewObject",
                                    type(obj.ViewObject)))
        except Exception:
            pass
        try:
            import FreeCADGui
            gdoc = FreeCADGui.ActiveDocument
            if gdoc is not None:
                out.append(("Gui.ActiveDocument", type(gdoc)))
                view = gdoc.ActiveView
                if view is not None:
                    out.append(("Gui.ActiveDocument.ActiveView", type(view)))
        except Exception:
            pass
        return out

    for qual, cls in live_types():
        if id(cls) in seen_objs:
            continue
        seen_objs.add(id(cls))
        keepalive.append(cls)
        visit_class(qual, cls)

    scored.sort(key=lambda v: (v[0], v[1]))
    truncated = len(scored) > limit
    matches = [
        {"name": qual, "kind": kind, "doc": doc}
        for _, qual, kind, doc in scored[:limit]
    ]
    return {"matches": matches, "truncated": truncated}


_SEARCH_API_DESCRIPTION = """\
Keyword-search the LIVE FreeCAD Python API by name and docstring.

Use this BEFORE guessing method names or writing exploratory dir() loops:
e.g. query "preselect" finds Gui.Selection.setPreselection, "make fillet"
finds Part.makeFillet. Every whitespace-separated term must match
(case-insensitive) in the qualified name or the docstring head; name
matches rank first. FreeCAD docstrings usually start with the call
signature, so the results teach the arguments too.

Searches FreeCAD + FreeCADGui (including one submodule level, e.g.
Gui.Selection) and the common workbench modules (Part, Sketcher, Draft,
Mesh, ...; imported on demand). Pass `modules` to search other importable
modules as well. Follow up with run_python help(<name>) for the full text.
"""


_RUN_PYTHON_DESCRIPTION = """\
Execute Python inside the running FreeCAD process and return captured stdout,
stderr, FreeCAD console messages (PrintError/PrintWarning/... and C++
Base::Console output, which bypass sys.stdout/stderr), the repr of the final
expression (when the code is an expression), and a traceback string if it
raised.

The interpreter session is PERSISTENT across calls (like a REPL) and runs on
FreeCAD's main thread, so it is safe to create documents, build geometry, and
touch the GUI.

Pre-bound names:
  App / FreeCAD     - FreeCAD core (documents, objects, geometry, App.Vector...)
  Gui / FreeCADGui  - GUI module (selection, view providers, active view)
  _                 - value of the last evaluated expression

The ENTIRE FreeCAD API is reachable from here. Use the search_api tool to
DISCOVER names by keyword, then introspect rather than guess:
  App.listDocuments()               # names of open documents
  doc = App.ActiveDocument          # the active document (may be None)
  [o.Name for o in doc.Objects]     # object names in a document
  obj = doc.getObject("Box")        # fetch an object by name
  obj.PropertiesList                # its property names
  obj.ViewObject                    # its GUI side (colors, visibility...)
  dir(obj); help(App.Vector)        # introspect any object or type
  obj.Shape.Volume                  # OCCT shape data

Common GUI entry points:
  Gui.Selection                     # selection module: addSelection(doc, obj,
                                    #   "Face3"), clearSelection, getSelectionEx,
                                    #   setPreselection(obj, "Edge2") for hover
                                    #   highlight, observers...
                                    # The sub-element argument is a full object
                                    # PATH relative to the given object:
                                    #   "Group2.Link2.Face1" reaches into
                                    #   groups/links; a trailing dot
                                    #   ("Group2.Link2.") targets that whole
                                    #   object. Same for addSelection and
                                    #   setPreselection.
  Gui.runCommand("Std_ViewFitAll")  # run any GUI command by name
  Gui.listCommands()                # all runnable command names
  Gui.ActiveDocument.ActiveView     # the 3D view (camera, axonometric, ...)

Example - create and measure a box:
  d = App.ActiveDocument or App.newDocument()
  b = d.addObject("Part::Box", "Box"); b.Length = 5
  d.recompute(); b.Shape.Volume
"""


_GET_LOG_DESCRIPTION = """\
Read FreeCAD's console output captured since the MCP server started.

Everything Base::Console emits is captured -- C++ and Python, from any thread --
including messages produced outside any run_python call: document restore,
recomputes driven from the GUI, OCCT errors, warnings before a crash. This is
the log to consult when something went wrong and you were not the one who
triggered it.

Served without touching FreeCAD's main thread, so it still answers while the
main thread is busy in a long recompute or wedged.

    limit     max lines to return, newest last (default 200, 0 for all held)
    level     keep only one level: Error, Warning, Message, Log, Critical
    contains  keep only lines containing this substring

The same lines are appended to a file on disk (see the 'path' field), which
outlives the process -- read that if FreeCAD died.
"""


def _make_server(host: str, port: int):
    """Return ``(server, serve)`` for whichever major version of ``mcp`` is present.

    The high-level server class moved between major versions, and so did where the
    bind address is given:

    * ``mcp`` 1.x — ``mcp.server.fastmcp.FastMCP``, host/port on the constructor,
      ``run(transport=...)`` takes no address.
    * ``mcp`` 2.x — ``FastMCP`` is gone, replaced by ``mcp.server.MCPServer``, whose
      constructor has no host/port; they are passed through ``run()`` instead.

    ``serve`` is a zero-argument callable that blocks serving Streamable HTTP, so
    the caller does not have to care which one it got. Both versions expose the
    same ``tool(name=..., description=...)`` decorator and default the endpoint
    path to ``/mcp``, so the rest of this module is version-agnostic.

    This ``serve`` is only the fallback: it keeps its uvicorn.Server private, so
    there is no handle to bring the server back down. :func:`_make_serve` prefers
    to drive uvicorn itself -- see there.
    """
    try:
        from mcp.server.fastmcp import FastMCP  # mcp 1.x
    except ImportError:
        pass
    else:
        server = FastMCP("FreeCAD Debug Console", host=host, port=port)
        return server, lambda: server.run(transport="streamable-http")

    try:
        from mcp.server import MCPServer  # mcp 2.x
    except ImportError as exc:
        raise ImportError(
            "freecad.mcp_console needs the 'mcp' package: neither "
            "mcp.server.fastmcp.FastMCP (1.x) nor mcp.server.MCPServer (2.x) "
            "could be imported"
        ) from exc

    server = MCPServer("FreeCAD Debug Console")
    return server, (lambda: server.run(transport="streamable-http",
                                       host=host, port=port))


def _default_log_path() -> str:
    try:
        import FreeCAD
        return os.path.join(FreeCAD.getUserAppDataDir(), "mcp_console.log")
    except Exception:
        return os.path.join(os.path.expanduser("~"), "mcp_console.log")


def _rotate_logs(path: str):
    """Shift ``path`` -> ``path.1`` -> ... -> ``path.N``, dropping the oldest.

    Called once per session and again whenever the current file outgrows
    _LOG_MAX_BYTES, so the last _LOG_KEEP sessions/rolls stay on disk and total
    usage is bounded.
    """
    try:
        oldest = "%s.%d" % (path, _LOG_KEEP)
        if os.path.exists(oldest):
            os.remove(oldest)
        for i in range(_LOG_KEEP - 1, 0, -1):
            src = "%s.%d" % (path, i)
            if os.path.exists(src):
                os.replace(src, "%s.%d" % (path, i + 1))
        if os.path.exists(path):
            os.replace(path, path + ".1")
    except OSError:
        pass  # a locked or vanished file must not stop the server starting


def _roll_if_large():
    """Roll the current log once it passes the size cap. Caller holds _log_lock."""
    global _log_fh, _log_unflushed
    if _log_fh is None or _log_path is None:
        return
    try:
        if _log_fh.tell() < _LOG_MAX_BYTES:
            return
        _log_fh.flush()
        _log_fh.close()
        _log_fh = None
        _rotate_logs(_log_path)
        _log_fh = open(_log_path, "w", encoding="utf-8", errors="replace")
        _log_fh.write("==== continued %s (previous part rolled to %s.1) ====\n"
                      % (time.strftime("%Y-%m-%d %H:%M:%S"), os.path.basename(_log_path)))
        _log_unflushed = 0
    except Exception:
        pass


def _attach_log_observer(path: Optional[str]) -> Optional[str]:
    """Capture every Base::Console message for the life of the server.

    Returns the log path, or None if the running binary has no AttachObserver.
    The observer fires on whichever thread emitted the message (OCCT worker
    threads included), hence the lock.
    """
    global _log_observer, _log_fh, _log_path, _log_unflushed

    import FreeCAD
    if not hasattr(FreeCAD.Console, "AttachObserver"):
        return None

    path = path or _default_log_path()
    # Start a fresh file and push the previous session down a slot, rather than
    # appending forever: the last _LOG_KEEP sessions stay readable and disk use
    # stays bounded.
    _rotate_logs(path)
    _log_fh = open(path, "w", encoding="utf-8", errors="replace")
    _log_path = path
    _log_unflushed = 0
    _log_fh.write("==== mcp_console session started %s ====\n"
                  % time.strftime("%Y-%m-%d %H:%M:%S"))
    _log_fh.flush()

    def observer(notifier, msg, level):
        global _log_unflushed, _log_total
        line = "%s %-8s %s%s" % (time.strftime("%H:%M:%S"), level,
                                 ("[%s] " % notifier) if notifier else "",
                                 msg.rstrip())
        with _log_lock:
            _log_ring.append(line)
            _log_total += 1
            if _log_fh is None:
                return
            try:
                _log_fh.write(line + "\n")
                _log_unflushed += 1
                # A per-line flush is too costly here: restoring a large
                # document emits console errors in the thousands.
                if level in _LOG_FLUSH_LEVELS or _log_unflushed >= 200:
                    _log_fh.flush()
                    _log_unflushed = 0
                _roll_if_large()
            except Exception:
                pass  # never let logging break the thing being logged

    FreeCAD.Console.AttachObserver(observer)
    _log_observer = observer
    return path


def _detach_log_observer():
    global _log_observer, _log_fh, _log_unflushed
    if _log_observer is not None:
        try:
            import FreeCAD
            FreeCAD.Console.DetachObserver(_log_observer)
        except Exception:
            pass
        _log_observer = None
    with _log_lock:
        if _log_fh is not None:
            try:
                _log_fh.write("==== mcp_console session stopped %s ====\n"
                              % time.strftime("%Y-%m-%d %H:%M:%S"))
                _log_fh.flush()
                _log_fh.close()
            except Exception:
                pass
            _log_fh = None
        _log_unflushed = 0


def log_path() -> Optional[str]:
    """Where the session console log is being written, if capture is active."""
    return _log_path


def get_log(limit: int = 200, level: Optional[str] = None,
            contains: Optional[str] = None, flush: bool = False) -> LogResult:
    """Recent captured console lines, newest last.

    ``flush`` pushes the file buffer out first. Off by default: the ring buffer
    is the source of truth for this call, and a flush is a syscall made while
    holding _log_lock -- which the observer also takes for every message. Under
    a storm (a document restore emitting thousands of OCCT errors per second,
    each also contending for the GIL) that was enough to starve this call past a
    30s client timeout. Ask for it only when reading the file matters.
    """
    if flush:
        with _log_lock:
            if _log_fh is not None:
                try:
                    _log_fh.flush()
                except Exception:
                    pass
    with _log_lock:
        lines = list(_log_ring)
    total = len(lines)
    if level:
        lines = [ln for ln in lines if (" %s " % level) in ln[:22]]
    if contains:
        lines = [ln for ln in lines if contains in ln]
    matched = len(lines)
    if limit and limit > 0:
        lines = lines[-limit:]
    return {
        "lines": lines,
        "total_captured": total,
        "matched": matched,
        "truncated": matched > len(lines),
        "path": _log_path,
    }


def _make_serve(server, fallback, host: str, port: int):
    """Return ``(uvicorn_server, serve)`` for an already-configured MCP server.

    Prefer building the ASGI app and running uvicorn ourselves: ``server.run()``
    constructs its ``uvicorn.Server`` internally and never hands it out, and
    without that object there is nothing to set ``should_exit`` on -- i.e. no way
    to implement :func:`stop`. Both mcp majors expose ``streamable_http_app()``;
    if some version does not, fall back to the blocking ``run()`` and return
    ``None``, so ``stop()`` can say it cannot help instead of pretending.
    """
    app_factory = getattr(server, "streamable_http_app", None)
    if not callable(app_factory):
        return None, fallback

    import uvicorn

    config = uvicorn.Config(app_factory(), host=host, port=port, log_level="warning")
    uv = uvicorn.Server(config)
    # uvicorn skips signal-handler installation off the main thread, so running
    # this on our daemon thread is fine.
    return uv, uv.run


_atexit_registered = False


def _atexit_cleanup():
    """Detach from the interpreter before it goes away.

    Runs early in interpreter shutdown, while Python is still fully alive.
    Two things must not outlive this point: the uvicorn daemon thread (it
    would keep calling into Python during finalization) and the C++-side
    console observer (Base::Console messages emitted after Py_Finalize --
    main() logs "completely terminated" after destructing the App -- would
    call back into a torn-down runtime).
    """
    try:
        stop(timeout=1.0)
    except Exception:
        pass
    _detach_log_observer()


def start(host: str = _DEFAULT_HOST, port: int = _DEFAULT_PORT,
          log: Optional[str] = None) -> str:
    """Start the MCP console server on a background thread.

    Must be called from FreeCAD's main thread (e.g. the Python console). Returns
    the endpoint URL. Idempotent: a second call while running is a no-op.

    Console capture is attached for the life of the server; ``log`` overrides
    where it is written (default ``<UserAppData>/mcp_console.log``).
    """
    global _executor, _server_thread, _mcp, _uvicorn, _bound, _atexit_registered

    if is_running():
        return "MCP console already running at " + url()

    if not _atexit_registered:
        import atexit
        atexit.register(_atexit_cleanup)
        _atexit_registered = True

    _bound = (host, port)
    _seed_namespace()
    try:
        logfile = _attach_log_observer(log)
    except Exception:
        logfile = None  # capture is a convenience, never a reason not to serve
    _executor = _make_executor()
    _mcp, _fallback_serve = _make_server(host, port)

    @_mcp.tool(name="run_python", description=_RUN_PYTHON_DESCRIPTION)
    def run_python(code: str) -> RunResult:
        # FastMCP runs sync tools in a worker thread, so blocking here is fine.
        return _executor.run_on_main(lambda: _exec_code(code))

    @_mcp.tool(name="search_api", description=_SEARCH_API_DESCRIPTION)
    def search_api(query: str,
                   modules: Optional[list[str]] = None,
                   limit: int = 30) -> SearchResult:
        # On the main thread too: it may import workbench modules.
        return _executor.run_on_main(
            lambda: _search_api(query, modules, limit))

    @_mcp.tool(name="get_log", description=_GET_LOG_DESCRIPTION)
    def get_log_tool(limit: int = 200,
                     level: Optional[str] = None,
                     contains: Optional[str] = None) -> LogResult:
        # Deliberately NOT marshalled onto the main thread: the whole point is
        # to be able to read the log while the main thread is busy or wedged.
        return get_log(limit=limit, level=level, contains=contains)

    _uvicorn, _serve_forever = _make_serve(_mcp, _fallback_serve, host, port)

    _server_thread = threading.Thread(target=_serve_forever, name="mcp-console",
                                      daemon=True)
    _server_thread.start()
    msg = "MCP console running at " + url()
    if logfile:
        msg += " (console log: %s)" % logfile
    return msg


def stop(timeout: float = 5.0) -> str:
    """Shut the MCP console server down and release the port.

    Idempotent: stopping a server that is not running is a no-op. Returns a
    human-readable status line, the same way :func:`start` does.
    """
    global _executor, _server_thread, _mcp, _uvicorn

    if not is_running():
        return "MCP console is not running"

    was = url()

    if _uvicorn is None:
        # Fallback serving path -- see _make_serve(). Nothing to signal.
        return ("MCP console at " + was + " cannot be stopped: this version of "
                "'mcp' exposes no ASGI app, so uvicorn is out of reach. It will "
                "go away when FreeCAD exits.")

    _uvicorn.should_exit = True
    _server_thread.join(timeout)
    if _server_thread.is_alive():
        return ("MCP console at " + was + " did not stop within %.1fs; it will "
                "go away when FreeCAD exits." % timeout)

    _server_thread = None
    _uvicorn = None
    _mcp = None
    _executor = None
    # Console capture is documented to last "for the life of the server".
    _detach_log_observer()
    return "MCP console stopped (was " + was + ")"


def is_running() -> bool:
    return _server_thread is not None and _server_thread.is_alive()


def url() -> str:
    host, port = _bound
    return "http://{}:{}/mcp".format(host, port)
