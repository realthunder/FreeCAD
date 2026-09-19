// Drive a real browser through the streamed desktop tool bars
// (docs/ThinClient.md 8.11 item 4).
//
// src/Mod/Test/SandboxToolBarMirror.py checks the stream against an
// injected sender: the models, the snapshot, a live change, an icon. What
// it cannot check is the client -- src/Gui/Renderer/web/src/toolbar.tsx
// subscribing on a real connection, drawing the bars in the desktop's
// order with their icons, following a workbench switch, and a click going
// back as the `command` op that runs a tool in this browser's view.
//
// The caller (tests/gui/serve-toolbar-browser.py) samples the desktop at
// the phases below and acknowledges each one in TOOLBAR_ACK, so what the
// page shows is compared with what the desktop had at the same moment.
//
// Usage:  node scripts/toolbar-drive.js <url> <object> <settle_ms>
// Phases (appended to TOOLBAR_PHASES; node buffers stdout to a pipe):
//   PHASE settled | entered | toggled | done   (each waits for its ack)
// TOOLBAR_RESULT names a file the page's readings are written to as JSON.
const ppPath = process.env.PUPPETEER_PATH || 'puppeteer-core';
const puppeteer = require(ppPath);
const fs = require('fs');

const sleep = ms => new Promise(r => setTimeout(r, ms));

const phaseFile = process.env.TOOLBAR_PHASES;
const ackFile = process.env.TOOLBAR_ACK;
function phase(name) {
  console.log('PHASE ' + name);
  if (phaseFile) {
    try { fs.appendFileSync(phaseFile, name + '\n'); }
    catch (e) { console.error('phase file:', e.message); }
  }
}

async function waitAck(name, ms = 60000) {
  if (!ackFile) return true;
  const until = Date.now() + ms;
  while (Date.now() < until) {
    try {
      if (fs.readFileSync(ackFile, 'utf8').split('\n').includes(name)) return true;
    }
    catch (e) { /* not written yet */ }
    await sleep(100);
  }
  throw new Error('no ack for phase ' + name);
}

// Injected: plain listeners and element clicks reach the same code a real
// user's input does, without waiting on CDP (scripts/edit-drive.js).
const HELPERS = () => {
  window.__fcWidgetPushes = 0;
  window.addEventListener('fc:control', e => {
    const d = e.detail;
    if (d && d.op === 'widgets' && typeof d.id !== 'number') ++window.__fcWidgetPushes;
  });
  window.__fcOnView = 0;
  window.addEventListener('fc:onview', e => {
    if (Array.isArray(e.detail) && e.detail.length) ++window.__fcOnView;
  });
  window.__fcBars = () => Array.from(document.querySelectorAll('.fc-tb-bar'))
    .map(b => b.getAttribute('aria-label'));
  window.__fcButtons = () => Array.from(document.querySelectorAll('.fc-tb-btn'))
    .map(b => {
      const img = b.querySelector('img.fc-tb-icon');
      return {cmd: b.dataset.command || '', disabled: b.disabled, title: b.title,
              group: !!b.parentElement.querySelector('.fc-tb-caret'),
              icon: !!(img && img.complete && img.naturalWidth > 0),
              text: !!b.querySelector('.fc-tb-text')};
    });
  window.__fcError = () => {
    const el = document.querySelector('.fc-tb-error');
    return el ? el.textContent : null;
  };
  let reqId = 990000;
  window.__fcControl = (msg) => new Promise(resolve => {
    const id = ++reqId;
    const timer = setTimeout(() => resolve({timeout: true}), 15000);
    const listen = e => {
      if (e.detail && e.detail.id === id) {
        clearTimeout(timer);
        window.removeEventListener('fc:control', listen);
        resolve(e.detail);
      }
    };
    window.addEventListener('fc:control', listen);
    window.fcviewerControlSend(JSON.stringify(Object.assign({id}, msg)));
  });
  window.__fcMoves = async () => {
    const c = document.getElementById('canvas');
    const r = c.getBoundingClientRect();
    const cx = r.left + r.width / 2, cy = r.top + r.height / 2;
    for (let i = 0; i <= 12; ++i) {
      document.dispatchEvent(new MouseEvent('mousemove', {
        bubbles: true, cancelable: true, view: window,
        clientX: cx - 60 + i * 10, clientY: cy - 20, buttons: 0}));
      await new Promise(res => requestAnimationFrame(res));
    }
  };
  window.__fcEscape = () => {
    const target = document.activeElement || document;
    for (const type of ['keydown', 'keyup'])
      target.dispatchEvent(new KeyboardEvent(type,
          {key: 'Escape', bubbles: true, cancelable: true}));
  };
  window.__fcMenuToggle = () => {
    let item = Array.from(document.querySelectorAll('.fc-menu-item'))
      .find(el => /Toolbars/.test(el.textContent));
    if (!item) {
      document.querySelector('.fc-launch').click();
      item = Array.from(document.querySelectorAll('.fc-menu-item'))
        .find(el => /Toolbars/.test(el.textContent));
    }
    if (!item) return false;
    item.click();
    return true;
  };
};

(async () => {
  const url = process.argv[2];
  const obj = process.argv[3] || 'Sketch';
  const settleMs = parseInt(process.argv[4] || '60000', 10);
  if (!url) {
    console.error('usage: node toolbar-drive.js <url> [object] [settle_ms]');
    process.exit(2);
  }
  const browser = await puppeteer.launch({
    executablePath: process.env.CHROME,
    headless: true,
    protocolTimeout: 180000,
    args: ['--no-sandbox', '--enable-unsafe-swiftshader',
           '--use-angle=swiftshader', '--window-size=1280,900',
           '--disable-background-timer-throttling',
           '--disable-backgrounding-occluded-windows',
           '--disable-renderer-backgrounding'],
  });
  const out = {};
  try {
    const page = await browser.newPage();
    page.setDefaultTimeout(60000);
    await page.setViewport({width: 1280, height: 900});
    let kept = 0;
    page.on('console', m => {
      const t = m.text();
      if (kept < 60 && /tool bars|toolbar|refused|websocket|fcviewer-ui/i.test(t)) {
        ++kept;
        console.log('page:', t);
      }
    });
    await page.goto(url, {waitUntil: 'domcontentloaded'});
    await page.evaluate(HELPERS);

    // 1. The subscription and the first bars, with no click from anyone:
    // on by default at this size.
    const t0 = Date.now();
    try {
      await page.waitForFunction(() => document.querySelectorAll('.fc-tb-bar').length > 0,
                                 {timeout: settleMs});
      out.firstBarsMs = Date.now() - t0;
    }
    catch (e) {
      out.firstBarsMs = null;
    }
    await sleep(4000);   // icons, and the stream's first coalesced ticks
    phase('settled');
    await waitAck('settled');
    out.bars1 = await page.evaluate(() => window.__fcBars());
    out.buttons1 = await page.evaluate(() => window.__fcButtons());
    out.topInset = await page.evaluate(
        () => getComputedStyle(document.getElementById('fc-ui')).getPropertyValue('--fc-top'));

    // 2. The server's gate, asked directly: a group member that is not a
    // sketch tool, and a plain command that is not one.
    out.refusedMember = await page.evaluate(
        () => window.__fcControl({op: 'command', name: 'Std_DrawStyle', index: 1}));
    out.refusedPlain = await page.evaluate(
        () => window.__fcControl({op: 'command', name: 'Std_ViewFitAll'}));

    // 3. Into the sketch: the desktop switches workbench, and the bars
    // must follow.
    await page.evaluate((o) => window.fcviewerEdit(o, 0, ''), obj);
    try {
      await page.waitForFunction((o) => window.fcviewerEditing === o,
                                 {timeout: 20000}, obj);
      out.entered = true;
    }
    catch (e) {
      out.entered = false;
    }
    try {
      await page.waitForFunction(() => window.__fcButtons().some(
          b => b.group && !b.disabled && b.cmd.startsWith('Sketcher_Create')),
          {timeout: 30000});
      out.sketchTools = true;
    }
    catch (e) {
      out.sketchTools = false;
    }
    await sleep(3000);
    phase('entered');
    await waitAck('entered');
    out.bars2 = await page.evaluate(() => window.__fcBars());
    out.buttons2 = await page.evaluate(() => window.__fcButtons());

    // 4. A group's face: the default member runs, in this view -- the
    // tool's on-view parameters are the proof it is running here.
    out.face = await page.evaluate(async () => {
      const btn = Array.from(document.querySelectorAll('.fc-tb-btn')).find(
          b => !b.disabled && b.dataset.command.startsWith('Sketcher_Create')
            && b.parentElement.querySelector('.fc-tb-caret'));
      if (!btn) return {why: 'no enabled sketch group'};
      const cmd = btn.dataset.command;
      const before = window.__fcOnView;
      btn.click();
      await new Promise(r => setTimeout(r, 1500));
      await window.__fcMoves();
      await new Promise(r => setTimeout(r, 1500));
      return {cmd, onview: window.__fcOnView - before, error: window.__fcError()};
    });
    await page.evaluate(() => window.__fcEscape());
    await sleep(1500);

    // 5. A member from the drop-down that is not the default: it runs, and
    // the group's default moves the desktop's way, which the stream brings
    // back as the face's new command.
    out.member = await page.evaluate(async () => {
      const items = Array.from(document.querySelectorAll('.fc-tb-item')).filter(
          it => it.querySelector('.fc-tb-caret')
            && it.querySelector('.fc-tb-btn').dataset.command.startsWith('Sketcher_Create'));
      for (const item of items) {
        const face = item.querySelector('.fc-tb-btn');
        const before = face.dataset.command;
        item.querySelector('.fc-tb-caret').click();
        await new Promise(r => setTimeout(r, 300));
        const choice = Array.from(document.querySelectorAll('.fc-tb-menu-item')).find(
            m => !m.disabled && m.dataset.command && m.dataset.command !== before
              && m.dataset.command.startsWith('Sketcher_Create'));
        if (!choice) {
          window.__fcEscape();
          await new Promise(r => setTimeout(r, 200));
          continue;
        }
        const chosen = choice.dataset.command;
        const count = document.querySelectorAll('.fc-tb-menu-item').length;
        choice.click();
        const until = Date.now() + 8000;
        while (Date.now() < until && face.dataset.command === before)
          await new Promise(r => setTimeout(r, 100));
        return {before, chosen, after: face.dataset.command, members: count,
                menuClosed: !document.querySelector('.fc-tb-menu'),
                error: window.__fcError()};
      }
      return {why: 'no group with a second sketch tool'};
    });
    await page.evaluate(() => window.__fcEscape());
    await sleep(1500);

    // 6. The switch off: the strip goes, the choice is remembered, and the
    // server stops pushing -- which the caller provokes after the ack.
    out.off = await page.evaluate(async () => {
      const ok = window.__fcMenuToggle();
      await new Promise(r => setTimeout(r, 1500));
      let stored = null;
      try { stored = localStorage.getItem('fcviewer.toolbars'); } catch (e) {}
      return {ok, bars: window.__fcBars().length, stored,
              pushes: window.__fcWidgetPushes,
              topInset: getComputedStyle(document.getElementById('fc-ui'))
                .getPropertyValue('--fc-top')};
    });
    phase('toggled');
    await waitAck('toggled');
    await sleep(2000);
    out.off.pushesAfter = await page.evaluate(() => window.__fcWidgetPushes);

    // 7. And on again: a fresh snapshot, not an empty strip.
    out.on = await page.evaluate(async () => {
      const ok = window.__fcMenuToggle();
      const until = Date.now() + 15000;
      while (Date.now() < until && window.__fcBars().length === 0)
        await new Promise(r => setTimeout(r, 100));
      let stored = null;
      try { stored = localStorage.getItem('fcviewer.toolbars'); } catch (e) {}
      return {ok, bars: window.__fcBars().length, stored};
    });

    await page.evaluate(() => window.fcviewerResetEdit && window.fcviewerResetEdit());
    await sleep(1500);
  }
  catch (e) {
    out.error = String(e && e.message || e);
    console.error('DRIVE ERROR', e);
  }
  finally {
    await browser.close();
  }
  if (process.env.TOOLBAR_RESULT) {
    try { fs.writeFileSync(process.env.TOOLBAR_RESULT, JSON.stringify(out)); }
    catch (e) { console.error('result file:', e.message); }
  }
  console.log('RESULT ' + JSON.stringify(out));
  phase('done');
  if (out.error)
    process.exit(1);
})().catch(e => { console.error('DRIVE ERROR', e); process.exit(1); });
