// A Python console session in the page (docs/Sandbox.md 7.20, C4): the guest
// booted from the FreeCAD that serves the document, its host bridge on the
// viewer's scene socket (a socket of its own on a page with no viewer), and
// the interpreter of console.py driven one line at a time.  No DOM here -- the panel (console.tsx) and its gate page
// (consolepanelmain.ts) sit on top.
//
// Every line enters the guest through fcx_call (BrowserGuest.call), so a
// statement's host calls suspend on the socket under JSPI and the statement
// ends as one bridge statement: the host drops its handles, and a name the
// console keeps re-resolves by its durable key on the next line.
//
// Output is the guest's own sys.stdout and sys.stderr, delivered as written
// (pyodide's raw write handler), not batched per line: a loop printing
// progress shows it at each host call, where the page gets to paint.
//
// Where the guest runs (C6).  Two transports, one session on top:
//   jspi    the guest in THIS thread, its host call suspended on a promise
//           (Chrome 137+, Firefox 139+)
//   worker  the guest in a worker, its host call parked in Atomics.wait while
//           this thread carries the op (every browser with cross-origin
//           isolation, which is how Safari gets a console at all)
// `auto` takes JSPI where there is JSPI, because that is the path C1-C5
// measured, and the worker otherwise.
//
// Interrupt: pyodide checks an interrupt buffer at bytecode boundaries.  Under
// JSPI the page can only set it while the guest is NOT running -- it runs on
// this thread -- which is exactly while a statement is suspended on a host
// call, so a loop that reaches the host is interruptible and a pure CPU loop
// is not.  In the worker the buffer is shared memory the page writes while the
// guest runs, and both are.

import { BrowserGuest, type SandboxGuest } from './guest.js';
import { RemoteBridge } from './remote.js';
import { WorkerGuest } from './workerguest.js';
import consoleSource from './console.py?raw';

export type Stream = 'out' | 'err';
/// Where the guest runs; see the note above.
export type Transport = 'auto' | 'jspi' | 'worker';
/// What push() says of a line: ran, still incomplete, did not compile, raised.
export type PushStatus = 'ok' | 'more' | 'syntax' | 'error';

export interface SessionOptions {
  /// The serving FreeCAD's origin.
  server: string;
  token?: string;
  /// The served document to join.
  doc?: string;
  /// The label the owner's sharing roster shows, for a connection of its own.
  client?: string;
  /// Ride the wasm viewer's scene socket: the console is then the same
  /// connection as the view.  Waits a while for the viewer to install its
  /// hook; a viewer that never does gets a connection of its own, said so
  /// through `output`.
  viewerSocket?: boolean;
  output: (text: string, stream: Stream) => void;
  /// Boot progress, for the panel's status line.
  progress?: (what: string) => void;
  /// Where the guest runs (default 'auto').  A gate forces one.
  transport?: Transport;
}

/// The transport `auto` would take, and why there is none.
export function chooseTransport(want: Transport = 'auto'): Transport | null {
  if (want === 'jspi')
    return BrowserGuest.jspi ? 'jspi' : null;
  if (want === 'worker')
    return WorkerGuest.supported ? 'worker' : null;
  if (BrowserGuest.jspi)
    return 'jspi';
  return WorkerGuest.supported ? 'worker' : null;
}

/// How long a console opened with the page waits for the viewer's hook.
const VIEWER_WAIT_MS = 20000;

async function waitFor(cond: () => boolean, ms: number): Promise<boolean> {
  const t0 = performance.now();
  while (!cond()) {
    if (performance.now() - t0 > ms)
      return false;
    await new Promise((r) => setTimeout(r, 100));
  }
  return true;
}

export class ConsoleSession {
  private readonly intr: Int32Array;
  private running = 0;

  private constructor(readonly guest: SandboxGuest, readonly bridge: RemoteBridge,
                      readonly transport: Transport,
                      private readonly output: (text: string, stream: Stream) => void) {
    // Shared memory in the worker, where the guest reads it from another
    // thread; a plain array on this one, where it is the same memory anyway.
    this.intr = transport === 'worker'
      ? new Int32Array(new SharedArrayBuffer(4))
      : new Int32Array(1);
    guest.setInterruptBuffer(this.intr);
  }

  static get supported(): boolean {
    return chooseTransport() !== null;
  }

  static async open(opts: SessionOptions): Promise<ConsoleSession> {
    const transport = chooseTransport(opts.transport);
    if (!transport)
      throw new Error(opts.transport === 'worker'
        ? 'this page is not cross-origin isolated, so the console cannot run the '
          + 'interpreter in a worker'
        : opts.transport === 'jspi'
        ? 'this browser cannot suspend WebAssembly on a promise (no JSPI)'
        : 'this browser cannot run the console: it has neither JavaScript Promise '
          + 'Integration (Chrome 137, Firefox 139 and later) nor shared memory on '
          + 'this page');
    const say = opts.progress ?? (() => {});
    say('connecting');
    let bridge: RemoteBridge;
    if (opts.viewerSocket && await waitFor(() => RemoteBridge.viewerAvailable, VIEWER_WAIT_MS)) {
      bridge = RemoteBridge.viewer();
    }
    else {
      if (opts.viewerSocket)
        opts.output('this viewer offers no bridge on its socket; the console opens a '
                    + 'connection of its own, which the owner sees and sets apart\n', 'err');
      bridge = await RemoteBridge.connect(opts.server, {
        token: opts.token, doc: opts.doc, client: opts.client ?? 'python-console',
      });
    }
    try {
      say('loading the Python runtime'
          + (transport === 'worker' ? ' in a worker' : ''));
      const base = new URL('/pyodide/', opts.server).href;
      // Boot messages before the raw writers are installed: batched per line,
      // which is what pyodide's own loader output wants.
      const guest: SandboxGuest = transport === 'worker'
        ? await WorkerGuest.boot(base, {
            token: opts.token, bridge,
            output: (text, stream) => opts.output(text, stream),
          })
        : await BrowserGuest.boot(base, {
            token: opts.token,
            bridge,
            stdout: (s) => opts.output(s + '\n', 'out'),
            stderr: (s) => opts.output(s + '\n', 'err'),
          });
      guest.setRawOutput((text, stream) => opts.output(text, stream));
      say('starting the console');
      const installed = await guest.call({ op: 'exec', src: consoleSource, module: 'fcx_console' });
      if (!installed.ok)
        throw new Error(`the console module did not load: ${installed.exc}: ${installed.msg}`);
      return new ConsoleSession(guest, bridge, transport, opts.output);
    } catch (e) {
      bridge.close();
      throw e;
    }
  }

  /// Whether a line is running (or queued behind one).
  get busy(): boolean {
    return this.running > 0;
  }

  /// One line of input; see console.py push().
  async push(line: string): Promise<PushStatus> {
    this.running++;
    try {
      Atomics.store(this.intr, 0, 0);
      const r = await this.guest.call({
        op: 'eval', src: "__import__('fcx_console').push(line)", bindings: { line },
      });
      if (r.ok)
        return r.val as PushStatus;
      // Raised outside the statement itself -- an interrupt landing between
      // the statement and push()'s return, or the guest refusing the call.
      this.guest.call({ op: 'eval', src: "__import__('fcx_console').reset()" }).catch(() => {});
      return this.fail(`${r.exc}: ${r.msg}\n`);
    } catch (e) {
      return this.fail(`${e}\n`);
    } finally {
      this.running--;
    }
  }

  /// Completions for the word at the end of `source`.
  async complete(source: string): Promise<{ items: string[]; start: number }> {
    const r = await this.guest.call({
      op: 'eval', src: "__import__('fcx_console').complete(src)", bindings: { src: source },
    });
    if (!r.ok || !Array.isArray(r.val))
      return { items: [], start: source.length };
    return { items: r.val[0] ?? [], start: r.val[1] ?? source.length };
  }

  /// Drop a half-typed statement.
  reset(): Promise<unknown> {
    return this.guest.call({ op: 'eval', src: "__import__('fcx_console').reset()" });
  }

  /// Raise KeyboardInterrupt in the running statement: at its next host call
  /// under JSPI, at its next bytecode boundary in the worker.
  interrupt(): void {
    if (this.busy)
      Atomics.store(this.intr, 0, 2);  // SIGINT
  }

  /// Whether the console is the viewer's own connection.
  get onViewerSocket(): boolean {
    return !this.bridge.ownsConnection;
  }

  /// Follow the viewer to another served document.  The host starts the
  /// connection's bridge over with an empty handle table; a name the console
  /// holds from the old document no longer resolves.  On the viewer's socket
  /// the viewer's own switch did it, and this sends nothing.
  switchDoc(name: string): void {
    this.bridge.switchDoc(name);
  }

  close(): void {
    this.guest.close();
    this.bridge.close();
  }

  private fail(text: string): PushStatus {
    Atomics.store(this.intr, 0, 0);
    this.output(text, 'err');
    return 'error';
  }
}
