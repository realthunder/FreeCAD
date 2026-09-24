#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Run Python inside a running FreeCAD through its MCP debug console.

Standard library only, so any Python runs it -- the client needs no 'mcp'
package; FreeCAD's own environment does (see docs/DevEnvironment.md, "MCP
debug console").

    mcp_run.py script.py              run a file in the newest live FreeCAD
    mcp_run.py -c "print(App.Version())"
    mcp_run.py --list                 the live servers this user can see
    mcp_run.py --log 50               the last 50 console lines
    mcp_run.py --launch [--port N] -- <FreeCAD executable> [args...]
                                      start FreeCAD serving, wait until it is
                                      up, print its pid and URL, and return

Where the server is: --url, else FCAD_MCP_URL, else the endpoint files every
running server writes to ~/.freecad-mcp/<pid>.json (FC_MCP_ENDPOINT_DIR
overrides) -- the newest one whose process is alive, or the one --pid names.
The files are per OS and per user, so a FreeCAD in WSL is never picked up
from Windows or the other way round, whatever answers on 127.0.0.1.

Exit status: 0, 1 when the code raised inside FreeCAD, 2 on a usage or
connection error.
"""

import argparse
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

PROTOCOL_VERSION = "2025-06-18"


def endpoint_dir():
    return (os.environ.get("FC_MCP_ENDPOINT_DIR")
            or os.path.join(os.path.expanduser("~"), ".freecad-mcp"))


def pid_alive(pid):
    if os.name == "nt":
        # os.kill(pid, 0) would TerminateProcess on Windows
        import ctypes
        kernel32 = ctypes.windll.kernel32
        handle = kernel32.OpenProcess(0x1000, False, pid)  # QUERY_LIMITED_INFORMATION
        if not handle:
            return False
        code = ctypes.c_ulong()
        ok = kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
        kernel32.CloseHandle(handle)
        return bool(ok) and code.value == 259  # STILL_ACTIVE
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def endpoints():
    """Live servers, newest first. Files of dead processes are removed."""
    found = []
    directory = endpoint_dir()
    try:
        names = os.listdir(directory)
    except OSError:
        return found
    for name in names:
        if not name.endswith(".json"):
            continue
        path = os.path.join(directory, name)
        try:
            with open(path) as f:
                info = json.load(f)
        except (OSError, ValueError):
            continue
        if not pid_alive(int(info.get("pid", 0))):
            try:
                os.remove(path)
            except OSError:
                pass
            continue
        found.append(info)
    found.sort(key=lambda i: i.get("started", 0), reverse=True)
    return found


class McpError(Exception):
    pass


class Session:
    """The Streamable HTTP transport, as much of it as a one-shot client needs."""

    def __init__(self, url, timeout):
        self.url = url
        self.timeout = timeout
        self.session_id = None
        self.protocol = None
        self.next_id = 1

    def _post(self, message):
        headers = {"Content-Type": "application/json",
                   "Accept": "application/json, text/event-stream"}
        if self.session_id:
            headers["Mcp-Session-Id"] = self.session_id
        if self.protocol:
            headers["Mcp-Protocol-Version"] = self.protocol
        request = urllib.request.Request(self.url, json.dumps(message).encode(),
                                         headers, method="POST")
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                sid = response.headers.get("Mcp-Session-Id")
                if sid:
                    self.session_id = sid
                body = response.read().decode("utf-8", "replace")
                kind = response.headers.get("Content-Type", "")
        except urllib.error.HTTPError as e:
            raise McpError("HTTP %d from %s: %s" % (e.code, self.url, e.read()[:500]))
        except (urllib.error.URLError, OSError) as e:
            raise McpError("cannot reach %s: %s" % (self.url, e))
        if "id" not in message:
            return None
        if "text/event-stream" in kind:
            replies = [json.loads(line[5:]) for line in body.splitlines()
                       if line.startswith("data:") and line[5:].strip()]
        else:
            replies = [json.loads(body)] if body.strip() else []
        for reply in replies:
            if reply.get("id") == message["id"]:
                if "error" in reply:
                    raise McpError("%s: %s" % (message["method"], reply["error"]))
                return reply.get("result")
        raise McpError("no reply to %s from %s" % (message["method"], self.url))

    def request(self, method, params):
        message = {"jsonrpc": "2.0", "id": self.next_id, "method": method, "params": params}
        self.next_id += 1
        return self._post(message)

    def open(self):
        result = self.request("initialize", {
            "protocolVersion": PROTOCOL_VERSION,
            "capabilities": {},
            "clientInfo": {"name": "mcp_run", "version": "1"}})
        self.protocol = result.get("protocolVersion", PROTOCOL_VERSION)
        self._post({"jsonrpc": "2.0", "method": "notifications/initialized"})
        return result

    def close(self):
        if not self.session_id:
            return
        request = urllib.request.Request(self.url, method="DELETE",
                                         headers={"Mcp-Session-Id": self.session_id})
        try:
            urllib.request.urlopen(request, timeout=5).close()
        except (urllib.error.URLError, OSError):
            pass

    def call(self, tool, arguments):
        result = self.request("tools/call", {"name": tool, "arguments": arguments})
        if result.get("structuredContent") is not None:
            return result["structuredContent"], result.get("isError", False)
        text = "".join(c.get("text", "") for c in result.get("content", []))
        try:
            return json.loads(text), result.get("isError", False)
        except ValueError:
            return {"stdout": text}, result.get("isError", False)


def resolve_url(args):
    if args.url:
        return args.url, None
    if os.environ.get("FCAD_MCP_URL"):
        return os.environ["FCAD_MCP_URL"], None
    live = endpoints()
    if args.pid:
        live = [i for i in live if int(i.get("pid", 0)) == args.pid]
    if not live:
        where = "pid %d" % args.pid if args.pid else "any FreeCAD"
        raise McpError("no MCP console found for %s in %s -- is FreeCAD running with "
                       "FC_MCP_PORT set, or Tools > MCP server on?" % (where, endpoint_dir()))
    return live[0]["url"], live[0]


def launch(args, command):
    if not command:
        raise McpError("--launch needs the FreeCAD command after --")
    env = dict(os.environ, FC_MCP_PORT=str(args.port))
    flags = 0
    if os.name == "nt":
        flags = subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.DETACHED_PROCESS
    child = subprocess.Popen(command, env=env, creationflags=flags,
                             stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL, start_new_session=os.name != "nt")
    deadline = time.time() + args.timeout
    while time.time() < deadline:
        if child.poll() is not None:
            raise McpError("FreeCAD exited with %d before serving" % child.returncode)
        for info in endpoints():
            if int(info.get("pid", 0)) == child.pid:
                print("pid %d serving %s" % (child.pid, info["url"]))
                return 0
        time.sleep(0.5)
    raise McpError("pid %d did not start serving within %ds -- check its report view "
                   "or mcp_console.log; a missing 'mcp' package is reported there"
                   % (child.pid, args.timeout))


def main():
    argv = sys.argv[1:]
    command = []
    if "--" in argv:
        at = argv.index("--")
        argv, command = argv[:at], argv[at + 1:]
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("script", nargs="?", help="a Python file to run inside FreeCAD")
    parser.add_argument("-c", dest="code", help="code to run instead of a file")
    parser.add_argument("--url", help="the server's URL; default: discovered")
    parser.add_argument("--pid", type=int, help="the FreeCAD process to talk to")
    parser.add_argument("--list", action="store_true", help="list the live servers")
    parser.add_argument("--log", type=int, metavar="N", help="print the last N console lines")
    parser.add_argument("--launch", action="store_true", help="start FreeCAD, see above")
    parser.add_argument("--port", type=int, default=8765, help="--launch: first port to try")
    parser.add_argument("--timeout", type=float, default=600,
                        help="seconds to wait for a reply, or for --launch to serve")
    args = parser.parse_args(argv)

    try:
        if args.launch:
            return launch(args, command)
        if args.list:
            for info in endpoints():
                print("pid %-7s %s  %s%s" % (info.get("pid"), info.get("url"),
                                             info.get("home", "?"),
                                             "" if info.get("gui", True) else "  (no GUI)"))
            return 0
        if args.code is None and not args.script and args.log is None:
            parser.error("give a script, -c CODE, --log, --list or --launch")

        url, info = resolve_url(args)
        if info:
            print("[mcp_run] pid %s %s %s" % (info.get("pid"), url, info.get("home", "")),
                  file=sys.stderr)
        session = Session(url, args.timeout)
        session.open()
        try:
            if args.log is not None:
                result, _ = session.call("get_log", {"limit": args.log})
                print("\n".join(result.get("lines", [])))
                return 0
            code = args.code
            if code is None:
                with open(args.script, encoding="utf-8") as f:
                    code = f.read()
            result, is_error = session.call("run_python", {"code": code})
        finally:
            session.close()
    except McpError as e:
        print("mcp_run: %s" % e, file=sys.stderr)
        return 2

    if result.get("stdout"):
        sys.stdout.write(result["stdout"])
    if result.get("stderr"):
        sys.stderr.write(result["stderr"])
    for line in result.get("console") or []:
        print("[console] %s" % line.rstrip(), file=sys.stderr)
    if result.get("result") is not None:
        print(result["result"])
    if result.get("exception"):
        sys.stderr.write(result["exception"])
        return 1
    return 1 if is_error else 0


if __name__ == "__main__":
    sys.exit(main())
