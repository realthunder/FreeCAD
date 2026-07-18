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
"""

import ast
import io
import sys
import threading
import traceback
from typing import Optional, TypedDict

__all__ = ["start", "is_running", "url"]


class RunResult(TypedDict):
    """Structured result of a ``run_python`` call (drives the MCP outputSchema)."""

    stdout: str
    stderr: str
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

_DEFAULT_HOST = "127.0.0.1"
_DEFAULT_PORT = 8765

# Persistent REPL namespace, shared across every run_python call.
_namespace: dict = {}

_executor = None
_server_thread = None
_mcp = None
_bound = (_DEFAULT_HOST, _DEFAULT_PORT)


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
    return {
        "stdout": out.getvalue(),
        "stderr": err.getvalue(),
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
stderr, the repr of the final expression (when the code is an expression), and a
traceback string if it raised.

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


def start(host: str = _DEFAULT_HOST, port: int = _DEFAULT_PORT) -> str:
    """Start the MCP console server on a background thread.

    Must be called from FreeCAD's main thread (e.g. the Python console). Returns
    the endpoint URL. Idempotent: a second call while running is a no-op.
    """
    global _executor, _server_thread, _mcp, _bound

    if is_running():
        return "MCP console already running at " + url()

    from mcp.server.fastmcp import FastMCP

    _bound = (host, port)
    _seed_namespace()
    _executor = _make_executor()
    _mcp = FastMCP("FreeCAD Debug Console", host=host, port=port)

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

    def _serve():
        _mcp.run(transport="streamable-http")

    _server_thread = threading.Thread(target=_serve, name="mcp-console", daemon=True)
    _server_thread.start()
    return "MCP console running at " + url()


def is_running() -> bool:
    return _server_thread is not None and _server_thread.is_alive()


def url() -> str:
    host, port = _bound
    return "http://{}:{}/mcp".format(host, port)
