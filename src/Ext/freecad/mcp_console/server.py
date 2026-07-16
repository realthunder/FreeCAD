# SPDX-License-Identifier: LGPL-2.1-or-later
"""MCP-conformant debug console server for FreeCAD.

Exposes a single ``run_python`` tool over the Model Context Protocol
(Streamable HTTP transport) so an AI agent can drive a live, running FreeCAD
process as a persistent Python REPL.

Everything in FreeCAD is already reachable from Python, so this server does not
try to wrap individual operations as separate tools.  Instead it offers one
excellent console tool whose description teaches the agent the entry points and
how to introspect the rest.

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

The ENTIRE FreeCAD API is reachable from here; there are no other tools. Prefer
introspecting over guessing:
  App.listDocuments()               # names of open documents
  doc = App.ActiveDocument          # the active document (may be None)
  [o.Name for o in doc.Objects]     # object names in a document
  obj = doc.getObject("Box")        # fetch an object by name
  obj.PropertiesList                # its property names
  dir(obj); help(App.Vector)        # introspect any object or type
  obj.Shape.Volume                  # OCCT shape data

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
