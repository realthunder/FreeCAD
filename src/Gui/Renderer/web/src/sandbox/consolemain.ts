// Entry point of console-test.html, the C1 gate of docs/Sandbox.md 7.20: the
// guest booted in this page from the FreeCAD serving it, the bridge
// unattached.  The page reads its server from its own origin (?server=
// names another) and passes the link's ?token= to boot.json.  The verdict is
// left on window.fcxConsole for scripts/console-drive.js.
//
// ?guest=worker boots the same guest in a worker instead (C6): the transport
// a browser with no JSPI takes, and the one thing this gate can prove without
// a host -- that the runtime and the wheel load and run there.  ?report=<url>
// POSTs the verdict, for a browser no driver attaches to (Safari).

import { BrowserGuest, type SandboxGuest } from './guest.js';
import { WorkerGuest } from './workerguest.js';

const out = document.createElement('pre');
out.style.cssText = 'font: 12px/1.45 ui-monospace, monospace; padding: 12px; white-space: pre-wrap';
document.body.appendChild(out);
const lines: string[] = [];
const sink = (line: string) => {
  lines.push(line);
  out.textContent = lines.join('\n');
  console.log(line);
};

interface Check { name: string; pass: boolean; detail: string }
const report = {
  ok: false,
  transport: '',
  ua: navigator.userAgent,
  jspi: BrowserGuest.jspi,
  isolated: WorkerGuest.supported,
  version: '',
  runtimeMs: 0,
  wheelsMs: 0,
  memoryMB: 0,
  failed: [] as string[],
  checks: [] as Check[],
  error: '',
};
const check = (name: string, pass: boolean, detail: unknown) => {
  const d = typeof detail === 'string' ? detail : JSON.stringify(detail);
  report.checks.push({ name, pass, detail: d });
  sink(`${pass ? 'PASS' : 'FAIL'} ${name} | ${d}`);
};

const params = new URLSearchParams(location.search);
const server = params.get('server') ?? location.origin;
const token = params.get('token') ?? undefined;

/// The unattached bridge, in the shape the worker path needs one: an empty
/// reply is what "nothing serves the bridge" looks like on the wire, and the
/// guest raises "host bridge unavailable" on it -- which is what this gate
/// checks either way.
const noHost = {
  roundTrip: () => Promise.resolve(new Uint8Array(0)),
  endStatement: () => {},
};

(async () => {
  sink(`sandbox console, C1 -- ${server}`);
  sink(navigator.userAgent);
  try {
    const base = new URL('/pyodide/', server).href;
    const worker = params.get('guest') === 'worker';
    report.transport = worker ? 'worker' : 'jspi';
    sink('the guest runs ' + (worker ? 'in a worker' : 'in this page'));
    const guest: SandboxGuest = worker
      ? await WorkerGuest.boot(base, {
          token, bridge: noHost,
          output: (text) => sink(text.replace(/\n$/, '')),
        })
      : await BrowserGuest.boot(base, {
          token,
          stdout: (s) => sink('stdout: ' + s),
          stderr: (s) => sink('stderr: ' + s),
        });
    report.version = guest.info.version;
    report.runtimeMs = Math.round(guest.timing.runtimeMs);
    report.wheelsMs = Math.round(guest.timing.wheelsMs);
    report.failed = guest.failed;
    sink(`pyodide ${guest.info.version} (abi ${guest.info.abi}): runtime ${report.runtimeMs} ms, `
         + `wheels ${report.wheelsMs} ms, ${guest.info.bundled.length} bundled, `
         + `${guest.info.packages.length} package(s)`);

    const seven = await guest.evalExpression('1 + 2 * 3');
    check('an expression evaluates in the page', seven.ok && seven.val === 7, seven);
    const q = await guest.evalExpression('(12 mm) * 2');
    check('a quantity crosses back', q.ok && q.val?.t === 'quantity' && q.val.v === 24, q);
    const v = await guest.runPython("__import__('sys').version");
    check('the guest is CPython 3.14', v.ok && String(v.val).startsWith('3.14'), v);
    const fc = await guest.runPython("hasattr(__import__('FreeCAD'), 'Vector')");
    check('the in-image FreeCAD module is loaded', fc.ok && fc.val === true, fc);
    const b = await guest.runPython("__import__('_fcx').op('len', 1)");
    check('the host bridge is unattached', !b.ok && /host bridge unavailable/.test(b.msg), b);
    check('every bundled wheel and package loaded', guest.failed.length === 0, guest.failed);
    report.memoryMB = Math.round(guest.memoryMB);
    sink(`guest linear memory ${report.memoryMB} MB`);
    report.ok = report.checks.every((c) => c.pass);
  } catch (e: any) {
    report.error = String(e?.stack ?? e);
    sink('FAIL boot | ' + report.error);
  }
  document.title = report.ok ? 'console OK' : 'console FAILED';
  (window as any).fcxConsole = report;
  const back = params.get('report');
  if (back) {
    try {
      await fetch(back, { method: 'POST', body: JSON.stringify(report) });
    } catch (e) {
      sink('FAIL report | ' + e);
    }
  }
})();
