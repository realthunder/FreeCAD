// Drive two real browser viewers through ONE client's own object
// visibility (docs/CoinRetirement.md 5.18, per client).
//
// tests/gui/serve-client-visibility-browser.py serves the document. Client
// A sets its own ObjectVisibilities map with the `view.visibility` op; the
// host parses it onto A's mirror and tells A the parsed table back, and A's
// viewer draws by it with the renderer's own rule, against the object
// chains the scene carries (SceneDump v80). What only a browser can show is
// that half: what A DRAWS, and what B, which set nothing, keeps drawing.
//
// The reading is pixels by colour: Box is red, Box2 green, the hidden box
// yellow, so each count says what is on a canvas without knowing where the
// viewer put its camera.
//
// Usage:  node scripts/clientvis-drive.js <url> <settle_ms>
// CLIENTVIS_PHASES and CLIENTVIS_RESULT name files (node buffers stdout to
// a pipe): the phases as they pass, and the readings as JSON.
const ppPath = process.env.PUPPETEER_PATH || 'puppeteer-core';
const puppeteer = require(ppPath);
const fs = require('fs');

const sleep = ms => new Promise(r => setTimeout(r, ms));

const phaseFile = process.env.CLIENTVIS_PHASES;
function phase(name) {
  console.log('PHASE ' + name);
  if (phaseFile) {
    try { fs.appendFileSync(phaseFile, name + '\n'); }
    catch (e) { console.error('phase file:', e.message); }
  }
}

// Injected: see scripts/peersel-drive.js for why input is dispatched in
// the page and screenshots are decoded there.
const HELPERS = () => {
  window.__fcSel = [];
  window.addEventListener('fc:selection',
                          e => window.__fcSel.push(e.detail || []));
  window.__fcControl = [];
  window.addEventListener('fc:control', e => {
    const d = e.detail;
    window.__fcControl.push(typeof d === 'string' ? d : JSON.stringify(d));
  });
  window.__fcRect = () => {
    const c = document.getElementById('canvas');
    const r = c.getBoundingClientRect();
    return {cx: r.left + r.width / 2, cy: r.top + r.height / 2,
            x: r.left, y: r.top, w: r.width, h: r.height};
  };
  window.__fcMouse = (type, x, y, buttons, button) => {
    const c = document.getElementById('canvas');
    const target = type === 'mousedown' ? c : document;
    target.dispatchEvent(new MouseEvent(type, {
      bubbles: true, cancelable: true, view: window,
      clientX: x, clientY: y, buttons: buttons, button: button || 0}));
  };
  window.__fcClick = (x, y) => {
    window.__fcMouse('mousedown', x, y, 1, 0);
    window.__fcMouse('mouseup', x, y, 0, 0);
  };
  // One op through the viewer's own control socket, and its answer.
  window.__fcOp = (id, fields) => new Promise(resolve => {
    const want = '"id":' + id;
    const t0 = Date.now();
    const poll = () => {
      const hit = window.__fcControl.find(s => s.indexOf(want) >= 0);
      if (hit || Date.now() - t0 > 10000)
        return resolve(hit || null);
      setTimeout(poll, 50);
    };
    window.fcviewerControlSend(JSON.stringify(Object.assign({id: id}, fields)));
    poll();
  });

  window.__fcShots = {};
  window.__fcCapture = async (name, b64) => {
    const img = new Image();
    img.src = 'data:image/png;base64,' + b64;
    await img.decode();
    const c = document.createElement('canvas');
    c.width = img.width;
    c.height = img.height;
    const g = c.getContext('2d', {willReadFrequently: true});
    g.drawImage(img, 0, 0);
    window.__fcShots[name] = g.getImageData(0, 0, c.width, c.height);
    return {w: c.width, h: c.height};
  };
  // Pixel counts by the three colours, and each colour's centroid in
  // page coordinates (the canvas rect's origin added back).
  window.__fcColours = (name) => {
    const s = window.__fcShots[name];
    if (!s)
      return null;
    const r0 = window.__fcRect();
    const out = {};
    for (const k of ['red', 'green', 'yellow'])
      out[k] = {n: 0, x: 0, y: 0};
    for (let i = 0, p = 0; i < s.data.length; i += 4, ++p) {
      const r = s.data[i], g = s.data[i + 1], b = s.data[i + 2];
      let k = null;
      if (r > 110 && g > 110 && b < 0.45 * Math.min(r, g))
        k = 'yellow';
      else if (r > 110 && r > 1.8 * g && r > 1.8 * b)
        k = 'red';
      else if (g > 110 && g > 1.8 * r && g > 1.8 * b)
        k = 'green';
      if (!k)
        continue;
      const o = out[k];
      ++o.n;
      o.x += p % s.width;
      o.y += Math.floor(p / s.width);
    }
    for (const k in out) {
      const o = out[k];
      if (o.n) {
        o.x = Math.round(r0.x + o.x / o.n);
        o.y = Math.round(r0.y + o.y / o.n);
      }
    }
    return out;
  };
};

async function capture(page, name) {
  const rect = await page.evaluate(() => window.__fcRect());
  const b64 = await page.screenshot({
    encoding: 'base64',
    clip: {x: Math.round(rect.x), y: Math.round(rect.y),
           width: Math.round(rect.w), height: Math.round(rect.h)},
  });
  await page.evaluate((n, d) => window.__fcCapture(n, d), name, b64);
  return page.evaluate(n => window.__fcColours(n), name);
}

(async () => {
  const url = process.argv[2];
  const settleMs = parseInt(process.argv[3] || '12000', 10);
  if (!url) {
    console.error('usage: node clientvis-drive.js <url> [settle_ms]');
    process.exit(2);
  }
  // One browser each: a background tab composites no frames.
  const launch = () => puppeteer.launch({
    executablePath: process.env.CHROME,
    headless: true,
    protocolTimeout: 240000,
    args: ['--no-sandbox', '--enable-unsafe-swiftshader',
           '--use-angle=swiftshader', '--window-size=900,760',
           '--disable-background-timer-throttling',
           '--disable-backgrounding-occluded-windows',
           '--disable-renderer-backgrounding'],
  });
  const browsers = [await launch(), await launch()];
  const out = {};
  try {
    const pages = [];
    for (const [i, tag] of ['a', 'b'].entries()) {
      const page = (await browsers[i].pages())[0] || await browsers[i].newPage();
      page.setDefaultTimeout(60000);
      await page.setViewport({width: 900, height: 760});
      let kept = 0;
      page.on('console', m => {
        const t = m.text();
        if (kept < 40 && /visib|refused|websocket|error/i.test(t)) {
          ++kept;
          console.log(tag + ':', t);
        }
      });
      page.on('pageerror', e => console.log(tag + ' ERROR: ' + e.message));
      await page.goto(url, {waitUntil: 'domcontentloaded'});
      await page.evaluate(HELPERS);
      pages.push(page);
    }
    const [a, b] = pages;
    await sleep(settleMs);
    // A click on empty canvas states each client's camera (its pick
    // needs one) and selects nothing.
    for (const page of pages) {
      await page.evaluate(() => {
        const r = window.__fcRect();
        window.__fcClick(Math.round(r.x + 12), Math.round(r.y + 12));
      });
    }
    await sleep(2000);
    phase('ready');

    out.a0 = await capture(a, 'a0');
    out.b0 = await capture(b, 'b0');

    // 1. A hides Box2, for itself.
    out.hideReply = await a.evaluate(
        () => window.__fcOp(900001, {op: 'view.visibility',
                                     map: {Box2: '0'}, perView: true}));
    await sleep(3000);
    out.a1 = await capture(a, 'a1');
    out.b1 = await capture(b, 'b1');
    phase('hidden');

    // 2. ...and shows the hidden box, for itself. The host captures it
    // for everyone, flagged; only A's own table admits it.
    out.showReply = await a.evaluate(
        () => window.__fcOp(900002, {op: 'view.visibility',
                                     map: {Box2: '0', Hid: '1'}, perView: true}));
    await sleep(5000);
    out.a2 = await capture(a, 'a2');
    out.b2 = await capture(b, 'b2');
    phase('shown');

    // 3. A click where Box2 WAS drawn picks nothing in A: what a client
    // does not see, it does not pick -- locally or on the host.
    const at = out.a0 && out.a0.green && out.a0.green.n ? out.a0.green : null;
    out.clickedAt = at;
    if (at) {
      const before = await a.evaluate(() => window.__fcSel.length);
      await a.evaluate(p => window.__fcClick(p.x, p.y), at);
      await sleep(3000);
      out.selAfterClick = await a.evaluate(
          n => window.__fcSel.slice(n), before);
    }
    phase('clicked');

    // 4. A clears its map: back to what everyone draws.
    out.clearReply = await a.evaluate(
        () => window.__fcOp(900003, {op: 'view.visibility', map: {}}));
    await sleep(4000);
    out.a3 = await capture(a, 'a3');
    out.b3 = await capture(b, 'b3');
    phase('cleared');
  }
  catch (e) {
    out.error = (e && e.stack) || String(e);
    console.error(out.error);
  }
  finally {
    for (const br of browsers) {
      try { await br.close(); } catch (e) { /* best effort */ }
    }
  }
  if (process.env.CLIENTVIS_RESULT) {
    try {
      fs.writeFileSync(process.env.CLIENTVIS_RESULT, JSON.stringify(out, null, 1));
    }
    catch (e) {
      console.error('result file:', e.message);
    }
  }
  phase('done');
  process.exit(out.error ? 1 : 0);
})();
