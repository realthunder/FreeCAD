// The guest in the page (docs/Sandbox.md 7.20): pyodide and the fcx_image
// wheel booted from the FreeCAD that serves the document -- the browser twin
// of the desktop's pyodide runtime (src/App/ExpressionPyodideRuntime.cpp,
// src/App/PyodideHost/pyodide_glue.js).  The boot follows the glue step for
// step: the runtime, the two bridge imports merged into the main module's
// symbol table, the wheel, the bundled wheels and the user's package set, the
// _fcx_image import, the side module's exports.
//
// Where the files come from (src/Gui/SandboxServe.cpp): GET
// /pyodide/boot.json, behind the door because it names the owner's package
// set, says what to load, relative to /pyodide/.  The runtime, the wheels and
// the packages are served ahead of the door: pyodide fetches them by URL
// arithmetic that drops a ?token= tail, and takes a URL for a wheel only
// when it ENDS in .whl.
//
// The bridge.  The guest's host call is a synchronous wasm import made from
// deep inside CPython.  Given a `bridge`, the import is a WebAssembly.Suspending
// around it and every call enters the guest through WebAssembly.promising, so
// the wasm stack suspends while the round trip awaits (JSPI).  Probed
// 2026-09-14 in Chrome 153: a CBOR op and a fixed-layout op both suspended in
// the middle of a Python statement and resumed.  Without a bridge the import
// answers -1 and the guest raises "host bridge unavailable", as the reference
// image's page host does (image.ts).

import * as cbor from './cbor.js';

/// What GET /pyodide/boot.json answers.
export interface BootInfo {
  version: string;
  abi: string;
  /// The runtime directory, relative to /pyodide/.
  runtime: string;
  /// The fcx_image wheel, relative to /pyodide/.
  wheel: string;
  /// FreeCAD's bundled wheels, relative to /pyodide/, in load order.
  bundled: string[];
  /// The user's package set, relative to /pyodide/.
  packageBase: string;
  /// Lock-file names, in load order.
  packages: string[];
}

/// The guest's reach-back to the host, shaped as a transport: request bytes in,
/// a promise of reply bytes out (remote.ts carries it over the viewer's socket,
/// docs/Sandbox.md 7.20 C2).  An empty reply means nothing serves the bridge.
export interface HostBridge {
  roundTrip(request: Uint8Array): Promise<Uint8Array>;
  /// The guest call is over: the host may drop the handles it minted.
  endStatement?(): void;
}

export interface GuestOptions {
  /// The link's ?token=, for boot.json.
  token?: string;
  /// Reach-back to the host; unattached when absent.
  bridge?: HostBridge;
  stdout?: (text: string) => void;
  stderr?: (text: string) => void;
  /// Load FreeCAD's bundled wheels (default true, as the desktop does).
  bundled?: boolean;
}

export interface GuestTiming {
  /// loadPyodide: the runtime, the stdlib, the interpreter start.
  runtimeMs: number;
  /// The wheel, the bundled wheels, the package set, the _fcx_image import.
  wheelsMs: number;
}

interface SideExports {
  fcx_alloc(n: number): number;
  fcx_free(p: number): void;
  fcx_call(req: number, len: number): number;
}

export class BrowserGuest {
  /// Package load failures, reported and skipped as on the desktop.
  readonly failed: string[];
  private queue: Promise<unknown> = Promise.resolve();

  private constructor(
    readonly info: BootInfo,
    readonly timing: GuestTiming,
    /// The pyodide API object, for a console on top (7.20 C4).
    readonly pyodide: any,
    private readonly ex: SideExports,
    private readonly enter: (req: number, len: number) => number | Promise<number>,
    failed: string[],
    private readonly bridge: HostBridge | undefined,
  ) {
    this.failed = failed;
  }

  /// Whether this browser can suspend a wasm import on a promise.
  static get jspi(): boolean {
    const wa = WebAssembly as any;
    return typeof wa.Suspending === 'function' && typeof wa.promising === 'function';
  }

  /// Boot a guest from `base`, the absolute URL of a server's /pyodide/.
  static async boot(base: string, opts: GuestOptions = {}): Promise<BrowserGuest> {
    const root = base.endsWith('/') ? base : base + '/';
    const jspi = BrowserGuest.jspi;
    if (opts.bridge && !jspi)
      throw new Error('this browser has no JSPI (WebAssembly.Suspending), which the host bridge needs');
    const warn = opts.stderr ?? ((s: string) => console.warn(s));

    const q = opts.token ? '?token=' + encodeURIComponent(opts.token) : '';
    const r = await fetch(root + 'boot.json' + q, { cache: 'no-store' });
    const text = await r.text();
    let info: BootInfo & { error?: string };
    try {
      info = JSON.parse(text);
    } catch {
      throw new Error(`boot.json: HTTP ${r.status}`);
    }
    if (!r.ok || info.error)
      throw new Error('the sandbox runtime is not served: ' + (info.error ?? `HTTP ${r.status}`));

    const t0 = performance.now();
    const indexURL = new URL(info.runtime, root).href;
    const loader: any = await import(/* @vite-ignore */ indexURL + 'pyodide.mjs');
    const py = await loader.loadPyodide({
      indexURL,
      packageBaseUrl: new URL(info.packageBase, root).href,
      stdout: opts.stdout ?? ((s: string) => console.log(s)),
      stderr: warn,
    });
    const M = py._module;

    // The guest's two "env" imports, before any wheel: a side module's
    // imports resolve against the main module's table at instantiation.
    let hostCall: unknown = () => -1;
    let hostFetch: (dst: number, cap: number) => number = () => -1;
    if (opts.bridge) {
      const bridge = opts.bridge;
      let parked: Uint8Array | null = null;
      hostCall = new (WebAssembly as any).Suspending(async (ptr: number, len: number) => {
        const request = M.HEAPU8.slice(ptr, ptr + len);
        try {
          const reply = await bridge.roundTrip(request);
          if (!reply.length)
            throw new Error('nothing on the server answers the sandbox bridge');
          parked = reply;
          return reply.length;
        } catch (e) {
          parked = null;
          warn('host bridge: ' + e);
          return -1;
        }
      });
      hostFetch = (dst, cap) => {
        const reply = parked;
        parked = null;
        if (!reply || reply.length !== cap)
          return -1;
        M.HEAPU8.set(reply, dst);
        return cap;
      };
    }
    M.mergeLibSymbols({ fcx_host_call: hostCall, fcx_host_fetch: hostFetch }, 'fcx');

    const t1 = performance.now();
    await py.loadPackage(new URL(info.wheel, root).href, { messageCallback: () => {} });
    const failed: string[] = [];
    const more = [
      ...(opts.bundled === false ? [] : info.bundled.map((w) => new URL(w, root).href)),
      ...info.packages,
    ];
    if (more.length) {
      await py.loadPackage(more, {
        messageCallback: () => {},
        errorCallback: (m: string) => {
          failed.push(m);
          warn('sandbox package: ' + m);
        },
      });
    }
    // PyInit__fcx_image: the in-image FreeCAD module, the _fcx bridge, the
    // eval globals.
    py.pyimport('_fcx_image').destroy();

    let ex: SideExports | null = null;
    const libs = M.LDSO.loadedLibsByName;
    for (const name in libs) {
      if (/(^|\/)_fcx_image\..*\.so$/.test(name)) {
        ex = libs[name].exports;
        break;
      }
    }
    if (!ex)
      throw new Error('fcx_image is not among the loaded side modules');
    for (const fn of ['fcx_alloc', 'fcx_free', 'fcx_call'])
      if (typeof (ex as any)[fn] !== 'function')
        throw new Error('fcx_image exports no ' + fn);
    const enter = jspi ? (WebAssembly as any).promising(ex.fcx_call) : ex.fcx_call;
    return new BrowserGuest(info, { runtimeMs: t1 - t0, wheelsMs: performance.now() - t1 },
                            py, ex, enter, failed, opts.bridge);
  }

  /// The guest's linear memory, in MB.
  get memoryMB(): number {
    return this.pyodide._module.HEAPU8.length / 1048576;
  }

  /// One fcx_call round trip, the call sequence of the desktop host's
  /// roundTrip().  Calls queue: the guest has one stack, and a suspended call
  /// still holds it.
  call(request: Record<string, any>): Promise<any> {
    const run = this.queue.then(() => this.roundTrip(request));
    this.queue = run.catch(() => {});
    return run;
  }

  private async roundTrip(request: Record<string, any>): Promise<any> {
    const M = this.pyodide._module;
    const req = cbor.encode(request);
    const ptr = this.ex.fcx_alloc(req.length);
    if (!ptr)
      throw new Error('sandbox guest allocation failed');
    M.HEAPU8.set(req, ptr);
    let at = 0;
    try {
      at = await this.enter(ptr, req.length);
    } finally {
      this.ex.fcx_free(ptr);
      // One call is one statement: its handles on the host can go.  What
      // the guest keeps across statements re-resolves by its durable key.
      this.bridge?.endStatement?.();
    }
    if (!at)
      throw new Error('sandbox guest returned no reply');
    const heap: Uint8Array = M.HEAPU8;   // may have grown during the call
    const len = new DataView(heap.buffer).getUint32(at, true);
    const reply = cbor.decode(heap.subarray(at + 4, at + 4 + len));
    this.ex.fcx_free(at);
    return reply;
  }

  /// Evaluate one FreeCAD expression, as SandboxImage.evalExpression does.
  evalExpression(src: string, bindings?: Record<string, any>, ctx?: { doc: string; obj: string }) {
    const req: Record<string, any> = { op: 'eval', lang: 'expr', src };
    if (bindings)
      req.bindings = bindings;
    if (ctx)
      req.ctx = ctx;
    return this.call(req);
  }

  /// Raw Python in the guest's interpreter -- the debug path.
  runPython(src: string) {
    return this.call({ op: 'eval', src });
  }

  /// Python statements in the guest (FcxWire OpExec): their names are
  /// discarded, what they put in sys.modules or builtins stays.
  exec(src: string) {
    return this.call({ op: 'exec', src });
  }
}
