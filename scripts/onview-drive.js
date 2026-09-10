// Drive a real browser through a sketch tool's on-view parameters
// (docs/ThinClient.md sec 8.7, the browser half of stage 5).
//
// tests/gui/serve-onview-params.py already checks the server side against
// a synthetic socket client: the allowlisted `command` op, the pushed set,
// a key frame typed into the box that has the keys. What no synthetic
// client can check is the two halves that live in the browser --
// src/Gui/Renderer/wasm/main.cpp projecting a world anchor into canvas
// pixels every frame, and src/Gui/Renderer/web/src/onview.tsx drawing a
// box there and forwarding a keystroke back. This drives exactly that.
//
// The reading that only a browser can give is the PROJECTION: orbit the
// camera and the box must follow the geometry across the screen, with the
// server never told it moved. A server-computed position would sit still.
//
// Usage:  node scripts/onview-drive.js <url> <object> <settle_ms>
// Phases (also appended to ONVIEW_PHASES, a file, because node buffers
// its stdout to a pipe and the caller samples on these):
//   PHASE settled | entered | tooled | placed | typed | orbited | done
// ONVIEW_RESULT names a file the page's readings are written to as JSON.
const ppPath = process.env.PUPPETEER_PATH || 'puppeteer-core';
const puppeteer = require(ppPath);
const fs = require('fs');

const sleep = ms => new Promise(r => setTimeout(r, ms));

const phaseFile = process.env.ONVIEW_PHASES;
function phase(name) {
  console.log('PHASE ' + name);
  if (phaseFile) {
    try { fs.appendFileSync(phaseFile, name + '\n'); }
    catch (e) { console.error('phase file:', e.message); }
  }
}

// Injected rather than driven through page.mouse: every CDP input call
// waits on the page's main thread, and a viewer busy with a scene starves
// them (scripts/edit-drive.js says the same). Emscripten and Solid both
// register plain listeners, so a dispatched event reaches exactly the code
// a real one would.
const HELPERS = () => {
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
  // What the page was told about the boxes, kept from the first moment
  // so that a push arriving before a poll is not missed.
  window.__fcOnView = [];
  window.__fcOnViewLayouts = 0;
  window.addEventListener('fc:onview', e => {
    window.__fcOnView.push(e.detail || []);
  });
  window.addEventListener('fc:onviewlayout', () => {
    ++window.__fcOnViewLayouts;
  });
  // Where the drawn boxes are and what they say -- read off the DOM, so
  // this is the whole chain and not the event that feeds it.
  window.__fcBoxes = () => {
    return Array.from(document.querySelectorAll('.fc-onview-box')).map(el => {
      const r = el.getBoundingClientRect();
      return {text: el.value, x: Math.round(r.left + r.width / 2),
              y: Math.round(r.top + r.height / 2),
              focused: document.activeElement === el};
    });
  };
};

(async () => {
  const url = process.argv[2];
  const obj = process.argv[3] || 'Sketch';
  const settleMs = parseInt(process.argv[4] || '12000', 10);
  if (!url) {
    console.error('usage: node onview-drive.js <url> [object] [settle_ms]');
    process.exit(2);
  }
  const real = !!process.env.ONVIEW_REAL;
  const browser = real
    ? await require('./wasm-chrome').launch({
        headless: false, args: ['--window-size=1100,900'],
      })
    : await puppeteer.launch({
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
    const verbose = !!process.env.ONVIEW_VERBOSE;
    const cap = verbose ? 400 : 60;
    let kept = 0;
    page.on('console', m => {
      const t = m.text();
      if (kept < cap
          && (verbose || /edit mode|onview|refused|websocket|not forwarded/i.test(t))) {
        ++kept;
        console.log('page:', t);
      }
    });
    await page.goto(url, {waitUntil: 'domcontentloaded'});
    await page.evaluate(HELPERS);
    await sleep(settleMs);
    phase('settled');

    // 1. Into the edit session. The viewer forces a camera frame with the
    // op, because under the default uplink policy a page nobody has
    // clicked in has stated none and the op would be refused (sec 8.10b).
    await page.evaluate((o) => window.fcviewerEdit(o, 0, ''), obj);
    try {
      await page.waitForFunction((o) => window.fcviewerEditing === o,
                                 {timeout: 20000}, obj);
      out.entered = true;
    }
    catch (e) {
      out.entered = false;
    }
    phase('entered');

    // 2. Start a tool. This is the op that had to exist for any of this
    // to be reachable at all: a browser has no other way in, the
    // sketcher's shortcuts being Qt shortcuts on a main window.
    out.toolSent = await page.evaluate(() => {
      return !!window.fcviewerControlSend(JSON.stringify(
          {id: 9101, op: 'command', name: 'Sketcher_CreateLine'}));
    });
    await sleep(1500);
    phase('tooled');

    // 3. A tool places its boxes against the cursor, so give it one.
    await page.evaluate(async () => {
      const {cx, cy} = window.__fcRect();
      for (let i = 0; i <= 12; ++i) {
        window.__fcMouse('mousemove', cx - 60 + i * 10, cy - 20, 0, 0);
        await new Promise(r => requestAnimationFrame(r));
      }
    });
    await sleep(1200);
    out.pushes = await page.evaluate(() => window.__fcOnView.length);
    out.lastPush = await page.evaluate(
        () => window.__fcOnView[window.__fcOnView.length - 1] || []);
    out.layouts = await page.evaluate(() => window.__fcOnViewLayouts);
    out.boxes = await page.evaluate(() => window.__fcBoxes());
    phase('placed');

    // 4. Type into the box that has the keys, through the DOM box itself
    // -- which is the path a user's keystroke takes, and the one that
    // must reach the server rather than editing the field locally.
    out.typed = await page.evaluate(async () => {
      const el = document.querySelector('.fc-onview-box');
      if (!el) return {ok: false, why: 'no box drawn'};
      const before = el.value;
      el.focus();
      for (const key of ['1', '2']) {
        el.dispatchEvent(new KeyboardEvent('keydown',
            {key, bubbles: true, cancelable: true}));
        el.dispatchEvent(new KeyboardEvent('keyup',
            {key, bubbles: true, cancelable: true}));
        await new Promise(r => setTimeout(r, 250));
      }
      return {ok: true, before};
    });
    await sleep(1500);
    out.boxesAfterTyping = await page.evaluate(() => window.__fcBoxes());
    phase('typed');

    // 5. The reading only a browser can give. Orbit with the RIGHT
    // button, which stays the local camera's while the left button is
    // the edit mode's (sec 8.9 step 4), and the boxes must travel with
    // the geometry -- projected here, every frame, from the world anchor.
    // A position computed on the server would not move at all: it is not
    // told the camera turned.
    out.orbit = await page.evaluate(async () => {
      const before = window.__fcBoxes().map(b => ({x: b.x, y: b.y}));
      const pushesBefore = window.__fcOnView.length;
      const {cx, cy} = window.__fcRect();
      window.__fcMouse('mousedown', cx, cy, 2, 2);
      for (let i = 0; i <= 30; ++i) {
        window.__fcMouse('mousemove', cx + i * 6, cy + i * 2, 2, 2);
        await new Promise(r => requestAnimationFrame(r));
      }
      window.__fcMouse('mouseup', cx + 180, cy + 60, 0, 2);
      await new Promise(r => setTimeout(r, 600));
      return {before, after: window.__fcBoxes().map(b => ({x: b.x, y: b.y})),
              pushesBefore, pushesAfter: window.__fcOnView.length};
    });
    phase('orbited');

    // 6. Escape ends the tool and the session, and the boxes must go with
    // it -- one left on screen would be forwarding keystrokes to a tool
    // that has finished.
    //
    // Dispatched at the FOCUSED element, which while a box is open is the
    // box: that is where a real Escape lands, and it is the only place it
    // reaches the wire from. The viewer's own key handler stands down
    // whenever a form control has the keyboard (fcviewer_dom_has_keyboard),
    // so a synthetic Escape aimed at the document reaches nothing at all
    // -- which looked exactly like a session that would not end.
    const escape = () => page.evaluate(() => {
      const target = document.activeElement || document;
      for (const type of ['keydown', 'keyup']) {
        target.dispatchEvent(new KeyboardEvent(type,
            {key: 'Escape', bubbles: true, cancelable: true}));
      }
    });
    await escape();
    await sleep(1500);
    // Twice: the first ends the tool, the second the edit session.
    await escape();
    await sleep(1500);
    out.boxesAtEnd = await page.evaluate(() => window.__fcBoxes());
    out.editingAtEnd = await page.evaluate(() => window.fcviewerEditing);
    await sleep(400);
  }
  catch (e) {
    out.error = String(e && e.message || e);
    console.error('DRIVE ERROR', e);
  }
  finally {
    await browser.close();
  }
  if (process.env.ONVIEW_RESULT) {
    try { fs.writeFileSync(process.env.ONVIEW_RESULT, JSON.stringify(out)); }
    catch (e) { console.error('result file:', e.message); }
  }
  console.log('RESULT ' + JSON.stringify(out));
  phase('done');
  if (out.error)
    process.exit(1);
})().catch(e => { console.error('DRIVE ERROR', e); process.exit(1); });
