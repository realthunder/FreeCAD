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
// Transport-shaped on purpose: bytes in, a promise of bytes out.  How the guest
// waits for the promise is guest.ts's business -- JSPI's suspending import
// today, a Worker blocked in Atomics.wait for Safari later (C6), which swaps
// only that side.

import type { HostBridge } from './guest.js';

const MAGIC = [0x46, 0x43, 0x53, 0x42]; // FCSB
const TAG = 0x53; // 'S'
const KIND_OP = 0;
const KIND_END = 1;

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
  private closedWith: string | null = null;

  private constructor(private readonly socket: WebSocket, private readonly timeoutMs: number) {
    socket.binaryType = 'arraybuffer';
    socket.addEventListener('message', (e) => this.onMessage(e));
    socket.addEventListener('close', () => this.fail('the connection to the server closed'));
  }

  /// A bridge over a socket already open to /scene -- the viewer's own.
  static attach(socket: WebSocket, timeoutMs = 60000): RemoteBridge {
    return new RemoteBridge(socket, timeoutMs);
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
        const bridge = new RemoteBridge(socket, opts.timeoutMs ?? 60000);
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

  roundTrip(request: Uint8Array): Promise<Uint8Array> {
    if (this.closedWith)
      return Promise.reject(new Error(this.closedWith));
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
      this.waiting.set(seq, { resolve, reject, timer, t0: performance.now() });
      this.stats.ops++;
      this.stats.bytesUp += frame.length;
      this.opsSinceEnd++;
      this.socket.send(frame);
    });
  }

  endStatement(): void {
    if (!this.opsSinceEnd || this.closedWith || this.socket.readyState !== WebSocket.OPEN)
      return;
    this.opsSinceEnd = 0;
    const frame = new Uint8Array(6);
    frame[0] = TAG;
    frame[1] = KIND_END;
    this.stats.statements++;
    this.socket.send(frame);
  }

  /// Join another served document on this connection, as the viewer's own
  /// switch does (main.cpp fcviewer_switch_doc).  The host drops the
  /// connection's bridge endpoint with the old document's handles.
  switchDoc(name: string): void {
    if (this.socket.readyState === WebSocket.OPEN)
      this.socket.send(JSON.stringify({ cmd: 'switch', doc: name }));
  }

  close(): void {
    this.socket.close();
  }

  private onMessage(e: MessageEvent) {
    if (!(e.data instanceof ArrayBuffer) || e.data.byteLength < 8) {
      this.stats.otherBytesDown += typeof e.data === 'string' ? e.data.length : e.data.byteLength;
      return;
    }
    const bytes = new Uint8Array(e.data);
    for (let i = 0; i < 4; ++i)
      if (bytes[i] !== MAGIC[i]) {
        this.stats.otherBytesDown += bytes.length;
        return;
      }
    const seq = new DataView(e.data).getUint32(4, true);
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

  private fail(why: string) {
    this.closedWith = why;
    for (const w of this.waiting.values()) {
      clearTimeout(w.timer);
      w.reject(new Error(why));
    }
    this.waiting.clear();
  }
}
