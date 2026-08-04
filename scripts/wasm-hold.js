// Open the WASM viewer in headless Chromium and hold the page while the
// backend drives the dumpFrame protocol (saveRenderDump source='viewer',
// docs/RenderDebug.md §4.4). Headless Chromium renders on swiftshader, so
// this validates function/layout, not device-GPU precision — real-GPU
// browser evidence comes from a real device left on the page.
//
// Usage:  node scripts/wasm-hold.js <url> [hold_ms]
//   Pin the camera with the page's &cam=yaw,pitch,dist,cx,cy,cz,panX,panY
//   query parameter so captures are reproducible.
// puppeteer: resolved via require, or set PUPPETEER_PATH to a node_modules
// puppeteer install (see wasm-shot.js).
const path = process.env.PUPPETEER_PATH || 'puppeteer';
const puppeteer = require(path);

(async () => {
  const url = process.argv[2];
  const holdMs = parseInt(process.argv[3] || '180000', 10);
  if (!url) { console.error('usage: node wasm-hold.js <url> [hold_ms]'); process.exit(1); }
  // A held page is a background page, and Chromium throttles background
  // timers hard enough that the viewer's WebSocket reconnect backoff
  // never fires — so a backend restarted under the page is never
  // reconnected to and anything about resync is untestable here.
  const awake = ['--disable-background-timer-throttling',
                 '--disable-backgrounding-occluded-windows',
                 '--disable-renderer-backgrounding'];
  // HEADFUL=1 holds the page on the WSLg desktop against the real GPU
  // instead of swiftshader (scripts/wasm-chrome.js). Swiftshader gets
  // float render targets and MRT wrong — a stateful particle emitter
  // renders as uniform noise there while being correct on a device —
  // so anything about float state, precision or GPU pipeline behaviour
  // has to be judged on this tier. It OPENS A WINDOW on the desktop.
  const browser = process.env.HEADFUL
    ? await require('./wasm-chrome.js').launch({headless: false, args: awake})
    : await puppeteer.launch({
        headless: 'new',
        args: ['--no-sandbox', '--enable-unsafe-swiftshader',
               '--use-angle=swiftshader', '--window-size=1100,900',
               ...awake],
      });
  const page = await browser.newPage();
  await page.setViewport({width: 1100, height: 900});
  page.on('console', m => console.log('PAGE: ' + m.text()));
  page.on('pageerror', e => console.log('PAGEERROR: ' + (e.stack || e.message)));
  await page.goto(url, {waitUntil: 'networkidle0', timeout: 60000});
  console.log('HOLDING ' + url);
  await new Promise(r => setTimeout(r, holdMs));
  await browser.close();
})();
