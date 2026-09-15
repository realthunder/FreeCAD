// Entry point of console-panel-test.html, the C4 gate of docs/Sandbox.md 7.20:
// the console panel mounted with no WASM viewer, on the FreeCAD serving the
// page.  By hand it is just the panel.  With ?drive=1 a script uses the panel
// the way a person does -- text into the input, Enter, Tab, the arrow keys,
// a paste, the Interrupt button -- through DOM events, reads what the output
// shows, and leaves its verdict on window.fcxConsolePanel for
// scripts/console-drive.js (tests/gui/sandbox-console-panel-browser.py).

import { render } from 'solid-js/web';
import { createSignal } from 'solid-js';

import { ConsolePanel } from '../console';

const params = new URLSearchParams(location.search);
const server = params.get('server') ?? location.origin;
const token = params.get('token') ?? undefined;
const doc1 = params.get('doc') ?? '';
const doc2 = params.get('doc2') ?? '';

const [doc, setDoc] = createSignal(doc1);
const [open, setOpen] = createSignal(true);
const host = document.createElement('div');
document.body.appendChild(host);
render(() => ConsolePanel({
  open, onClose: () => setOpen(false), doc, viewOnly: () => false, server, token,
  client: () => 'console-panel-test',
}), host);

interface Check { name: string; pass: boolean; detail: string }
const report = {
  ok: false,
  bootMs: 0,
  stats: null as unknown,
  checks: [] as Check[],
  error: '',
};
const check = (name: string, pass: boolean, detail: unknown) => {
  const d = typeof detail === 'string' ? detail : JSON.stringify(detail);
  report.checks.push({ name, pass, detail: d });
  console.log(`${pass ? 'PASS' : 'FAIL'} ${name} | ${d}`);
};

const panel = () => document.querySelector('.fc-console') as HTMLElement;
const input = () => panel().querySelector('.fc-console-input') as HTMLInputElement;
const prompt = () => panel().querySelector('.fc-console-prompt')?.textContent ?? '';
const output = () => panel().querySelector('.fc-console-out')?.textContent ?? '';
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

async function until(what: string, cond: () => boolean, ms: number) {
  const t0 = performance.now();
  while (!cond()) {
    if (performance.now() - t0 > ms)
      throw new Error(`timed out waiting for ${what} (state ${panel()?.dataset.state})`);
    await sleep(15);
  }
}
const idle = (ms = 30000) => until('the console to be idle', () => panel()?.dataset.state === 'idle', ms);

function type(text: string) {
  const el = input();
  el.focus();
  el.value = text;
  el.setSelectionRange(text.length, text.length);
  el.dispatchEvent(new InputEvent('input', { bubbles: true }));
}
function key(k: string, mods: KeyboardEventInit = {}) {
  input().dispatchEvent(new KeyboardEvent('keydown', { key: k, bubbles: true, cancelable: true, ...mods }));
}
/// Enter one line; what the output gained while it ran.
async function enter(text: string): Promise<string> {
  const mark = output().length;
  type(text);
  key('Enter');
  await idle();
  return output().slice(mark);
}

if (params.has('drive')) (async () => {
  const t0 = performance.now();
  try {
    await until('the console to boot', () => ['idle', 'failed'].includes(panel()?.dataset.state ?? ''),
                240000);
    report.bootMs = Math.round(performance.now() - t0);
    if (panel().dataset.state === 'failed')
      throw new Error('boot failed: ' + output());
    check('the console boots in the page', /ms to start/.test(output()), output());

    let got = await enter('App.ActiveDocument.Name');
    check('an expression shows its repr', got.includes(`'${doc1}'`), got);

    await enter("b = App.ActiveDocument.getObject('Box')");
    await enter('b.Length = 30');
    got = await enter('b.Length.Value');
    check('a name persists across lines and writes the document', /\b30\.0\b/.test(got), got);

    got = await enter('for o in App.ActiveDocument.Objects:');
    const cont = prompt();
    await enter('    print(o.Name, o.TypeId)');
    got = await enter('');
    check('a block continues on ... and runs on the blank line',
          cont === '...' && got.includes('Box Part::Box') && prompt() === '>>>', { cont, got });

    got = await enter("print('partial', end='')");
    check('output without a newline is shown', got.includes('partial'), got);

    got = await enter('1/0');
    check('an error shows its traceback, not the console\'s own frames',
          /Traceback/.test(got) && /ZeroDivisionError/.test(got) && /<console>/.test(got)
          && !/fcx_console|in push/.test(got), got);

    got = await enter(')');
    const after = await enter('2 + 2');
    check('a syntax error is reported and the next line runs',
          /SyntaxError/.test(got) && /\b4\b/.test(after), { got, after });

    got = await enter("App.newDocument('ConsoleNope')");
    check('the host refuses what a client may not do', /PermissionError/.test(got), got);

    type('App.Vect');
    key('Tab');
    await until('a completion', () => input().value !== 'App.Vect', 10000).catch(() => {});
    check('Tab completes a module name', input().value === 'App.Vector(', input().value);

    type('b.Leng');
    key('Tab');
    await until('a completion', () => input().value !== 'b.Leng', 10000).catch(() => {});
    check('Tab completes a document object\'s attribute over the bridge',
          input().value.startsWith('b.Length'), input().value);

    type('');
    key('ArrowUp');
    check('Up recalls the last line', input().value === "App.newDocument('ConsoleNope')", input().value);
    key('ArrowDown');
    check('Down returns to the line being typed', input().value === '', input().value);

    {
      const mark = output().length;
      type('');
      const data = new DataTransfer();
      data.setData('text/plain', 'x = 6\ny = x * 7\nprint(y)\n');
      input().dispatchEvent(new ClipboardEvent('paste', { clipboardData: data, bubbles: true, cancelable: true }));
      await sleep(0);
      await idle();
      got = output().slice(mark);
      check('a pasted block runs line by line', got.includes('>>> y = x * 7') && /\b42\b/.test(got), got);
    }

    {
      const mark = output().length;
      await enter('n = 0');
      type('while True: n += b.Width.Value');
      key('Enter');
      await idle();
      type('');
      key('Enter');
      await until('the loop to run', () => panel().dataset.state === 'busy', 5000);
      await sleep(600);
      const button = panel().querySelector('.fc-console-interrupt') as HTMLButtonElement | null;
      button?.click();
      await idle(15000);
      got = output().slice(mark);
      const n = await enter('n > 0');
      check('Interrupt stops a loop that reaches the host',
            !!button && /KeyboardInterrupt/.test(got) && /True/.test(n), { got, n });
    }

    type('half typed');
    key('c', { ctrlKey: true });
    check('Ctrl+C drops the line being typed',
          input().value === '' && /half typed\nKeyboardInterrupt/.test(output()), input().value);

    if (doc2) {
      const mark = output().length;
      setDoc(doc2);
      await until('the switch note', () => output().slice(mark).includes('now on document'), 5000);
      // The switch is a text frame; the host acts on it before the next
      // bridge op, which rides the same socket behind it.
      got = await enter('App.ActiveDocument.Name');
      check('the console follows the viewer to another document', got.includes(`'${doc2}'`), got);
      got = await enter("App.ActiveDocument.addObject('Part::Box', 'FromConsole').Height = 3");
      check('a statement writes the new document', !/Error/.test(got), got);
    }

    report.ok = report.checks.every((c) => c.pass);
  } catch (e: any) {
    report.error = String(e?.stack ?? e);
    console.log('FAIL drive | ' + report.error + '\n' + output());
  }
  // The session is private to the panel; its figures are read off the bridge
  // through the one hook the panel leaves for a gate.
  report.stats = (window as any).fcxConsoleStats?.() ?? null;
  document.title = report.ok ? 'console panel OK' : 'console panel FAILED';
  (window as any).fcxConsolePanel = report;
})();
