// Load the WASM viewer under a tight geometry budget, let it converge, then
// orbit the camera so resident payloads stop being the best ones to hold --
// which is the only thing that makes the ladder run *backwards*
// (docs/SceneStreaming.md §6 phase 4b). A static camera converges and then
// merely refuses new chunks; nothing is ever given back, so the release path
// goes untested by wasm-burst.js.
//
// Usage:  node scripts/wasm-orbit.js <url> [settle_ms] [orbit_steps]
// e.g.
//   COUNT=200 scripts/renderer-serve.sh scripts/demo-varied.py 8077
//   scripts/wasm-viewer.sh 8011 8077
//   node scripts/wasm-orbit.js \
//     'http://127.0.0.1:8011/fcviewer.html?scene=http://127.0.0.1:8077&noidb&stream&membudget=8&cam=0.6,0.4,300,60,60,5,0,0'
//
// It prints released/no-room counts before and after the orbit separately,
// because that split is the assertion: releases should be ~0 while settling
// (an eviction is a decision under one camera, and only a move is new
// information -- the camera generation of phase 4b) and non-zero after.
// Both numbers equal across a refactor is a strong equivalence signal.
//
// PUPPETEER_PATH  a node_modules puppeteer install; LD_LIBRARY_PATH must
//       include the conda lib dir so Chromium finds libasound (see
//       wasm-shot.js).
//
// Headless Chromium renders on swiftshader: this validates the ladder's
// logic, not device-GPU precision.
const path = process.env.PUPPETEER_PATH || 'puppeteer';
const puppeteer = require(path);

(async () => {
  const url = process.argv[2];
  const settleMs = parseInt(process.argv[3] || '25000', 10);
  const steps = parseInt(process.argv[4] || '6', 10);
  if (!url) { console.error('usage: node wasm-orbit.js <url> [settle_ms] [steps]'); process.exit(1); }

  const browser = await puppeteer.launch({
    headless: 'new',
    args: ['--no-sandbox', '--enable-unsafe-swiftshader',
           '--use-angle=swiftshader', '--window-size=1100,900',
           '--disable-background-timer-throttling',
           '--disable-backgrounding-occluded-windows',
           '--disable-renderer-backgrounding'],
  });
  const page = await browser.newPage();
  await page.setViewport({ width: 1100, height: 900 });
  const lines = [];
  page.on('console', m => { const t = m.text(); lines.push(t); console.log(t); });

  await page.goto(url, { waitUntil: 'domcontentloaded' });
  await new Promise(r => setTimeout(r, settleMs));

  const settled = lines.length;
  console.log(`--- settled after ${settleMs}ms (${settled} lines), orbiting ---`);

  // Drag across the canvas: the viewer orbits on left-button drag.
  for (let i = 0; i < steps; ++i) {
    const y = 450;
    await page.mouse.move(250, y);
    await page.mouse.down();
    for (let x = 250; x <= 850; x += 60) {
      await page.mouse.move(x, y + (i % 2 ? 40 : -40));
      await new Promise(r => setTimeout(r, 30));
    }
    await page.mouse.up();
    await new Promise(r => setTimeout(r, 3000));
  }
  await new Promise(r => setTimeout(r, 5000));

  const after = lines.slice(settled);
  const count = (arr, re) => arr.filter(l => re.test(l)).length;
  console.log('=== ORBIT SUMMARY ===');
  console.log('released-after-orbit:', count(after, /released \d+ B of geometry/));
  console.log('noroom-after-orbit:  ', count(after, /no room for a chunk/));
  console.log('released-while-settling:', count(lines.slice(0, settled), /released \d+ B of geometry/));
  const last = after.filter(l => /scene at \d+ draws/.test(l)).pop();
  console.log('final:', last || '(none)');
  await browser.close();
})();
