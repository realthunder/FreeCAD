// Drive a real browser through a dimension's value editor
// (docs/SketcherPort.md "One editor for a constraint's value").
//
// tests/gui/serve-datum-in-place.py checks the server side against a
// synthetic socket client: the "datum" entry in the onview push, the
// onViewAction op, one undo step. What only a browser can check is the
// editor web/src/onview.tsx draws from it: that it is drawn at all, with
// its value, name row and toggle; that it is kept off the geometry it
// edits; that typing an expression offers names from the client's own
// completion (pathcomplete.ts, the omni box's) and that taking one goes up
// and comes back as the server's text; and that the toggle is a click.
//
// Usage:  node scripts/datum-drive.js <url> <object> <settle_ms>
// Phases (also appended to DATUM_PHASES): settled | entered | opened |
//   completed | members | toggled | escaped | done
// DATUM_RESULT names a file the page's readings are written to as JSON.
const ppPath = process.env.PUPPETEER_PATH || 'puppeteer-core';
const puppeteer = require(ppPath);
const fs = require('fs');

const sleep = ms => new Promise(r => setTimeout(r, ms));

const phaseFile = process.env.DATUM_PHASES;
function phase(name) {
  console.log('PHASE ' + name);
  if (phaseFile) {
    try { fs.appendFileSync(phaseFile, name + '\n'); }
    catch (e) { console.error('phase file:', e.message); }
  }
}

// Injected rather than driven through page.mouse (scripts/onview-drive.js
// says why): a dispatched event reaches exactly the code a real one would.
const HELPERS = () => {
  // the replies to this driver's own ops
  window.__fcReplies = {};
  window.addEventListener('fc:control', e => {
    const d = e.detail;
    if (d && typeof d.id === 'number' && d.id >= 9200 && d.id < 9300) window.__fcReplies[d.id] = d;
  });
  // how often the page asks for an object's property descriptors
  window.__fcPropAsks = 0;
  const send = WebSocket.prototype.send;
  WebSocket.prototype.send = function (data) {
    let text = '';
    if (typeof data === 'string') text = data;
    else if (data && data.byteLength !== undefined && data.byteLength < 4096) {
      try { text = new TextDecoder().decode(data); }
      catch (e) { /* not text */ }
    }
    if (text.indexOf('"getProperties"') >= 0) ++window.__fcPropAsks;
    return send.call(this, data);
  };
  window.__fcRect = () => {
    const c = document.getElementById('canvas');
    const r = c.getBoundingClientRect();
    return {c, cx: r.left + r.width / 2, cy: r.top + r.height / 2,
            w: r.width, h: r.height};
  };
  window.__fcMouse = (type, x, y, buttons, button) => {
    const {c} = window.__fcRect();
    const target = type === 'mousedown' ? c : document;
    target.dispatchEvent(new MouseEvent(type, {
      bubbles: true, cancelable: true, view: window,
      clientX: x, clientY: y, buttons: buttons, button: button || 0}));
  };
  window.__fcEditor = () => {
    const el = document.querySelector('.fc-onview-datum');
    if (!el) return null;
    const r = el.getBoundingClientRect();
    const value = el.querySelector('.fc-onview-datum-value');
    const toggle = el.querySelector('.fc-onview-datum-toggle');
    return {
      left: r.left, top: r.top, right: r.right, bottom: r.bottom,
      above: el.classList.contains('fc-onview-datum-above'),
      value: value ? value.value : null,
      focused: document.activeElement === value,
      name: !!el.querySelector('.fc-onview-datum-name'),
      toggle: toggle ? !toggle.classList.contains('fc-onview-datum-reference') : null,
      result: (el.querySelector('.fc-onview-datum-result') || {}).textContent || '',
      rows: Array.from(el.querySelectorAll('.fc-onview-datum-row'))
        .map(row => row.firstChild ? row.firstChild.textContent : ''),
      hi: Array.from(el.querySelectorAll('.fc-onview-datum-row'))
        .findIndex(row => row.classList.contains('fc-onview-datum-hi')),
    };
  };
  // Take the completion with that title, by the keys: down to it from the
  // row that is lit, then Enter
  window.__fcTake = async (title) => {
    const e = window.__fcEditor();
    const at = e ? e.rows.indexOf(title) : -1;
    if (at < 0) return false;
    const n = e.rows.length;
    for (let i = (at - Math.max(e.hi, 0) + n) % n; i > 0; --i) await window.__fcKey('ArrowDown');
    await window.__fcKey('Enter');
    return true;
  };
  window.__fcKey = async (key) => {
    const el = document.querySelector('.fc-onview-datum-value');
    if (!el) return false;
    el.focus();
    for (const type of ['keydown', 'keyup']) {
      el.dispatchEvent(new KeyboardEvent(type, {key, bubbles: true, cancelable: true}));
    }
    await new Promise(r => setTimeout(r, 300));
    return true;
  };
};

async function waitFor(page, fn, ms) {
  try {
    await page.waitForFunction(fn, {timeout: ms});
    return true;
  }
  catch (e) {
    return false;
  }
}

(async () => {
  const url = process.argv[2];
  const obj = process.argv[3] || 'Sketch';
  const settleMs = parseInt(process.argv[4] || '12000', 10);
  if (!url) {
    console.error('usage: node datum-drive.js <url> [object] [settle_ms]');
    process.exit(2);
  }
  const browser = await puppeteer.launch({
    executablePath: process.env.CHROME,
    headless: true,
    protocolTimeout: 180000,
    args: ['--no-sandbox', '--enable-unsafe-swiftshader',
           '--use-angle=swiftshader', '--window-size=1100,900',
           '--disable-background-timer-throttling',
           '--disable-backgrounding-occluded-windows',
           '--disable-renderer-backgrounding'],
  });
  const out = {};
  try {
    const page = await browser.newPage();
    page.setDefaultTimeout(60000);
    await page.setViewport({width: 1100, height: 900});
    let kept = 0;
    page.on('console', m => {
      const t = m.text();
      const verbose = !!process.env.DATUM_VERBOSE;
      if (kept < (verbose ? 400 : 60)
          && (verbose || /edit mode|onview|refused|websocket|not forwarded|completion/i.test(t))) {
        ++kept;
        console.log('page:', t);
      }
    });
    await page.goto(url, {waitUntil: 'domcontentloaded'});
    await page.evaluate(HELPERS);
    await sleep(settleMs);
    phase('settled');

    await page.evaluate((o) => window.fcviewerEdit(o, 0, ''), obj);
    out.entered = await waitFor(page, `window.fcviewerEditing === ${JSON.stringify(obj)}`,
                                20000);
    phase('entered');

    // A dimension on the line: the command, then a click where the line
    // is -- its middle, which the camera framing the sketch puts at the
    // canvas centre (the test keeps it off the origin and its root point).
    out.commandSent = await page.evaluate(() => !!window.fcviewerControlSend(JSON.stringify(
        {id: 9201, op: 'command', name: 'Sketcher_ConstrainDistance'})));
    await sleep(1500);
    out.commandReply = await page.evaluate(() => window.__fcReplies[9201] || null);
    out.centre = await page.evaluate(async () => {
      const {cx, cy} = window.__fcRect();
      for (let i = 0; i < 4; ++i) {
        window.__fcMouse('mousemove', cx + 3 - i, cy, 0, 0);
        await new Promise(r => requestAnimationFrame(r));
      }
      window.__fcMouse('mousedown', cx, cy, 1, 0);
      await new Promise(r => setTimeout(r, 80));
      window.__fcMouse('mouseup', cx, cy, 0, 0);
      return {x: cx, y: cy};
    });
    out.drawn = await waitFor(page, () => !!document.querySelector('.fc-onview-datum'), 15000);
    await sleep(800);
    out.opened = await page.evaluate(() => window.__fcEditor());
    phase('opened');

    // An expression: '=' and the start of a name. The list is the
    // client's, from names it fetched once; taking the first row goes up
    // as a replacement and comes back as the server's text.
    for (const key of ['=', 'S', 'k']) {
      await page.evaluate((k) => window.__fcKey(k), key);
    }
    out.listed = await waitFor(page, () => {
      const e = window.__fcEditor();
      return e && e.rows.length > 0;
    }, 15000);
    out.typed = await page.evaluate(() => window.__fcEditor());
    await page.evaluate(async () => {
      const e = window.__fcEditor();
      const at = e ? e.rows.indexOf('Sketch') : -1;
      for (let i = 0; i < at; ++i) await window.__fcKey('ArrowDown');
      await window.__fcKey('Enter');
    });
    await waitFor(page, () => {
      const e = window.__fcEditor();
      return e && e.value === '=Sketch';
    }, 10000);
    out.completed = await page.evaluate(() => window.__fcEditor());
    phase('completed');

    // What a property holds under its name: the sketch's named constraints
    // after "Sketch.Constraints.", from the descriptors the client fetched
    // for "Sketch." -- nothing more crosses the wire. Then the same with no
    // object in front, the edited sketch's own.
    const editor = () => page.evaluate(() => window.__fcEditor());
    const type = async (keys) => {
      for (const key of keys) await page.evaluate((k) => window.__fcKey(k), key);
    };
    const offered = (title) => waitFor(page,
        `((window.__fcEditor() || {}).rows || []).indexOf(${JSON.stringify(title)}) >= 0`, 6000);
    const reads = (value) => waitFor(page,
        `(window.__fcEditor() || {}).value === ${JSON.stringify(value)}`, 6000);
    const take = (title) => page.evaluate((t) => window.__fcTake(t), title);
    // The line under the value is judged once the typing pauses: wait for
    // it to say something else than it said of the text before
    const judged = async (was) => {
      await waitFor(page,
          `(window.__fcEditor() || {}).result !== ${JSON.stringify(was)}`, 4000);
      await sleep(500);
      return editor();
    };
    const members = out.members = {};
    await type(['.', 'C', 'o', 'n']);
    members.property = await offered('Constraints');
    await take('Constraints');
    await reads('=Sketch.Constraints');
    await type(['.']);
    members.listed = await offered('Width');
    members.rows = ((await editor()) || {}).rows;
    let before = ((await editor()) || {}).result;
    await take('Width');
    members.taken = await reads('=Sketch.Constraints.Width');
    members.full = await judged(before);
    // back to the '=' alone
    for (let i = ((members.full || {}).value || '=').length; i > 1; --i) {
      await page.evaluate(() => window.__fcKey('Backspace'));
    }
    members.cleared = await reads('=');
    await type(['C', 'o', 'n']);
    members.ownProperty = await offered('Constraints');
    await take('Constraints');
    await reads('=Constraints');
    await type(['.']);
    members.ownListed = await offered('Width');
    members.ownRows = ((await editor()) || {}).rows;
    before = ((await editor()) || {}).result;
    await take('Width');
    members.ownTaken = await reads('=Constraints.Width');
    members.own = await judged(before);
    members.asks = await page.evaluate(() => window.__fcPropAsks);
    phase('members');

    // The toggle is a click, not a key
    await page.evaluate(() => {
      const b = document.querySelector('.fc-onview-datum-toggle');
      if (b) b.dispatchEvent(new PointerEvent('pointerdown', {bubbles: true, cancelable: true}));
    });
    await waitFor(page, () => {
      const e = window.__fcEditor();
      return e && e.toggle === false;
    }, 10000);
    out.toggled = await page.evaluate(() => window.__fcEditor());
    phase('toggled');

    // Escape at the field that has the keys: by default it is Enter -- the
    // entry is applied and the editor goes
    await page.evaluate(() => window.__fcKey('Escape'));
    await waitFor(page, () => !document.querySelector('.fc-onview-datum'), 10000);
    out.atEnd = await page.evaluate(() => window.__fcEditor());
    phase('escaped');
    // the caller samples the document on that phase
    await sleep(1500);
    // and one undo, the client's own op, takes the entry back: it was one step
    out.undoSent = await page.evaluate(() => !!window.fcviewerControlSend(JSON.stringify(
        {id: 9202, op: 'undo'})));
    await waitFor(page, () => !!window.__fcReplies[9202], 10000);
    out.undoReply = await page.evaluate(() => window.__fcReplies[9202] || null);
    // the tool, then the edit session
    for (let i = 0; i < 2; ++i) {
      await page.evaluate(() => {
        const target = document.activeElement || document;
        for (const type of ['keydown', 'keyup']) {
          target.dispatchEvent(new KeyboardEvent(type,
              {key: 'Escape', bubbles: true, cancelable: true}));
        }
      });
      await sleep(1200);
    }
  }
  catch (e) {
    out.error = String(e && e.message || e);
    console.error('DRIVE ERROR', e);
  }
  finally {
    await browser.close();
  }
  if (process.env.DATUM_RESULT) {
    try { fs.writeFileSync(process.env.DATUM_RESULT, JSON.stringify(out)); }
    catch (e) { console.error('result file:', e.message); }
  }
  console.log('RESULT ' + JSON.stringify(out));
  phase('done');
  if (out.error)
    process.exit(1);
})().catch(e => { console.error('DRIVE ERROR', e); process.exit(1); });
