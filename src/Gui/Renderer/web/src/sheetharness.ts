// A harness for the spreadsheet panel with no WASM viewer in the way.
//
// The DOM chrome talks to the backend through exactly two things the viewer
// provides: window.fcviewerControlSend (uplink) and 'fc:control' events
// (downlink, docs/ThinClient.md sec 3).  Both are trivially satisfiable from
// JavaScript, so this page opens the scene socket itself and stands in for
// the viewer -- which makes the sheet tier developable and testable on its
// own, without waiting on a 3D bring-up that has nothing to do with it.
//
// Served as sheet-test.html:
//     scripts/renderer-serve.sh scripts/demo-sheet.py 8077
//     python3 -m http.server -d build/wasm/web
//     http://127.0.0.1:8000/sheet-test.html?scene=http://127.0.0.1:8077
//
// This is a harness, not a product page: the real chrome mounts SheetPanel
// over the canvas (main.tsx).

import { render } from 'solid-js/web';
import { createSignal } from 'solid-js';

import { SheetPanel } from './sheet';

const params = new URLSearchParams(location.search);
const scene = params.get('scene') ?? `http://${location.hostname}:8077`;
const wsUrl = scene.replace(/^http/, 'ws') + '/scene?v=0&s=0';

const status = document.createElement('div');
status.style.cssText =
  'position:fixed;left:12px;top:12px;font:12px ui-monospace,monospace;color:#9aa4b2';
document.body.appendChild(status);
const say = (s: string) => { status.textContent = s; };

const [ready, setReady] = createSignal(false);

say('connecting to ' + wsUrl);
const socket = new WebSocket(wsUrl);
socket.binaryType = 'arraybuffer';

socket.onopen = () => {
  // Compact separators matter: the server sniffs the literal
  // '"cmd":"hello"' out of the frame to register a viewer, and only a
  // registered viewer is sent unsolicited pushes like sheet.changed.
  socket.send('{"cmd":"hello","snapshot":0,"page":0,"client":"sheet-test"}');
  window.fcviewerControlSend = (json: string) => {
    if (socket.readyState !== WebSocket.OPEN) return false;
    socket.send(json);
    return true;
  };
  setReady(true);
  say('connected: ' + wsUrl);
};

socket.onmessage = (e) => {
  if (typeof e.data !== 'string') return;      // the scene lane; not ours
  let detail: any;
  try { detail = JSON.parse(e.data); } catch { return; }
  window.dispatchEvent(new CustomEvent('fc:control', { detail }));
};

socket.onclose = () => {
  window.fcviewerControlSend = undefined;
  setReady(false);
  say('socket closed -- is the backend still serving?');
};
socket.onerror = () => say('socket error against ' + wsUrl);

const host = document.createElement('div');
document.body.appendChild(host);
render(
  () => SheetPanel({
    open: () => true,
    onClose: () => { /* the harness has nothing to close to */ },
    viewOnly: () => false,
    doc: () => params.get('doc') ?? '',
    // The harness is served from under /web, one level below where the
    // image is staged (scripts/fcx-web-stage.sh).
    fcxBase: () => params.get('fcx') ?? '../fcx',
  }),
  host,
);

// Referenced so the signal is not dead code: the panel does its own
// waiting, this only reports it.
void ready;

// A headless screenshot fires on the load event, which happens long before
// a socket round trip has filled the grid -- every early capture of this
// page showed an empty one.  ?wait=<ms> holds the load event open with a
// deliberately slow subresource, so the shot lands on a populated panel.
const waitMs = Number(params.get('wait') ?? 0);
if (waitMs > 0) {
  const holder = document.createElement('img');
  holder.style.display = 'none';
  holder.src = '/__wait?ms=' + waitMs;
  document.body.appendChild(holder);
}

// Self-check, for a headless run: watch the panel's OWN rendered DOM until
// the grid has content, then report it.  Reading the rendered cells rather
// than calling the control ops again is the point -- what needs proving is
// that the component displays what the host sent, not that the host answers
// (scripts/control-client.py already proves that).
//
//   ?report=/result   POST the report there and stop
//   ?reportms=8000    give up waiting after this long (default 10 s)
//   ?reportdelay=6000 report at this fixed time instead of as soon as the
//                     grid fills -- how an externally made edit is caught
//                     arriving through the sheet.changed push
const reportTo = params.get('report');
if (reportTo) {
  const limit = Number(params.get('reportms') ?? 10000);
  const delay = Number(params.get('reportdelay') ?? 0);
  const started = Date.now();

  const collect = () => {
    const rows: string[][] = [];
    for (const tr of document.querySelectorAll('.fc-sheet-grid tbody tr')) {
      const cells: string[] = [];
      for (const td of tr.querySelectorAll('td')) cells.push(td.textContent ?? '');
      if (cells.some((t) => t.length)) rows.push(cells);
    }
    return rows;
  };

  // ?preview=<formula> types a formula into the formula bar (for the
  // selected cell) once the grid has filled, so a headless run can see the
  // sandbox's local answer appear before any host round trip.  Driving the
  // real input rather than calling the component is the point: what is
  // being checked is that typing produces a preview.
  const formula = params.get('preview');
  let typed = false;
  const typeFormula = () => {
    const input = document.querySelector<HTMLInputElement>(
      '.fc-sheet-formula .fc-sheet-input');
    if (!input || !formula) return;
    input.focus();
    input.value = formula;
    input.dispatchEvent(new Event('input', { bubbles: true }));
    typed = true;
  };

  const tick = () => {
    const rows = collect();
    if (formula && !typed && rows.length) typeFormula();
    // Re-type until the sandbox image has finished loading: the first
    // keystrokes land before it is ready, and previews are silent then.
    if (formula && typed && !document.querySelector('.fc-sheet-preview')
        && Date.now() - started < limit) {
      typeFormula();
    }
    const banner = document.querySelector('.fc-sheet-error')?.textContent ?? '';
    const sheets = [...document.querySelectorAll('.fc-sheet-pick option')]
      .map((o) => o.textContent ?? '');
    const elapsed = Date.now() - started;
    const previewText = document.querySelector('.fc-sheet-preview')?.textContent ?? '';
    const waitingForPreview = !!formula && !previewText && elapsed < limit;
    const done = delay > 0
      ? elapsed >= delay
      : ((rows.length > 0 && !waitingForPreview) || elapsed > limit);
    if (!done) { setTimeout(tick, 250); return; }
    const report = {
      ok: rows.length > 0 && !banner && (!formula || !!previewText),
      preview: previewText,
      previewIsError: !!document.querySelector('.fc-sheet-preview-err'),
      elapsedMs: Date.now() - started,
      banner,
      sheets,
      rows,
      ua: navigator.userAgent,
    };
    say(report.ok ? 'reported OK' : 'reported FAILURE: ' + banner);
    void fetch(reportTo, { method: 'POST', body: JSON.stringify(report) });
  };
  setTimeout(tick, 250);
}
