// Entry point of latency-test.html, the C5 bench of docs/Sandbox.md 7.20: what
// a console statement costs when the host is a LAN or a tunnel away, and what
// the guest costs the page in memory.  The page boots the guest over a socket of
// its own, runs a fixed set of statements a console user types -- a property,
// the object list, a loop over a shape's edges -- each `reps` times, and records
// for each the bridge ops it made and its wall time.  The RTT is whatever sits
// between the page and its server: tests/gui/sandbox-latency-browser.py puts
// scripts/delay-proxy.js there.  Memory is sampled at three marks through
// window.fcxMark, which scripts/console-drive.js installs (the page's processes,
// read from /proc); a page without it records only what JavaScript can see.
// The report is left on window.fcxLatency.

import { BrowserGuest } from './guest.js';
import { RemoteBridge } from './remote.js';

declare global {
  interface Window {
    /// Installed by the driver: sample the browser's processes now.
    fcxMark?: (label: string) => Promise<unknown>;
    /// Chrome with --js-flags=--expose-gc.
    gc?: () => void;
  }
}

const out = document.createElement('pre');
out.style.cssText = 'font: 12px/1.45 ui-monospace, monospace; padding: 12px; white-space: pre-wrap';
document.body.appendChild(out);
const lines: string[] = [];
const sink = (line: string) => {
  lines.push(line);
  out.textContent = lines.join('\n');
  console.log(line);
};

const params = new URLSearchParams(location.search);
const server = params.get('server') ?? location.origin;
const token = params.get('token') ?? undefined;
const doc = params.get('doc') ?? '';
const reps = Math.max(1, +(params.get('reps') ?? 3));

const D = "__import__('FreeCAD').ActiveDocument";

/// What a console user types.  `exec` ones are statements, the rest
/// expressions; the gate's document has Box, 48 more boxes and Poly, a
/// 100-edge polygon.
const STATEMENTS: { name: string; src: string; exec?: boolean }[] = [
  { name: 'doc.Name', src: `${D}.Name` },
  { name: 'Box.Length', src: `${D}.getObject('Box').Length.Value` },
  { name: 'Box.Length = 12', src: `import FreeCAD\nFreeCAD.ActiveDocument.getObject('Box').Length = 12\n`, exec: true },
  { name: 'len(doc.Objects)', src: `len(${D}.Objects)` },
  { name: '[o.Name for o in doc.Objects]', src: `[o.Name for o in ${D}.Objects]` },
  { name: '[o.Placement.Base.x for o in doc.Objects]', src: `[o.Placement.Base.x for o in ${D}.Objects]` },
  { name: 'Box.Shape.Volume', src: `${D}.getObject('Box').Shape.Volume` },
  { name: 'len(Box.Shape.Edges)', src: `len(${D}.getObject('Box').Shape.Edges)` },
  { name: 'sum(e.Length for e in Box.Shape.Edges)', src: `sum(e.Length for e in ${D}.getObject('Box').Shape.Edges)` },
  { name: 'sum(e.Length for e in Poly.Shape.Edges)', src: `sum(e.Length for e in ${D}.getObject('Poly').Shape.Edges)` },
  { name: '[v.Point.y for v in Poly.Shape.Vertexes]', src: `[v.Point.y for v in ${D}.getObject('Poly').Shape.Vertexes]` },
];

interface Row {
  name: string;
  ok: boolean;
  /// Bridge ops one run makes, and the statement ends among them.
  ops: number;
  /// Wall time of each run, and the part of it spent waiting on the bridge.
  ms: number[];
  bridgeMs: number[];
  detail: string;
}

const report = {
  ok: false,
  jspi: BrowserGuest.jspi,
  doc,
  reps,
  connectMs: 0,
  runtimeMs: 0,
  wheelsMs: 0,
  rows: [] as Row[],
  memory: {} as Record<string, unknown>,
  stats: null as unknown,
  error: '',
};

async function mark(label: string, guest?: BrowserGuest) {
  window.gc?.();
  await new Promise((r) => setTimeout(r, 200));
  const perf = (performance as any).memory;
  const m: Record<string, unknown> = {
    jsHeapUsedMB: perf ? perf.usedJSHeapSize / 1048576 : null,
    jsHeapTotalMB: perf ? perf.totalJSHeapSize / 1048576 : null,
    wasmMB: guest ? guest.memoryMB : null,
  };
  if (window.fcxMark)
    m.processes = await window.fcxMark(label);
  report.memory[label] = m;
}

const brief = (v: unknown) => {
  const s = JSON.stringify(v) ?? String(v);
  return s.length > 160 ? s.slice(0, 157) + '...' : s;
};

(async () => {
  sink(`sandbox latency, C5 -- ${server}, document ${doc}, ${reps} reps`);
  try {
    await mark('page');
    let t = performance.now();
    const bridge = await RemoteBridge.connect(server, { token, doc, client: 'sandbox-latency-test' });
    report.connectMs = Math.round(performance.now() - t);
    const guest = await BrowserGuest.boot(new URL('/pyodide/', server).href, {
      token,
      bridge,
      stdout: (s) => sink('stdout: ' + s),
      stderr: (s) => sink('stderr: ' + s),
    });
    report.runtimeMs = Math.round(guest.timing.runtimeMs);
    report.wheelsMs = Math.round(guest.timing.wheelsMs);
    sink(`connect ${report.connectMs} ms, runtime ${report.runtimeMs} ms, wheels ${report.wheelsMs} ms`);
    await mark('booted', guest);

    for (const st of STATEMENTS) {
      const row: Row = { name: st.name, ok: true, ops: 0, ms: [], bridgeMs: [], detail: '' };
      for (let i = 0; i < reps; i++) {
        const ops0 = bridge.stats.ops;
        const b0 = bridge.stats.totalMs;
        t = performance.now();
        const r = await (st.exec ? guest.exec(st.src) : guest.runPython(st.src));
        row.ms.push(performance.now() - t);
        row.bridgeMs.push(bridge.stats.totalMs - b0);
        row.ops = bridge.stats.ops - ops0;
        if (!r.ok) {
          row.ok = false;
          row.detail = brief(r);
          break;
        }
        row.detail = brief(r.val);
      }
      report.rows.push(row);
      const best = Math.min(...row.ms);
      sink(`${row.ok ? 'PASS' : 'FAIL'} ${row.name} | ${row.ops} ops, best ${best.toFixed(1)} ms | ${row.detail}`);
    }
    await mark('bench', guest);

    report.stats = bridge.stats;
    report.ok = report.rows.every((r) => r.ok);
  } catch (e: any) {
    report.error = String(e?.stack ?? e);
    sink('FAIL boot | ' + report.error);
  }
  document.title = report.ok ? 'latency OK' : 'latency FAILED';
  (window as any).fcxLatency = report;
})();
