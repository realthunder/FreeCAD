"""One public origin for a shared FreeCAD document, with the client's
real address preserved.

Runs on the *public* host (the tunnel endpoint), because that is the
only place the client's address still exists: an `ssh -R` tunnel does
not carry it — the local ssh client opens its own connection to the
backend, which is why an unproxied roster shows nothing but loopback.

What it does, per connection:
  - reads the request head, picks an upstream by path (the scene
    server's routes go to the scene tunnel, everything else to the
    viewer bundle), so page and scene share one origin and one
    firewall port;
  - inserts `X-Forwarded-For: <peer>` (replacing any the client sent —
    a client-supplied one is a claim, not a fact);
  - then splices bytes both ways, untouched. That is what makes the
    WebSocket upgrade work: after the head, this is a pipe.

Deliberately stdlib-only and single-file: the host is an EOL Ubuntu
14.04 box, and nothing should have to be installed on it to take this
away again — kill the process and it is gone.

    python3 fcproxy.py [listen_port] [scene_port] [bundle_port]
"""
import socket
import sys
import threading

LISTEN = int(sys.argv[1]) if len(sys.argv) > 1 else 8083
SCENE = int(sys.argv[2]) if len(sys.argv) > 2 else 8081
BUNDLE = int(sys.argv[3]) if len(sys.argv) > 3 else 8080

# The scene server's own routes (SceneServer.cpp); everything else is
# the viewer page and its bundle.
SCENE_PATHS = ("/scene", "/blob", "/blobs", "/level", "/decisions", "/log")


def pick_upstream(head):
    line = head.split(b"\r\n", 1)[0].decode("latin-1", "replace")
    parts = line.split(" ")
    path = parts[1] if len(parts) > 2 else "/"
    base = path.split("?", 1)[0]
    for p in SCENE_PATHS:
        if base == p or base.startswith(p + "/"):
            return SCENE
    return BUNDLE


def rewrite(head, peer):
    """Head with our X-Forwarded-For, and any client-sent one dropped."""
    lines = head.split(b"\r\n")
    out = [lines[0]]
    for line in lines[1:]:
        low = line.lower()
        if low.startswith(b"x-forwarded-for:") or low.startswith(b"forwarded:"):
            continue
        out.append(line)
    # The head ends with the blank line; insert just before it.
    while out and out[-1] == b"":
        out.pop()
    out.append(("X-Forwarded-For: %s" % peer).encode())
    out.append(b"")
    out.append(b"")
    return b"\r\n".join(out)


def splice(src, dst):
    try:
        while True:
            data = src.recv(65536)
            if not data:
                break
            dst.sendall(data)
    except OSError:
        pass
    finally:
        for s in (src, dst):
            try:
                s.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass


def handle(client, peer):
    up = None
    try:
        client.settimeout(20)
        buf = b""
        while b"\r\n\r\n" not in buf:
            chunk = client.recv(4096)
            if not chunk:
                return
            buf += chunk
            if len(buf) > 32768:
                return
        head, rest = buf.split(b"\r\n\r\n", 1)
        port = pick_upstream(head)
        client.settimeout(None)
        up = socket.create_connection(("127.0.0.1", port), timeout=15)
        up.settimeout(None)
        up.sendall(rewrite(head + b"\r\n\r\n", peer) + rest)
        threading.Thread(target=splice, args=(client, up), daemon=True).start()
        splice(up, client)
    except OSError:
        pass
    finally:
        for s in (client, up):
            if s:
                try:
                    s.close()
                except OSError:
                    pass


def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", LISTEN))
    srv.listen(64)
    sys.stderr.write("fcproxy: :%d -> scene %d / bundle %d\n"
                     % (LISTEN, SCENE, BUNDLE))
    sys.stderr.flush()
    while True:
        client, addr = srv.accept()
        threading.Thread(target=handle, args=(client, addr[0]),
                         daemon=True).start()


main()
