// Drive a real browser viewer through an object's last change
// (docs/TransactionLog.md sec 32.19).
//
// tests/gui/serve-shape-diff-browser.py serves the document. The page asks
// for the edit mode that shows the last change of an object
// (window.fcviewerEdit, mode 6: Gui::ViewProvider::ValueDiff), and what
// only a browser can show is read here: that the two states are DRAWN --
// the session's overlay, counted as pixels of the colour a changed face is
// given -- that the task's panel comes up by itself with its slider, that
// moving the slider in the page is taken, that a left drag still turns the
// camera, and that all of it goes when the edit ends.
//
// Usage:  node scripts/shapediff-drive.js <url> <object> <settle_ms>
// SHAPEDIFF_PHASES and SHAPEDIFF_RESULT name files (node buffers stdout to
// a pipe): the phases as they pass, and the readings as JSON.
// SHAPEDIFF_SHOTS names a directory each counted canvas is written to.
const puppeteer = require(process.env.PUPPETEER_PATH || 'puppeteer-core');
const fs = require('fs');

const sleep = ms => new Promise(r => setTimeout(r, ms));

const phaseFile = process.env.SHAPEDIFF_PHASES;
// A phase is when the serving side reads its own state: it is given the
// time to, before the page goes on and changes it.
async function phase(name) {
  console.log('PHASE ' + name);
  if (phaseFile) {
    try { fs.appendFileSync(phaseFile, name + '\n'); }
    catch (e) { console.error('phase file:', e.message); }
  }
  await sleep(700);
}

const HELPERS = () => {
  // Pixels of the colour a changed face is drawn in: strong red and blue,
  // little green. Decoded in the page, which has the image decoder.
  window.__fcMagenta = async (b64) => {
    const img = new Image();
    img.src = 'data:image/png;base64,' + b64;
    await img.decode();
    const c = document.createElement('canvas');
    c.width = img.width;
    c.height = img.height;
    const g = c.getContext('2d', {willReadFrequently: true});
    g.drawImage(img, 0, 0);
    const d = g.getImageData(0, 0, c.width, c.height).data;
    let n = 0;
    for (let i = 0; i < d.length; i += 4) {
      const r = d[i], gr = d[i + 1], bl = d[i + 2];
      if (r > 150 && bl > 150 && gr < 0.6 * Math.min(r, bl))
        ++n;
    }
    return n;
  };
  window.__fcMouse = (type, x, y, buttons, button) => {
    const c = document.getElementById('canvas');
    const target = type === 'mousedown' ? c : document;
    target.dispatchEvent(new MouseEvent(type, {
      bubbles: true, cancelable: true, view: window,
      clientX: x, clientY: y, buttons: buttons, button: button || 0}));
  };
};

async function magenta(page, label) {
  const r = await page.evaluate(() => {
    const b = document.getElementById('canvas').getBoundingClientRect();
    return {x: b.left, y: b.top, w: b.width, h: b.height};
  });
  const b64 = await page.screenshot({
    encoding: 'base64',
    clip: {x: Math.round(r.x), y: Math.round(r.y),
           width: Math.round(r.w), height: Math.round(r.h)},
  });
  if (process.env.SHAPEDIFF_SHOTS && label) {
    try {
      fs.writeFileSync(process.env.SHAPEDIFF_SHOTS + '/' + label + '.png',
                       Buffer.from(b64, 'base64'));
    }
    catch (e) { console.error('shot file:', e.message); }
  }
  return page.evaluate(d => window.__fcMagenta(d), b64);
}

// Read until `done` says the count is the one expected, or the time runs
// out: the overlay comes and goes with a publish.
async function magentaUntil(page, label, done, ms) {
  const t0 = Date.now();
  let n = await magenta(page, label);
  while (!done(n) && Date.now() - t0 < ms) {
    await sleep(250);
    n = await magenta(page, label);
  }
  return n;
}

const panelText = () => {
  const s = document.querySelector('input.fc-panel-slider');
  let p = s;
  while (p && !(p.classList && p.classList.contains('fc-panel-box')))
    p = p.parentElement;
  return p ? p.innerText : null;
};

(async () => {
  const url = process.argv[2];
  const obj = process.argv[3] || 'Cut';
  const settleMs = parseInt(process.argv[4] || '9000', 10);
  if (!url) {
    console.error('usage: node shapediff-drive.js <url> [object] [settle_ms]');
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
    page.on('console', m => {
      const t = m.text();
      if (/edit mode|edit refused|PAGEERROR/i.test(t))
        console.log('  page: ' + t);
    });
    page.on('pageerror', e => console.log('  PAGEERROR: ' + (e.stack || e.message)));
    await page.goto(url, {waitUntil: 'domcontentloaded', timeout: 60000});
    await page.evaluate(HELPERS);
    await sleep(settleMs);
    out.before = await magenta(page, 'before');
    out.slidersBefore = await page.evaluate(
      () => document.querySelectorAll('input.fc-panel-slider').length);
    await phase('settled');

    out.asked = await page.evaluate((o) => !!window.fcviewerEdit(o, 6, ''), obj);
    try {
      await page.waitForFunction((o) => window.fcviewerEditing === o, {timeout: 20000}, obj);
      out.entered = true;
    }
    catch (e) { out.entered = false; }
    try {
      await page.waitForFunction(
        () => !!document.querySelector('input.fc-panel-slider'), {timeout: 15000});
      out.panel = true;
    }
    catch (e) { out.panel = false; }
    out.panelText = await page.evaluate(panelText);
    out.sliderValue = await page.evaluate(() => {
      const s = document.querySelector('input.fc-panel-slider');
      return s ? Number(s.value) : null;
    });
    out.inEdit = await magentaUntil(page, 'edit', n => n > 20000, 15000);
    await phase('entered');

    // A left drag across the view: the camera's, in this edit
    const box = await page.evaluate(() => {
      const b = document.getElementById('canvas').getBoundingClientRect();
      return {x: b.left + b.width * 0.35, y: b.top + b.height * 0.75};
    });
    await page.evaluate(async (b) => {
      window.__fcMouse('mousemove', b.x, b.y, 0, 0);
      window.__fcMouse('mousedown', b.x, b.y, 1, 0);
      for (let i = 1; i <= 8; ++i) {
        window.__fcMouse('mousemove', b.x + 12 * i, b.y - 4 * i, 1, 0);
        await new Promise(r => setTimeout(r, 30));
      }
      window.__fcMouse('mouseup', b.x + 96, b.y - 32, 0, 0);
    }, box);
    await sleep(1500);
    out.editingAfterDrag = await page.evaluate(() => window.fcviewerEditing);
    out.afterDrag = await magenta(page, 'dragged');
    await phase('dragged');

    out.moved = await page.evaluate(() => {
      const s = document.querySelector('input.fc-panel-slider');
      if (!s) return false;
      s.value = '0';
      s.dispatchEvent(new Event('input', {bubbles: true}));
      return true;
    });
    await sleep(2500);
    out.atBefore = await magenta(page, 'slid');
    await phase('slid');

    out.reset = await page.evaluate(
      () => (window.fcviewerResetEdit ? (window.fcviewerResetEdit(), true) : false));
    try {
      await page.waitForFunction(() => !window.fcviewerEditing, {timeout: 20000});
      out.left = true;
    }
    catch (e) { out.left = false; }
    out.after = await magentaUntil(page, 'after', n => n < 500, 15000);
    await sleep(500);
    out.slidersAfter = await page.evaluate(
      () => document.querySelectorAll('input.fc-panel-slider').length);
    await phase('left');
  }
  catch (e) {
    out.error = String((e && e.stack) || e);
    console.log('drive failed: ' + out.error);
  }
  if (process.env.SHAPEDIFF_RESULT) {
    try { fs.writeFileSync(process.env.SHAPEDIFF_RESULT, JSON.stringify(out)); }
    catch (e) { console.error('result file:', e.message); }
  }
  console.log(JSON.stringify(out));
  await phase('done');
  await browser.close();
})();
