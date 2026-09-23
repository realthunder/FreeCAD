// Drive two real browser viewers through a FOREIGN selection: one picks,
// the other must paint it as somebody else's (docs/ThinClient.md sec
// 8.11a).
//
// tests/gui/serve-peer-selection-browser.py owns the server half -- it
// serves the document and puts both connections on the `everyone` route.
// What only a browser can show is the half that lives in the viewer:
// src/Gui/Renderer/wasm/main.cpp resolving the pushed OBJECT NAMES back
// into draws of its own scene and feeding them as a second highlight.
// A synthetic client can see the push arrive; it cannot see whether
// anything was drawn, which is the whole of this feature.
//
// So the reading here is PIXELS, against a measured noise floor: the
// watcher's canvas must change when the peer picks, change back when the
// peer leaves, and its OWN selection must never move while either
// happens.
//
// Usage:  node scripts/peersel-drive.js <url> <settle_ms>
// Phases (also appended to PEERSEL_PHASES, a file, because node buffers
// its stdout to a pipe and the caller samples on these):
//   PHASE ready | routed | picked | gone | done
// PEERSEL_GO names a file the caller creates once it has set the routes;
// PEERSEL_RESULT names the file the readings are written to as JSON.
const ppPath = process.env.PUPPETEER_PATH || 'puppeteer-core';
const puppeteer = require(ppPath);
const fs = require('fs');

const sleep = ms => new Promise(r => setTimeout(r, ms));

const phaseFile = process.env.PEERSEL_PHASES;
function phase(name) {
  console.log('PHASE ' + name);
  if (phaseFile) {
    try { fs.appendFileSync(phaseFile, name + '\n'); }
    catch (e) { console.error('phase file:', e.message); }
  }
}

// Injected rather than driven through page.mouse: every CDP input call
// waits on the page's main thread, and a viewer busy with a scene starves
// them (scripts/edit-drive.js and onview-drive.js say the same).
// Emscripten registers plain listeners, so a dispatched event reaches
// exactly the code a real one would.
const HELPERS = () => {
  // Kept from the first moment, so a push that arrives before a poll is
  // not missed.
  window.__fcSel = [];
  window.addEventListener('fc:selection',
                          e => window.__fcSel.push(e.detail || []));
  window.__fcPeer = [];
  window.addEventListener('fc:peerselection',
                          e => window.__fcPeer.push(e.detail || {}));

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

  // Screenshots are decoded and kept IN THE PAGE: a 1100x900 frame is
  // four million numbers, and handing that across CDP per comparison
  // would cost more than the test.
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
  // Pixels that differ by more than a compression/dither margin. The
  // absolute number matters less than the ratio to the noise floor the
  // caller measures with two shots of a scene nobody touched.
  window.__fcDiff = (a, b) => {
    const x = window.__fcShots[a], y = window.__fcShots[b];
    if (!x || !y || x.width !== y.width || x.height !== y.height)
      return -1;
    let n = 0;
    for (let i = 0; i < x.data.length; i += 4) {
      if (Math.abs(x.data[i] - y.data[i]) > 24
          || Math.abs(x.data[i + 1] - y.data[i + 1]) > 24
          || Math.abs(x.data[i + 2] - y.data[i + 2]) > 24)
        ++n;
    }
    return n;
  };
  // Pixels near one of the foreign-selection colours (peerColor() in
  // main.cpp). Reported rather than asserted on: the outline goes
  // through the renderer's own colour handling, so the hue that lands
  // is the palette entry only up to that.
  window.__fcPalette = (name) => {
    const shot = window.__fcShots[name];
    if (!shot)
      return -1;
    const palette = [[0xFF, 0x4F, 0xA3], [0x00, 0xC2, 0xD6],
                     [0xFF, 0x9A, 0x1F], [0xA1, 0x66, 0xFF],
                     [0x3F, 0xA9, 0xFF], [0xFF, 0x6B, 0x6B]];
    let n = 0;
    for (let i = 0; i < shot.data.length; i += 4) {
      for (const p of palette) {
        if (Math.abs(shot.data[i] - p[0]) < 40
            && Math.abs(shot.data[i + 1] - p[1]) < 40
            && Math.abs(shot.data[i + 2] - p[2]) < 40) {
          ++n;
          break;
        }
      }
    }
    return n;
  };
};

/// Shoot the canvas of `page` and keep it in that page under `name`.
async function capture(page, name) {
  const rect = await page.evaluate(() => window.__fcRect());
  const b64 = await page.screenshot({
    encoding: 'base64',
    clip: {x: Math.round(rect.x), y: Math.round(rect.y),
           width: Math.round(rect.w), height: Math.round(rect.h)},
  });
  return page.evaluate((n, d) => window.__fcCapture(n, d), name, b64);
}

(async () => {
  const url = process.argv[2];
  const settleMs = parseInt(process.argv[3] || '12000', 10);
  if (!url) {
    console.error('usage: node peersel-drive.js <url> [settle_ms]');
    process.exit(2);
  }
  // ONE BROWSER EACH, not two tabs of one. A background tab composites
  // no frames: its requestAnimationFrame stops, its canvas stops
  // repainting, and a test whose reading is pixels then reads a frozen
  // picture -- which is how the first run of this hung, inside an
  // evaluate that was waiting on a frame that was never going to come.
  const real = !!process.env.PEERSEL_REAL;
  const launch = (i) => real
    ? require('./wasm-chrome').launch({
        headless: false,
        args: ['--window-size=900,760',
               '--window-position=' + (i * 460) + ',40'],
      })
    : puppeteer.launch({
        executablePath: process.env.CHROME,
        headless: true,
        protocolTimeout: 240000,
        args: ['--no-sandbox', '--enable-unsafe-swiftshader',
               '--use-angle=swiftshader', '--window-size=900,760',
               '--disable-background-timer-throttling',
               '--disable-backgrounding-occluded-windows',
               '--disable-renderer-backgrounding'],
      });
  const browsers = [await launch(0), await launch(1)];
  const out = {};
  try {
    const pages = [];
    for (const [i, tag] of ['picker', 'watcher'].entries()) {
      const page = (await browsers[i].pages())[0]
          || await browsers[i].newPage();
      page.setDefaultTimeout(60000);
      await page.setViewport({width: 900, height: 760});
      let kept = 0;
      page.on('console', m => {
        const t = m.text();
        if (kept < 60 && /selection|peer|websocket|refused/i.test(t)) {
          ++kept;
          console.log(tag + ':', t);
        }
      });
      page.on('pageerror', e => console.log(tag + ' ERROR: ' + e.message));
      await page.goto(url, {waitUntil: 'domcontentloaded'});
      await page.evaluate(HELPERS);
      pages.push(page);
    }
    const [picker, watcher] = pages;
    await sleep(settleMs);

    // Which tier this actually ran on. Headless swiftshader has masked
    // GPU and pacing bugs before, so a green run that does not say what
    // drew it is worth less than it looks.
    out.gpu = await watcher.evaluate(() => {
      try {
        const c = document.createElement('canvas');
        const gl = c.getContext('webgl2') || c.getContext('webgl');
        if (!gl)
          return 'no webgl';
        const dbg = gl.getExtension('WEBGL_debug_renderer_info');
        return String(dbg ? gl.getParameter(dbg.UNMASKED_RENDERER_WEBGL)
                          : gl.getParameter(gl.RENDERER));
      }
      catch (e) {
        return 'error: ' + e.message;
      }
    });

    // Both connections must have stated a camera before either can be
    // told about the other: that is what builds the mirror the server
    // announces through, and under the default uplink policy a page
    // nobody has clicked in has stated nothing (sec 8.10b). A click on
    // empty canvas states one and selects nothing -- and it happens
    // BEFORE the baseline shots, so what it costs in pixels is inside
    // the noise floor rather than on top of the reading.
    for (const page of pages) {
      await page.evaluate(() => {
        const r = window.__fcRect();
        window.__fcClick(Math.round(r.x + 12), Math.round(r.y + 12));
      });
    }
    await sleep(3000);
    phase('ready');

    // The caller now puts both connections on the `everyone` route --
    // only the host may, which is the point of the policy.
    const go = process.env.PEERSEL_GO;
    for (let i = 0; i < 600 && go && !fs.existsSync(go); ++i)
      await sleep(100);
    out.routed = !go || fs.existsSync(go);
    phase('routed');
    await sleep(1000);

    // The noise floor: two shots of a scene nobody has touched. Any
    // later reading is a multiple of this or it is nothing.
    await capture(watcher, 'base');
    await sleep(1200);
    await capture(watcher, 'base2');
    out.noise = await watcher.evaluate(() => window.__fcDiff('base', 'base2'));
    out.basePalette = await watcher.evaluate(() => window.__fcPalette('base2'));
    out.watcherSelBefore = await watcher.evaluate(() => window.__fcSel.length);

    // The pick. Off centre in both axes, so a highlight that landed by
    // the geometry of the viewport rather than by the pick would show.
    const at = await picker.evaluate(() => {
      const r = window.__fcRect();
      return {x: Math.round(r.cx - r.w * 0.09),
              y: Math.round(r.cy + r.h * 0.07)};
    });
    out.clickedAt = at;
    await picker.evaluate((p) => window.__fcClick(p.x, p.y), at);
    await sleep(4000);
    out.pickerSel = await picker.evaluate(
        () => window.__fcSel[window.__fcSel.length - 1] || []);
    out.peer = await watcher.evaluate(
        () => window.__fcPeer[window.__fcPeer.length - 1] || null);
    out.peerPushes = await watcher.evaluate(() => window.__fcPeer.length);
    out.watcherSelAfter = await watcher.evaluate(() => window.__fcSel.length);
    await capture(watcher, 'painted');
    out.paintedDiff = await watcher.evaluate(
        () => window.__fcDiff('base2', 'painted'));
    out.paintedPalette = await watcher.evaluate(
        () => window.__fcPalette('painted'));
    phase('picked');

    // And the peer leaves. Its highlight is nobody's now, and nothing
    // else would ever take it down: the pushes are change-driven, and a
    // connection that has gone announces nothing.
    await browsers[0].close();
    await sleep(4000);
    out.peerPushesAfterGone = await watcher.evaluate(
        () => window.__fcPeer.length);
    out.peerAfterGone = await watcher.evaluate(
        () => window.__fcPeer[window.__fcPeer.length - 1] || null);
    await capture(watcher, 'gone');
    out.goneVsPainted = await watcher.evaluate(
        () => window.__fcDiff('painted', 'gone'));
    out.goneVsBase = await watcher.evaluate(
        () => window.__fcDiff('base2', 'gone'));
    out.watcherSelEnd = await watcher.evaluate(() => window.__fcSel.length);
    out.watcherOwnSel = await watcher.evaluate(
        () => window.__fcSel[window.__fcSel.length - 1] || []);
    phase('gone');
  }
  catch (e) {
    out.error = (e && e.stack) || String(e);
    console.error(out.error);
  }
  finally {
    for (const b of browsers) {
      try { await b.close(); } catch (e) { /* closing is best effort */ }
    }
  }
  if (process.env.PEERSEL_RESULT) {
    try {
      fs.writeFileSync(process.env.PEERSEL_RESULT, JSON.stringify(out, null, 1));
    }
    catch (e) {
      console.error('result file:', e.message);
    }
  }
  phase('done');
  process.exit(out.error ? 1 : 0);
})();
