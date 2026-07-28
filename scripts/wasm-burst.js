// Screenshot the WASM viewer repeatedly *while* a scene streams in, over a
// throttled link, so the coarse rungs of the fidelity ladder
// (docs/SceneStreaming.md §6) are on screen long enough to be captured.
//
// Loopback delivers a whole scene faster than a frame, so a plain
// wasm-shot.js of a streaming load photographs the finished model and a
// broken ladder looks identical to a working one. This throttles the page
// through CDP first, then shoots at fixed times from navigation.
//
// Pair it with the viewer's `&stream` flag, which prints per assembly pass:
//   fcviewer: scene at N draws, M of them coarse, K objects still arriving
// The counts are the checkable part; the shots are what shows a rung is
// placed and coloured correctly.
//
// Usage:
//   node scripts/wasm-burst.js <url> <out-prefix> [ms,ms,...]
// e.g.
//   COUNT=200 scripts/renderer-serve.sh scripts/demo-varied.py 8077
//   KBPS=3000 node scripts/wasm-burst.js \
//     'http://127.0.0.1:8011/fcviewer.html?scene=http://127.0.0.1:8077&noidb&stream&cam=0.6,0.4,300,60,60,5,0,0' \
//     /tmp/rung 4000,9000,16000,30000
//   # -> /tmp/rung-4000.png … , then the filtered console log
//
// KBPS  link speed to emulate, default 800 (kbit/s). Pick it against the
//       scene: demo-varied at 200 objects is ~7 MB, so 3000 spreads the
//       load over ~20 s. Too fast and there is nothing to see; too slow
//       and the run outlasts your patience.
// PUPPETEER_PATH  a node_modules puppeteer install (LD_LIBRARY_PATH must
//       include the conda lib dir so Chromium finds libasound etc.) — see
//       wasm-shot.js.
//
// Headless Chromium renders on swiftshader, so this validates the ladder's
// logic and layout, not device-GPU precision.
const path = process.env.PUPPETEER_PATH || 'puppeteer';
const puppeteer = require(path);

(async () => {
  const url = process.argv[2];
  const prefix = process.argv[3];
  if (!url || !prefix) {
    console.error('usage: node wasm-burst.js <url> <out-prefix> [ms,ms,...]');
    process.exit(1);
  }
  const at = (process.argv[4] || '400,900,1800,4000,9000')
        .split(',').map(Number);
  const kbps = Number(process.env.KBPS || 800);

  const browser = await puppeteer.launch({
    headless: 'new',
    args: ['--no-sandbox', '--enable-unsafe-swiftshader',
           '--use-angle=swiftshader', '--window-size=1100,900'],
  });
  const page = await browser.newPage();
  await page.setViewport({width: 1100, height: 900});

  const logs = [];
  page.on('console', m => {
    const t = m.text();
    // WebGL warnings drown everything else on swiftshader.
    if (t.startsWith('fcviewer') || t.startsWith('bgfx')) logs.push(t);
  });
  page.on('pageerror', e => logs.push('PAGEERROR: ' + (e.stack || e.message)));

  const cdp = await page.target().createCDPSession();
  await cdp.send('Network.enable');
  await cdp.send('Network.emulateNetworkConditions', {
    offline: false, latency: 20,
    downloadThroughput: kbps * 1024 / 8,
    uploadThroughput: kbps * 1024 / 8,
  });

  // domcontentloaded, not networkidle0: the point is to be shooting while
  // the scene is still on the wire.
  await page.goto(url, {waitUntil: 'domcontentloaded', timeout: 60000});
  const t0 = Date.now();
  for (const ms of at) {
    const wait = ms - (Date.now() - t0);
    if (wait > 0) await new Promise(r => setTimeout(r, wait));
    await page.screenshot({path: `${prefix}-${ms}.png`});
    console.log(`--- shot at ${ms}ms (${logs.length} log lines so far)`);
  }
  console.log(logs.join('\n'));
  await browser.close();
})();
