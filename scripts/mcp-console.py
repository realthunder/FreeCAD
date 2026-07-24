"""Start the MCP debug console inside a running FreeCAD.

Passed as an extra script to the FreeCAD binary (renderer-serve.sh does
this for serving backends), so an AI agent can drive the live process
over streamable-HTTP MCP (freecad.mcp_console: run_python/search_api).
FC_MCP_PORT overrides the port (default 8765); the server binds
127.0.0.1 only.
"""
import os

import FreeCAD
from freecad import mcp_console

FreeCAD.Console.PrintMessage(
    mcp_console.start(port=int(os.environ.get("FC_MCP_PORT", "8765"))) + "\n"
)
