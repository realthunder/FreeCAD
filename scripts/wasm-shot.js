// Screenshot the WASM viewer streaming a scene, at a chosen camera, without
// touching any live desktop GUI view (it's a separate headless client of the
// same scene stream). Headless Chromium uses swiftshader (no real GPU under
// WSL2), so this is for layout/logic checks, not real-GPU visuals.
//
// Usage:
//   node scripts/wasm-shot.js <url> <out.png>
// e.g.
//   node scripts/wasm-shot.js \
//     'http://127.0.0.1:8000/fcviewer.html?scene=http://127.0.0.1:8077&cam=0.6,0.3,70,0,0,5,0,0' \
//     shot.png
//
// puppeteer: resolved via require, or set PUPPETEER_PATH to a node_modules
// puppeteer install (LD_LIBRARY_PATH must include the conda lib dir so
// Chromium finds libasound etc.).
const path = process.env.PUPPETEER_PATH || 'puppeteer';
const puppeteer = require(path);

(async () => {
  const url = process.argv[2];
  const out = process.argv[3] || 'shot.png';
  if (!url) { console.error('usage: node wasm-shot.js <url> <out.png>'); process.exit(1); }
  const browser = await puppeteer.launch({
    headless: 'new',
    args: ['--no-sandbox', '--enable-unsafe-swiftshader',
           '--use-angle=swiftshader', '--window-size=1100,900'],
  });
  const page = await browser.newPage();
  await page.setViewport({width: 1100, height: 900});
  const logs = [];
  page.on('console', m => logs.push(m.text()));
  page.on('pageerror', e => logs.push('PAGEERROR: ' + (e.stack || e.message)));
  await page.goto(url, {waitUntil: 'networkidle0', timeout: 60000});
  await new Promise(r => setTimeout(r, 9000));
  await page.screenshot({path: out});
  console.log(logs.slice(-20).join('\n'));
  await browser.close();
})();
