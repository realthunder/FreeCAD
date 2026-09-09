// Drive the WASM viewer through a camera orbit and a click, for the
// camera-uplink experiment (docs/ThinClient.md sec 8.10a).
//
// The bench that produces the numbers is tests/gui/camera-uplink-bench.py,
// which speaks the wire itself. This one is the other question: does the
// REAL client -- src/Gui/Renderer/wasm/main.cpp under ?camup= -- put on
// the link what the policy says it should? It answers nothing itself; it
// makes the browser orbit and click, and the serving process counts what
// arrived (Gui.serveClients()). A client counting its own sends would be
// the client marking its own work.
//
// Usage:  node scripts/camup-drive.js <url> <settle_ms> <orbit_ms> <clicks>
// Prints one line per phase so the caller can align its counter samples:
//   PHASE settled | PHASE orbited | PHASE clicked | PHASE done
//
// puppeteer-core is resolved from PUPPETEER_PATH; CHROME names the
// executable. Headless swiftshader is fine: nothing here is judged by
// pixels, and picking is CPU-side.
const ppPath = process.env.PUPPETEER_PATH || 'puppeteer-core';
const puppeteer = require(ppPath);
const fs = require('fs');

const sleep = ms => new Promise(r => setTimeout(r, ms));

// The phases go to a FILE, not only to stdout. The caller samples its
// counters when a phase passes, so a marker that arrives late attributes
// an orbit's bytes to the settle before it -- and node's stdout to a pipe
// is buffered, which made every marker arrive at once, at the end, and
// the caller wait out its whole timeout for the first one. A file append
// is seen by the reader immediately.
const phaseFile = process.env.CAMUP_PHASES;
function phase(name) {
  console.log('PHASE ' + name);
  if (phaseFile) {
    try { fs.appendFileSync(phaseFile, name + '\n'); }
    catch (e) { console.error('phase file:', e.message); }
  }
}

(async () => {
  const url = process.argv[2];
  const settleMs = parseInt(process.argv[3] || '12000', 10);
  const orbitMs = parseInt(process.argv[4] || '10000', 10);
  const clicks = parseInt(process.argv[5] || '4', 10);
  if (!url) {
    console.error('usage: node camup-drive.js <url> [settle_ms] [orbit_ms] [clicks]');
    process.exit(2);
  }

  // A page that cannot draw must not hold the run open for ever: every
  // CDP call gets a bound, and a stuck one fails the run with a reason
  // rather than as a timeout with none.
  const browser = await puppeteer.launch({
    executablePath: process.env.CHROME,
    headless: true,
    // A starved page answers slowly; the in-page gesture below runs as
    // one protocol call and must be allowed to take the orbit's length
    // plus whatever the page's frame rate really is.
    protocolTimeout: 180000,
    args: ['--no-sandbox', '--enable-unsafe-swiftshader',
           '--use-angle=swiftshader', '--window-size=1100,900',
           '--disable-background-timer-throttling',
           '--disable-backgrounding-occluded-windows',
           '--disable-renderer-backgrounding'],
  });
  try {
    const page = await browser.newPage();
    page.setDefaultTimeout(60000);
    await page.setViewport({width: 1100, height: 900});
    // The viewer says which policy it took, and that is worth having in
    // the log: a mistyped parameter would otherwise look exactly like a
    // policy that does not help. Everything else is dropped, and even
    // the kept lines are capped -- a viewer that cannot make a
    // framebuffer repeats its complaint every frame, and reading that
    // through a pipe is what stalled the first run of this harness.
    // CAMUP_VERBOSE keeps everything the page says, up to a cap: what
    // is filtered out here is exactly what you want when the viewer
    // itself is the suspect rather than the policy.
    const verbose = !!process.env.CAMUP_VERBOSE;
    const cap = verbose ? 400 : 40;
    let kept = 0;
    page.on('console', m => {
      const t = m.text();
      if (kept < cap
              && (verbose
                  || /camera uplink|websocket (connected|lost)|scene update/i.test(t))) {
        ++kept;
        console.log('page:', t);
      }
    });
    await page.goto(url, {waitUntil: 'domcontentloaded'});
    await sleep(settleMs);
    phase('settled');

    // Orbit: the viewer turns the camera on a left-button drag. One
    // continuous gesture, so the camera changes every frame -- which is
    // the case the per-frame policy is worst at and the lazy one is
    // meant for.
    // The gesture is dispatched INSIDE the page, not through
    // page.mouse. Every CDP input call waits on the page's main thread,
    // and the viewer under headless swiftshader cannot build its scene
    // framebuffer -- it retries and complains once per frame, which
    // starves those calls badly enough that a drag never finishes.
    // Emscripten registers plain addEventListener handlers, so a
    // dispatched MouseEvent reaches exactly the code a real one would,
    // and pacing the moves on requestAnimationFrame gives the one move
    // per client frame that the camera policies key off.
    await page.evaluate(async (ms) => {
      const c = document.getElementById('canvas');
      const r = c.getBoundingClientRect();
      const cx = r.left + r.width / 2, cy = r.top + r.height / 2;
      const rad = Math.min(r.width, r.height) * 0.3;
      const send = (type, target, x, y, buttons) => target.dispatchEvent(
          new MouseEvent(type, {bubbles: true, cancelable: true, view: window,
                                clientX: x, clientY: y, buttons, button: 0}));
      send('mousedown', c, cx + rad, cy, 1);
      const t0 = performance.now();
      for (;;) {
        const t = performance.now() - t0;
        if (t >= ms) break;
        const a = (t / ms) * Math.PI * 2;
        send('mousemove', document, cx + rad * Math.cos(a),
             cy + rad * 0.35 * Math.sin(a), 1);
        await new Promise(res => requestAnimationFrame(res));
      }
      send('mouseup', document, cx + rad, cy, 0);
    }, orbitMs);
    await sleep(500);
    phase('orbited');

    // Clicks in the middle of the canvas, where the model is: the pick
    // the whole camera uplink exists to answer. Same door as the drag.
    await page.evaluate(async (n) => {
      const c = document.getElementById('canvas');
      const r = c.getBoundingClientRect();
      const cx = r.left + r.width / 2, cy = r.top + r.height / 2;
      const send = (type, x, y, buttons) => c.dispatchEvent(
          new MouseEvent(type, {bubbles: true, cancelable: true, view: window,
                                clientX: x, clientY: y, buttons, button: 0}));
      for (let i = 0; i < n; ++i) {
        const x = cx + (i % 2 ? 40 : -40), y = cy + (i % 2 ? 30 : -30);
        send('mousedown', x, y, 1);
        await new Promise(res => setTimeout(res, 40));
        send('mouseup', x, y, 0);
        await new Promise(res => setTimeout(res, 700));
      }
    }, clicks);
    phase('clicked');
    await sleep(300);
  }
  finally {
    await browser.close();
  }
  phase('done');
})().catch(e => { console.error('DRIVE ERROR', e); process.exit(1); });
