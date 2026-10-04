// The shared-memory handshake between a guest in a worker and the page that
// holds the socket (docs/Sandbox.md 7.20, C6 -- the path for a browser with no
// JSPI, which is every Safari).
//
// The guest's host call is a synchronous wasm import, so SOMETHING has to
// block until the reply is in hand.  JSPI suspends the wasm stack on a
// promise; where there is no JSPI the guest runs in a worker whose thread may
// block outright, and this is what it blocks on: one SharedArrayBuffer, the
// worker parked in Atomics.wait while the page's main thread -- free, and the
// only thread that holds the viewer's WebSocket -- carries the op and writes
// the reply back.
//
// Which half does what:
//   worker  copies the request out of wasm memory, postMessage's it, stores 0
//           in STATE and waits on it
//   main    round trips over the socket, writes the reply into DATA in chunks,
//           and for each chunk sets TOTAL, CHUNK, STATE=CHUNK_READY and
//           notifies; the worker copies the chunk out and, if more is coming,
//           stores 0 back and asks for the next one by message
//   main    STATE=FAILED when the op is refused, lost or too slow: the worker
//           answers the import -1 and the guest raises "host bridge
//           unavailable", the same end the JSPI path has
//
// The request goes by postMessage rather than through the buffer because the
// worker is not blocked yet when it sends it; the reply cannot, because by
// then it is.  Cross-origin isolation (COOP/COEP on the served page,
// SceneServer.cpp) is what makes the SharedArrayBuffer exist at all.

/// Int32 slots at the head of the buffer.
export const STATE = 0;
/// The whole reply's length in bytes, or -1.
export const TOTAL = 1;
/// Bytes of the reply in DATA right now.
export const CHUNK = 2;

export const WAITING = 0;
export const CHUNK_READY = 1;
export const FAILED = 2;

/// Where the reply bytes start; the control slots sit below it.
export const DATA = 64;
/// Reply bytes per round.  An op answers in about a hundred bytes and a
/// prefetch of a thousand siblings in some tens of kilobytes (7.20 C5), so
/// one round carries every realistic reply; a larger one chunks.
export const DATA_BYTES = 1 << 20;

/// How long the worker parks before it gives the guest -1.  The main thread's
/// own op timeout is shorter (remote.ts), so this only catches a page that
/// stopped answering altogether.
export const WAIT_MS = 120000;

export function makeSab(): SharedArrayBuffer {
  return new SharedArrayBuffer(DATA + DATA_BYTES);
}

/// Whether this browser can run the guest in a worker: shared memory, the
/// blocking wait, and module workers.  False on a page that is not
/// cross-origin isolated, where SharedArrayBuffer is absent by design.
export function sabSupported(): boolean {
  return typeof SharedArrayBuffer === 'function'
    && typeof Worker === 'function'
    && typeof Atomics === 'object'
    && typeof Atomics.wait === 'function'
    && (self as any).crossOriginIsolated === true;
}
