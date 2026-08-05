# SPDX-License-Identifier: LGPL-2.1-or-later
"""MCP debug console for FreeCAD.

    from freecad import mcp_console
    mcp_console.start()      # serves http://127.0.0.1:8765/mcp
    mcp_console.stop()

Or toggle it from the Tools menu, which also remembers the state for the next
start. See :mod:`freecad.mcp_console.server` for details.
"""

from .server import start, stop, is_running, url

__all__ = ["start", "stop", "is_running", "url"]
