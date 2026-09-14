// Entry point of bridge-test.html, the C2 gate of docs/Sandbox.md 7.20: the
// guest booted in this page reaches the FreeCAD serving it over the socket --
// statements read and write the served document, and the desktop sees it.
// The page reads its server from its own origin (?server= names another), the
// link's ?token= and the document to join from ?doc=.  The verdict is left on
// window.fcxBridge for scripts/console-drive.js; the desktop side of the gate
// is tests/gui/sandbox-bridge-browser.py.

import { BrowserGuest } from './guest.js';
import { RemoteBridge } from './remote.js';

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
const params = new URLSearchParams(location.search);
const server = params.get('server') ?? location.origin;
const token = params.get('token') ?? undefined;
const doc = params.get('doc') ?? '';

const report = {
  ok: false,
  jspi: BrowserGuest.jspi,
  doc,
  runtimeMs: 0,
  wheelsMs: 0,
  stats: null as unknown,
  checks: [] as Check[],
  error: '',
};
const check = (name: string, pass: boolean, detail: unknown) => {
  const d = typeof detail === 'string' ? detail : JSON.stringify(detail);
  report.checks.push({ name, pass, detail: d });
  sink(`${pass ? 'PASS' : 'FAIL'} ${name} | ${d}`);
};

(async () => {
  sink(`sandbox bridge, C2 -- ${server}, document ${doc}`);
  try {
    const bridge = await RemoteBridge.connect(server, { token, doc, client: 'sandbox-bridge-test' });
    const guest = await BrowserGuest.boot(new URL('/pyodide/', server).href, {
      token,
      bridge,
      stdout: (s) => sink('stdout: ' + s),
      stderr: (s) => sink('stderr: ' + s),
    });
    report.runtimeMs = Math.round(guest.timing.runtimeMs);
    report.wheelsMs = Math.round(guest.timing.wheelsMs);

    const name = await guest.runPython("__import__('FreeCAD').ActiveDocument.Name");
    check('ActiveDocument is the served document', name.ok && name.val === doc, name);
    const objs = await guest.runPython("[o.Name for o in __import__('FreeCAD').ActiveDocument.Objects]");
    check('its objects are read over the socket',
          objs.ok && Array.isArray(objs.val) && objs.val.includes('Box'), objs);
    const write = await guest.exec("import FreeCAD\nFreeCAD.ActiveDocument.getObject('Box').Length = 25\n");
    check('a statement writes a property', write.ok, write);
    const back = await guest.runPython("__import__('FreeCAD').ActiveDocument.getObject('Box').Length.Value");
    check('the write reads back', back.ok && back.val === 25, back);
    const add = await guest.exec(
      "import FreeCAD\nb = FreeCAD.ActiveDocument.addObject('Part::Box', 'Remote')\nb.Height = 7\n");
    check('a statement adds an object', add.ok, add);
    const keep = await guest.exec(
      "import builtins, FreeCAD\nbuiltins.fcx_kept = FreeCAD.ActiveDocument.getObject('Box')\n");
    const kept = await guest.runPython('fcx_kept.Width.Value');
    check('an object kept across statements re-resolves', keep.ok && kept.ok && kept.val === 10,
          { keep, kept });
    const loop = await guest.runPython(
      "sum(__import__('FreeCAD').ActiveDocument.getObject('Box').Height.Value for _ in [0] * 20)");
    check('twenty suspended host calls inside one expression', loop.ok && loop.val === 200, loop);
    const deny = await guest.exec("import FreeCAD\nFreeCAD.newDocument('SandboxBridgeNope')\n");
    check('the served document cannot open another',
          !deny.ok && /PermissionError/.test(`${deny.exc} ${deny.msg}`), deny);

    report.stats = bridge.stats;
    const s = bridge.stats;
    sink(`bridge: ${s.ops} ops over ${s.statements} statements, mean `
         + `${(s.totalMs / Math.max(1, s.ops)).toFixed(2)} ms, worst ${s.maxMs.toFixed(1)} ms, `
         + `${s.bytesUp} bytes up, ${s.bytesDown} down`);
    report.ok = report.checks.every((c) => c.pass);
  } catch (e: any) {
    report.error = String(e?.stack ?? e);
    sink('FAIL boot | ' + report.error);
  }
  document.title = report.ok ? 'bridge OK' : 'bridge FAILED';
  (window as any).fcxBridge = report;
})();
