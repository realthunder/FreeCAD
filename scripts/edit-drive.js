// Drive the WASM viewer into a server-side edit mode and work in it
// (docs/ThinClient.md sec 8.9 step 4, the browser half).
//
// tests/gui/serve-mirror-edit.py already checks the server side of this
// against a synthetic socket client: the ops, the binding, the 'E'
// replay. What it cannot check is the half that lives in
// src/Gui/Renderer/wasm/main.cpp -- that a real browser asks for the
// session, that its pointer and keys turn into 'E' frames instead of
// orbiting the camera, and that it finds out when the session ends
// without it asking. This drives exactly that, and answers nothing
// itself: the serving process counts what arrived and reads its own
// document.
//
// Usage:  node scripts/edit-drive.js <url> <object> <settle_ms>
// Phases (also appended to EDIT_PHASES, a file, because node buffers its
// stdout to a pipe and the caller samples its counters on these):
//   PHASE settled | viewclick | entered | drawn | editclick | escaped | done
// EDIT_RESULT names a file the page's own readings are written to as
// JSON -- what the VIEWER believed, against what the server saw.
const ppPath = process.env.PUPPETEER_PATH || 'puppeteer-core';
const puppeteer = require(ppPath);
const fs = require('fs');

const sleep = ms => new Promise(r => setTimeout(r, ms));

const phaseFile = process.env.EDIT_PHASES;
function phase(name) {
  console.log('PHASE ' + name);
  if (phaseFile) {
    try { fs.appendFileSync(phaseFile, name + '\n'); }
    catch (e) { console.error('phase file:', e.message); }
  }
}

// One gesture helper, injected into the page rather than driven through
// page.mouse: every CDP input call waits on the page's main thread, and
// a viewer busy with a scene starves them (scripts/camup-drive.js says
// the same, and says what it cost to find out). Emscripten registers
// plain addEventListener handlers, so a dispatched event reaches exactly
// the code a real one would.
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
  window.__fcKey = (type, key) => {
    document.dispatchEvent(new KeyboardEvent(type, {
      bubbles: true, cancelable: true, key: key}));
  };
  window.__fcClick = async (x, y) => {
    window.__fcMouse('mousedown', x, y, 1, 0);
    await new Promise(r => setTimeout(r, 60));
    window.__fcMouse('mouseup', x, y, 0, 0);
  };
};

(async () => {
  const url = process.argv[2];
  const obj = process.argv[3] || 'Sketch';
  const settleMs = parseInt(process.argv[4] || '12000', 10);
  if (!url) {
    console.error('usage: node edit-drive.js <url> [object] [settle_ms]');
    process.exit(2);
  }
  const real = !!process.env.EDIT_REAL;
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
    const verbose = !!process.env.EDIT_VERBOSE;
    const cap = verbose ? 400 : 60;
    let kept = 0;
    page.on('console', m => {
      const t = m.text();
      if (kept < cap
          && (verbose
              || /edit mode|edit refused|websocket (connected|lost)|scene update/i
                     .test(t))) {
        ++kept;
        console.log('page:', t);
      }
    });
    await page.goto(url, {waitUntil: 'domcontentloaded'});
    await page.evaluate(HELPERS);
    await sleep(settleMs);
    phase('settled');

    // 1. A click in view mode, at the middle of the canvas where the
    // model is. This is the room's selection (sec 8.2a) and it is what
    // gives the reading after entering edit its force: the same click,
    // in the other mode, must not reach the room.
    await page.evaluate(async () => {
      const {cx, cy} = window.__fcRect();
      await window.__fcClick(cx, cy);
    });
    await sleep(1200);
    phase('viewclick');

    // 2. Ask for the edit session. The viewer sends the op and believes
    // nothing until the answer comes back, so this waits on the state
    // rather than on the call.
    out.editCall = await page.evaluate(
        (o) => !!window.fcviewerEdit(o, 0, ''), obj);
    try {
      await page.waitForFunction(
          (o) => window.fcviewerEditing === o, {timeout: 20000}, obj);
      out.entered = true;
    }
    catch (e) {
      out.entered = false;
    }
    out.editingAfterEnter = await page.evaluate(() => window.fcviewerEditing);
    phase('entered');

    // 3. Work in it: a press, a drag across the canvas paced on the
    // page's own frames, a release. Under the split of sec 8.9 step 4
    // the left button is the edit mode's, so none of this is an orbit --
    // and the server counts every one of them as an 'E' frame.
    out.gesture = await page.evaluate(async () => {
      const {cx, cy, w} = window.__fcRect();
      const span = w * 0.2;
      window.__fcMouse('mousedown', cx - span, cy, 1, 0);
      let moves = 0;
      for (let i = 0; i <= 40; ++i) {
        window.__fcMouse('mousemove', cx - span + (2 * span * i) / 40, cy, 1, 0);
        ++moves;
        await new Promise(r => requestAnimationFrame(r));
      }
      window.__fcMouse('mouseup', cx + span, cy, 0, 0);
      // And a hover afterwards, with no button down: in an edit mode
      // that is part of the tool state machine too, and it is the half
      // that never travels in view mode.
      for (let i = 0; i <= 20; ++i) {
        window.__fcMouse('mousemove', cx + (i - 10) * 4, cy + (i - 10) * 2, 0, 0);
        ++moves;
        await new Promise(r => requestAnimationFrame(r));
      }
      return {moves};
    });
    await sleep(800);
    phase('drawn');

    // 4. A click while editing. It goes up the 'E' channel, not as a
    // pick, and whatever it selects is this client's own -- so the room
    // must be where entering the edit left it.
    await page.evaluate(async () => {
      const {cx, cy} = window.__fcRect();
      await window.__fcClick(cx, cy);
    });
    await sleep(1200);
    phase('editclick');

    // 5. Escape. The sketcher handles it and ends the session itself --
    // the client never asks -- so this is the leaving edge the server
    // has to push and the viewer has to believe.
    await page.evaluate(() => {
      window.__fcKey('keydown', 'Escape');
      window.__fcKey('keyup', 'Escape');
    });
    try {
      await page.waitForFunction(() => !window.fcviewerEditing,
                                 {timeout: 20000});
      out.leftOnEscape = true;
    }
    catch (e) {
      out.leftOnEscape = false;
    }
    out.editingAfterEscape = await page.evaluate(() => window.fcviewerEditing);
    phase('escaped');
    await sleep(500);
  }
  catch (e) {
    out.error = String(e && e.message || e);
    console.error('DRIVE ERROR', e);
  }
  finally {
    await browser.close();
  }
  if (process.env.EDIT_RESULT) {
    try { fs.writeFileSync(process.env.EDIT_RESULT, JSON.stringify(out)); }
    catch (e) { console.error('result file:', e.message); }
  }
  console.log('RESULT ' + JSON.stringify(out));
  phase('done');
  if (out.error)
    process.exit(1);
})().catch(e => { console.error('DRIVE ERROR', e); process.exit(1); });
