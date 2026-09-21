// The page's half of a guest in a worker (docs/Sandbox.md 7.20, C6): the
// SandboxGuest a console session talks to, with the interpreter one thread
// away and the socket still here.
//
// It is the JSPI path's twin, not a variant of it: the same BrowserGuest boots
// in the worker (guestworker.ts), over the same RemoteBridge on the same
// connection, under the same host, principal and catalog.  What differs is
// only how the guest's synchronous host call is made to wait -- Atomics.wait
// in the worker rather than a suspended wasm stack here (sabwire.ts) -- and
// two things follow from the thread:
//
//   - a statement that never reaches the host is still interruptible: the
//     interrupt buffer is shared memory the page writes while the guest runs,
//     where the JSPI path can only write it between host calls (C4);
//   - the page paints while a statement runs.
//
// It needs cross-origin isolation for the SharedArrayBuffer, which the served
// page's COOP/COEP headers give it (SceneServer.cpp).

import type { BootInfo, GuestTiming, HostBridge, SandboxGuest } from './guest.js';
import { CHUNK, CHUNK_READY, DATA, DATA_BYTES, FAILED, STATE, TOTAL, makeSab, sabSupported }
  from './sabwire.js';

export interface WorkerGuestOptions {
  /// The link's ?token=, for boot.json.
  token?: string;
  /// The host bridge, on this thread: the worker's ops come through it.
  bridge: HostBridge;
  /// Load FreeCAD's bundled wheels (default true, as the desktop does).
  bundled?: boolean;
  /// Boot-time messages of the runtime's own loader.
  output?: (text: string, stream: 'out' | 'err') => void;
}

interface Pending {
  resolve: (value: any) => void;
  reject: (e: Error) => void;
}

export class WorkerGuest implements SandboxGuest {
  static get supported(): boolean {
    return sabSupported();
  }

  private readonly ctl: Int32Array;
  private readonly bytes: Uint8Array;
  private readonly pending = new Map<number, Pending>();
  private seq = 0;
  private reply: Uint8Array | null = null;
  private at = 0;
  private closed = false;
  private out: (text: string, stream: 'out' | 'err') => void;

  memoryMB = 0;

  /// What the worker answered its boot with; until then the guest is not
  /// handed out, but it is already routing (a wheel's import is free to make
  /// a host call of its own).
  info!: BootInfo;
  timing!: GuestTiming;
  failed: string[] = [];

  private constructor(
    private readonly worker: Worker,
    sab: SharedArrayBuffer,
    private readonly bridge: HostBridge,
    output: (text: string, stream: 'out' | 'err') => void,
  ) {
    this.ctl = new Int32Array(sab, 0, 8);
    this.bytes = new Uint8Array(sab, DATA, DATA_BYTES);
    this.out = output;
  }

  /// Boot the guest in a worker from `base`, the absolute URL of a server's
  /// /pyodide/.  Resolves when the guest answers, as BrowserGuest.boot does.
  static boot(base: string, opts: WorkerGuestOptions): Promise<WorkerGuest> {
    if (!sabSupported())
      throw new Error('this page cannot run the console in a worker: it is not '
                      + 'cross-origin isolated (no SharedArrayBuffer)');
    const worker = new Worker(new URL('./guestworker.ts', import.meta.url),
                              { type: 'module', name: 'fcx-sandbox-guest' });
    const sab = makeSab();
    const guest = new WorkerGuest(worker, sab, opts.bridge, opts.output ?? (() => {}));
    return new Promise<WorkerGuest>((resolve, reject) => {
      worker.onerror = (e) => reject(new Error('the sandbox worker failed: ' + e.message));
      worker.onmessage = (e: MessageEvent) => {
        const m = e.data;
        if (m.t === 'booted') {
          guest.info = m.info;
          guest.timing = m.timing;
          guest.failed = m.failed;
          guest.memoryMB = m.mem;
          resolve(guest);
          return;
        }
        if (m.t === 'bootfail') {
          worker.terminate();
          reject(new Error(m.error));
          return;
        }
        // Everything else -- a host op the boot itself makes, output -- is
        // the guest's from the first message.
        guest.onMessage(m);
      };
      worker.postMessage({
        t: 'boot', base, token: opts.token, bundled: opts.bundled, sab,
      });
    });
  }

  call(request: Record<string, any>): Promise<any> {
    if (this.closed)
      return Promise.reject(new Error('the console closed its interpreter'));
    const id = ++this.seq;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.worker.postMessage({ t: 'call', id, request });
    });
  }

  /// One FreeCAD expression, as BrowserGuest does it -- the request is the
  /// same; only the thread it is made on differs.
  evalExpression(src: string, bindings?: Record<string, any>,
                 ctx?: { doc: string; obj: string }): Promise<any> {
    const req: Record<string, any> = { op: 'eval', lang: 'expr', src };
    if (bindings)
      req.bindings = bindings;
    if (ctx)
      req.ctx = ctx;
    return this.call(req);
  }

  runPython(src: string): Promise<any> {
    return this.call({ op: 'eval', src });
  }

  exec(src: string): Promise<any> {
    return this.call({ op: 'exec', src });
  }

  setInterruptBuffer(buffer: Int32Array): void {
    // Shared memory or nothing: a plain Int32Array would be a copy the worker
    // never sees, and the interrupt would be silently dead.
    if (!(buffer.buffer instanceof SharedArrayBuffer))
      throw new Error('a worker guest needs its interrupt buffer in shared memory');
    this.worker.postMessage({ t: 'interrupt', buffer: buffer.buffer });
  }

  setRawOutput(write: (text: string, stream: 'out' | 'err') => void): void {
    this.out = write;
  }

  close(): void {
    this.closed = true;
    for (const p of this.pending.values())
      p.reject(new Error('the console closed its interpreter'));
    this.pending.clear();
    this.worker.terminate();
  }

  /** @internal */ onMessage(m: any) {
    switch (m.t) {
      case 'op':
        this.hostOp(m.bytes);
        break;
      case 'next':
        this.writeChunk();
        break;
      case 'end':
        this.bridge.endStatement?.();
        break;
      case 'out':
        this.out(m.text, m.stream);
        break;
      case 'result': {
        this.memoryMB = m.mem;
        const p = this.pending.get(m.id);
        if (!p)
          break;
        this.pending.delete(m.id);
        if (m.error)
          p.reject(new Error(m.error));
        else
          p.resolve(m.value);
        break;
      }
      default:
        break;
    }
  }

  /// One op off the worker, over the socket and back into shared memory.  The
  /// worker is parked in Atomics.wait for the whole of this.
  private async hostOp(request: Uint8Array) {
    let reply: Uint8Array | null = null;
    try {
      reply = await this.bridge.roundTrip(request);
      if (!reply.length) {
        this.out('nothing on the server answers the sandbox bridge\n', 'err');
        reply = null;
      }
    } catch (e) {
      this.out('host bridge: ' + e + '\n', 'err');
      reply = null;
    }
    if (this.closed)
      return;
    if (!reply) {
      this.failOp();
      return;
    }
    this.reply = reply;
    this.at = 0;
    this.writeChunk();
  }

  /// The next slice of the reply, and the wake that goes with it.
  private writeChunk() {
    const reply = this.reply;
    if (!reply)
      return;
    const n = Math.min(reply.length - this.at, DATA_BYTES);
    this.bytes.set(reply.subarray(this.at, this.at + n), 0);
    this.at += n;
    Atomics.store(this.ctl, TOTAL, reply.length);
    Atomics.store(this.ctl, CHUNK, n);
    Atomics.store(this.ctl, STATE, CHUNK_READY);
    Atomics.notify(this.ctl, STATE);
    if (this.at >= reply.length)
      this.reply = null;
  }

  /// Nothing answered: the worker's import returns -1 and the guest raises,
  /// which is what the JSPI path does with a rejected round trip.
  private failOp() {
    this.reply = null;
    Atomics.store(this.ctl, TOTAL, -1);
    Atomics.store(this.ctl, CHUNK, 0);
    Atomics.store(this.ctl, STATE, FAILED);
    Atomics.notify(this.ctl, STATE);
  }
}
