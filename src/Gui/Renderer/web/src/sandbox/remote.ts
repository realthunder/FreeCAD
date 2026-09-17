// The remote bridge (docs/Sandbox.md 7.20, C2): a guest in this page reaching
// the FreeCAD that serves the document, over a WebSocket to /scene -- the
// transport under BrowserGuest's `bridge` option.
//
// The wire (src/Gui/Renderer/SceneServer.h, SceneBridgeRequest):
//   up    'S', kind u8 (0 op, 1 end of statement), seq u32 LE, request bytes
//   down  'FCSB', seq u32 LE, reply bytes -- empty when nothing serves the bridge
// Everything else on the socket -- scene payloads, streamed frames, control
// text -- is someone else's and ignored here.
//
// Which socket (C4): the viewer page's own, through the hooks the wasm viewer
// installs (`viewer()`), so the console is the same connection as the view --
// one roster entry, one access mode, one principal.  A page with no viewer
// opens one of its own (`connect()`), or hands over one it has (`attach()`).
//
// Transport-shaped on purpose: bytes in, a promise of bytes out.  How the guest
// waits for the promise is guest.ts's business -- JSPI's suspending import
// today, a Worker blocked in Atomics.wait for Safari later (C6), which swaps
// only that side.

import type { HostBridge } from './guest.js';

const MAGIC = [0x46, 0x43, 0x53, 0x42]; // FCSB
const TAG = 0x53; // 'S'
const KIND_OP = 0;
const KIND_END = 1;

declare global {
  interface Window {
    /// The wasm viewer's bridge uplink (wasm/main.cpp fcviewer_bridge_send):
    /// one whole 'S' frame on the scene socket; false when it is down.
    fcviewerBridgeSend?: (frame: Uint8Array) => boolean;
    /// Whether the viewer's scene socket is open; 'fc:socket' events follow it.
    fcviewerSocketOpen?: boolean;
  }
}

export interface RemoteOptions {
  /// The link's ?token=.
  token?: string;
  /// The served document to join; the server's default otherwise.
  doc?: string;
  /// The label the sharing roster shows.
  client?: string;
  /// How long one op may wait for its answer.
  timeoutMs?: number;
}

export interface BridgeStats {
  ops: number;
  statements: number;
  bytesUp: number;
  bytesDown: number;
  /// What else the socket was pushed -- the scene, streamed frames, control
  /// text -- which a console's socket of its own pays for and ignores.
  otherBytesDown: number;
  /// Round-trip time of the answered ops, summed and worst.
  totalMs: number;
  maxMs: number;
}

/// What the bridge needs of a connection.
interface Port {
  /// Put one frame on the wire; false when it cannot go.
  send(frame: Uint8Array): boolean;
  readonly open: boolean;
  /// Whether a down port can come back: the viewer reconnects, a socket of
  /// our own does not.
  readonly reconnects: boolean;
  /// Every binary message, every other message's size, every loss.
  listen(frame: (bytes: Uint8Array) => void, other: (size: number) => void,
         down: () => void): void;
  close(): void;
  /// Join another served document, where the port's owner is the page.
  switchDoc?(name: string): void;
}

function socketPort(socket: WebSocket): Port {
  socket.binaryType = 'arraybuffer';
  return {
    send(frame) {
      if (socket.readyState !== WebSocket.OPEN)
        return false;
      socket.send(frame);
      return true;
    },
    get open() { return socket.readyState === WebSocket.OPEN; },
    reconnects: false,
    listen(frame, other, down) {
      socket.addEventListener('message', (e) => {
        if (e.data instanceof ArrayBuffer)
          frame(new Uint8Array(e.data));
        else
          other(typeof e.data === 'string' ? e.data.length : 0);
      });
      socket.addEventListener('close', down);
    },
    close() { socket.close(); },
    switchDoc(name) {
      if (socket.readyState === WebSocket.OPEN)
        socket.send(JSON.stringify({ cmd: 'switch', doc: name }));
    },
  };
}

function viewerPort(): Port {
  return {
    send: (frame) => !!window.fcviewerBridgeSend?.(frame),
    get open() { return !!window.fcviewerSocketOpen; },
    reconnects: true,
    listen(frame, _other, down) {
      // The viewer hands over only bridge frames; the scene never gets here.
      window.addEventListener('fc:bridge', (e: Event) => {
        const d = (e as CustomEvent).detail;
        if (d instanceof Uint8Array)
          frame(d);
      });
      window.addEventListener('fc:socket', (e: Event) => {
        if (!(e as CustomEvent).detail)
          down();
      });
    },
    // The socket is the viewer's; closing the console leaves it alone.
    close() {},
  };
}

interface Waiter {
  resolve: (reply: Uint8Array) => void;
  reject: (e: Error) => void;
  timer: ReturnType<typeof setTimeout>;
  t0: number;
}

export class RemoteBridge implements HostBridge {
  readonly stats: BridgeStats = {
    ops: 0, statements: 0, bytesUp: 0, bytesDown: 0, otherBytesDown: 0, totalMs: 0, maxMs: 0,
  };
  private seq = 0;
  private opsSinceEnd = 0;
  private readonly waiting = new Map<number, Waiter>();
  private closed = false;

  private constructor(private readonly port: Port, private readonly timeoutMs: number) {
    port.listen((bytes) => this.onFrame(bytes),
                (size) => { this.stats.otherBytesDown += size; },
                () => this.lost());
  }

  /// Whether this page's wasm viewer offers its scene socket to the bridge.
  static get viewerAvailable(): boolean {
    return typeof window.fcviewerBridgeSend === 'function';
  }

  /// A bridge on the viewer's own scene socket.  The viewer owns the
  /// connection: it reconnects it, and a document switch is its to make.
  static viewer(timeoutMs = 60000): RemoteBridge {
    return new RemoteBridge(viewerPort(), timeoutMs);
  }

  /// A bridge over a socket already open to /scene.
  static attach(socket: WebSocket, timeoutMs = 60000): RemoteBridge {
    return new RemoteBridge(socketPort(socket), timeoutMs);
  }

  /// A connection of its own to `server`, an http(s) origin.
  static connect(server: string, opts: RemoteOptions = {}): Promise<RemoteBridge> {
    const url = new URL('/scene', server);
    url.protocol = url.protocol === 'https:' ? 'wss:' : 'ws:';
    if (opts.token)
      url.searchParams.set('token', opts.token);
    if (opts.doc)
      url.searchParams.set('doc', opts.doc);
    const socket = new WebSocket(url.href);
    return new Promise((resolve, reject) => {
      socket.addEventListener('open', () => {
        const bridge = RemoteBridge.attach(socket, opts.timeoutMs ?? 60000);
        const hello: Record<string, string> = { cmd: 'hello', client: opts.client ?? 'sandbox-console' };
        if (opts.token)
          hello.token = opts.token;
        if (opts.doc)
          hello.doc = opts.doc;
        socket.send(JSON.stringify(hello));
        resolve(bridge);
      }, { once: true });
      socket.addEventListener('error', () => reject(new Error(`cannot open ${url.origin}/scene`)),
                              { once: true });
    });
  }

  /// Whether a document switch is this bridge's to send (a socket of its
  /// own) rather than the viewer's.
  get ownsConnection(): boolean {
    return !!this.port.switchDoc;
  }

  roundTrip(request: Uint8Array): Promise<Uint8Array> {
    if (this.closed)
      return Promise.reject(new Error('the console closed its connection'));
    if (!this.port.open)
      return Promise.reject(new Error('the connection to the server is down'));
    const seq = this.seq = (this.seq + 1) >>> 0;
    const frame = new Uint8Array(6 + request.length);
    frame[0] = TAG;
    frame[1] = KIND_OP;
    new DataView(frame.buffer).setUint32(2, seq, true);
    frame.set(request, 6);
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.waiting.delete(seq);
        reject(new Error(`bridge op ${seq} unanswered after ${this.timeoutMs} ms`));
      }, this.timeoutMs);
      const w = { resolve, reject, timer, t0: performance.now() };
      this.waiting.set(seq, w);
      if (!this.port.send(frame)) {
        this.waiting.delete(seq);
        clearTimeout(timer);
        reject(new Error('the connection to the server is down'));
        return;
      }
      this.stats.ops++;
      this.stats.bytesUp += frame.length;
      this.opsSinceEnd++;
    });
  }

  endStatement(): void {
    if (!this.opsSinceEnd || this.closed || !this.port.open)
      return;
    this.opsSinceEnd = 0;
    const frame = new Uint8Array(6);
    frame[0] = TAG;
    frame[1] = KIND_END;
    if (this.port.send(frame))
      this.stats.statements++;
  }

  /// Join another served document on a connection of our own, as the
  /// viewer's switch does (main.cpp fcviewer_switch_doc).  The host drops the
  /// connection's bridge endpoint with the old document's handles.  On the
  /// viewer's socket the viewer switches, and this does nothing.
  switchDoc(name: string): void {
    this.port.switchDoc?.(name);
  }

  close(): void {
    this.closed = true;
    this.lost();
    this.port.close();
  }

  private onFrame(bytes: Uint8Array) {
    if (bytes.length < 8 || MAGIC.some((m, i) => bytes[i] !== m)) {
      this.stats.otherBytesDown += bytes.length;
      return;
    }
    const seq = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength).getUint32(4, true);
    const w = this.waiting.get(seq);
    if (!w)
      return;
    this.waiting.delete(seq);
    clearTimeout(w.timer);
    const ms = performance.now() - w.t0;
    this.stats.totalMs += ms;
    this.stats.maxMs = Math.max(this.stats.maxMs, ms);
    this.stats.bytesDown += bytes.length;
    w.resolve(bytes.slice(8));
  }

  /// The connection went: nothing pending will be answered -- a reconnect is
  /// a new connection, and its endpoint never saw these ops.
  private lost() {
    const why = this.closed || !this.port.reconnects
      ? 'the connection to the server closed'
      : 'the connection to the server dropped; it is reconnecting';
    for (const w of this.waiting.values()) {
      clearTimeout(w.timer);
      w.reject(new Error(why));
    }
    this.waiting.clear();
    this.opsSinceEnd = 0;
    if (!this.port.reconnects)
      this.closed = true;
  }
}
