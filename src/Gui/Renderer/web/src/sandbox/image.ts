// The browser counterpart of App::ExpressionSandbox::ImageHost
// (src/App/ExpressionImageHost.cpp): load the sandbox image, run fcx_init
// once, and drive fcx_call round trips.  The call sequence is deliberately
// identical to the C++ host's roundTrip() -- alloc, copy, call, free the
// request, read the u32-prefixed reply, free the reply -- so the two hosts
// cannot drift apart.
//
// ON THE BRIDGE.  The image's `fcx.host_call` import is SYNCHRONOUS: it is
// called from inside the C++ evaluator, mid-expression.  On the desktop the
// host answers it in the same thread.  In the browser the only thing worth
// reaching back to is the FreeCAD host at the far end of a WebSocket, which
// is asynchronous, and a synchronous wasm import cannot await a promise on
// the main thread.  So this host does NOT pretend: it leaves the bridge
// unattached, the import returns -1, and the image raises "host bridge
// unavailable" for anything needing a live host object.
//
// That is not a gap so much as the design (docs/ExpressionSandbox.md sec 10):
// the host PRE-RESOLVES every identifier into a bindings pack before the
// evaluation starts, so an expression over already-resolved values never
// reaches back.  Reach-back needs the image on a worker with
// Atomics.wait over a SharedArrayBuffer (hence cross-origin isolation) or
// JSPI; both are deferred until something actually needs them.

import { Wasi, makeTree, type WasiOptions } from './wasi.js';
import * as cbor from './cbor.js';

/// A value on the wire (Phase 0 sec 6.4).  Plain JSON values pass through;
/// the tagged forms carry the geometry/quantity types.
export type WireValue =
  | null | boolean | number | string | WireValue[]
  | { t: 'quantity'; v: number; u: number[] }
  | { t: 'vec'; v: [number, number, number] }
  | { t: 'rot'; v: [number, number, number, number] }
  | { t: 'pla'; p: [number, number, number]; r: [number, number, number, number] }
  | { t: 'mat'; v: number[] }
  | { t: 'bb'; v: number[] }
  | { t: 'h'; id: number; ty: string }
  | { [key: string]: any };

export type EvalReply =
  | { ok: true; val: WireValue }
  | { ok: false; exc: string; msg: string };

export interface ImageManifest {
  image: string;
  libDir: string;
  lib: string[];
  imageBytes?: number;
  libBytes?: number;
}

export interface ImageTiming {
  fetchMs: number;
  compileInstantiateMs: number;
  initMs: number;
}

interface FcxExports {
  memory: WebAssembly.Memory;
  _initialize(): void;
  fcx_init(): number;
  fcx_alloc(n: number): number;
  fcx_free(p: number): void;
  fcx_call(req: number, len: number): number;
}

export class SandboxImage {
  wasi!: Wasi;
  timing!: ImageTiming;
  private instance!: WebAssembly.Instance;

  private get exports() { return this.instance.exports as unknown as FcxExports; }
  private get mem() { return new Uint8Array(this.exports.memory.buffer); }

  /// Fetch, instantiate and initialize.  `baseUrl` is the directory holding
  /// fcx.json, as produced by tools/pack_image.py.
  static async load(baseUrl: string, opts: WasiOptions = {}): Promise<SandboxImage> {
    const img = new SandboxImage();
    const base = baseUrl.endsWith('/') ? baseUrl : baseUrl + '/';
    const t0 = performance.now();

    const manifest: ImageManifest = await (await fetch(base + 'fcx.json')).json();
    const files: Record<string, Uint8Array> = {};
    await Promise.all(manifest.lib.map(async (name) => {
      const r = await fetch(base + manifest.libDir + '/' + name);
      if (!r.ok) throw new Error('sandbox stdlib missing: ' + name);
      files[name] = new Uint8Array(await r.arrayBuffer());
    }));
    const t1 = performance.now();

    img.wasi = new Wasi(makeTree(files), opts);
    const { instance } = await WebAssembly.instantiateStreaming(
      fetch(base + manifest.image),
      {
        wasi_snapshot_preview1: img.wasi.imports,
        // Unattached on purpose -- see the note at the top of this file.
        fcx: { host_call: () => -1, host_fetch: () => -1 },
      },
    );
    img.instance = instance;
    img.wasi.memory = (instance.exports as any).memory;
    const t2 = performance.now();

    img.exports._initialize();
    const rc = img.exports.fcx_init();
    if (rc !== 0) throw new Error('sandbox fcx_init failed, rc ' + rc);

    img.timing = {
      fetchMs: t1 - t0,
      compileInstantiateMs: t2 - t1,
      initMs: performance.now() - t2,
    };
    return img;
  }

  /// One fcx_call round trip.  Mirrors ImageHost::Private::roundTrip.
  call(request: Record<string, any>): any {
    const e = this.exports;
    const req = cbor.encode(request);
    const ptr = e.fcx_alloc(req.length);
    if (!ptr) throw new Error('sandbox guest allocation failed');
    this.mem.set(req, ptr);
    let replyPtr = 0;
    try {
      replyPtr = e.fcx_call(ptr, req.length);
    } finally {
      e.fcx_free(ptr);
    }
    if (!replyPtr) throw new Error('sandbox image returned no reply');
    const mem = this.mem;                      // may have grown during the call
    const len = new DataView(mem.buffer).getUint32(replyPtr, true);
    const reply = cbor.decode(mem.subarray(replyPtr + 4, replyPtr + 4 + len));
    e.fcx_free(replyPtr);
    return reply;
  }

  /// Evaluate one FreeCAD expression.  `bindings` is the pre-resolved
  /// identifier pack (docs/ExpressionSandbox.md sec 7.3), keyed by each
  /// identifier's canonical toString() -- the host computes the same key from
  /// the same parse, so a hit costs no crossing at all.
  evalExpression(
    src: string,
    bindings?: Record<string, WireValue>,
    ctx?: { doc: string; obj: string },
  ): EvalReply {
    const req: Record<string, any> = { op: 'eval', lang: 'expr', src };
    if (bindings) req.bindings = bindings;
    if (ctx) req.ctx = ctx;
    return this.call(req);
  }

  /// Raw Python in the image's Ring 0 interpreter -- the debug path the C++
  /// smoke tools use.  Not a route the sheet takes.
  evalPython(src: string): EvalReply {
    return this.call({ op: 'eval', src });
  }
}
