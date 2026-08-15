// Launch Chrome for WASM-viewer testing, in either of two tiers:
//
//   real     headful window on the WSLg desktop, REAL GPU (WebGL2 on
//            ANGLE-over-D3D12 — Mesa d3d12 gallium under /dev/dxg), real
//            compositor at ~60Hz vsync. The only browser tier that
//            reproduces frame-pacing and GPU-state bugs (headless
//            swiftshader has masked both kinds); it opens a window on the
//            user's screen, so agents must announce it first.
//   headless swiftshader, no display needed — layout/logic checks and
//            backend-side load generation, same as the other wasm-*.js.
//
// The real tier needs all three of: libasound on LD_LIBRARY_PATH (the
// conda lib dir has it — without it the bundled Chrome dies, and with a
// partial env it can come up composing 0 frames), the d3d12 Mesa env, and
// the ANGLE flags. Wayland-ozone composites but never gets WebGL2; X11 is
// the working path. Verify what you actually got with `probe`:
//
//   node scripts/wasm-chrome.js probe            # want: ANGLE (... D3D12 ...)
//   node scripts/wasm-chrome.js probe --headless # swiftshader baseline
//
// Drive the standard convergence pattern against a served scene (settle,
// wheel-zoom burst to force exact-mesh retessellation, hold, then a
// stationary window that must stay quiet — the busy-loop regression
// check) and screenshot:
//
//   LD_LIBRARY_PATH=.conda/freecad/lib node scripts/wasm-chrome.js \
//     drive 'http://127.0.0.1:8000/fcviewer.html?scene=http://127.0.0.1:8077&hud' \
//     out.png
//
// Or require it from another harness and just get a correctly-launched
// browser:  const {launch} = require('./wasm-chrome'); const b = await
// launch({headless: false});
//
// puppeteer: resolved via require, or set PUPPETEER_PATH to a
// node_modules puppeteer install. CHROME overrides the executable.
const ppPath = process.env.PUPPETEER_PATH || 'puppeteer';
const puppeteer = require(ppPath);

async function launch(opts = {}) {
  const headless = opts.headless ?? !!process.env.HEADLESS;
  const args = ['--no-sandbox', '--window-size=1100,900',
                ...(opts.args || [])];
  const env = {...process.env};
  if (headless) {
    args.push('--enable-unsafe-swiftshader', '--use-angle=swiftshader');
  }
  else {
    // Real GPU: X11 on the WSLg desktop + ANGLE on native GL, with Mesa
    // steered to the d3d12 driver. Without --ignore-gpu-blocklist there
    // is no WebGL2 at all; without --use-angle=gl it lands on llvmpipe.
    args.push('--ignore-gpu-blocklist', '--use-gl=angle', '--use-angle=gl',
              '--disable-background-timer-throttling',
              '--disable-backgrounding-occluded-windows',
              '--disable-renderer-backgrounding');
    env.DISPLAY = env.DISPLAY || ':0';
    env.LIBGL_ALWAYS_SOFTWARE = '0';
    // The d3d12 steering is WSLg's, and /dev/dxg is what makes a host
    // WSLg. On a machine with a GPU of its own, pointing Mesa at d3d12
    // takes GL away rather than giving it -- so ask, rather than assume
    // the box this harness was written on.
    if (require('fs').existsSync('/dev/dxg')) {
      env.GALLIUM_DRIVER = 'd3d12';
      env.MESA_LOADER_DRIVER_OVERRIDE = 'd3d12';
    }
    delete env.WAYLAND_DISPLAY;   // ozone-wayland composites without WebGL2
  }
  return puppeteer.launch({
    headless: headless ? 'new' : false,
    executablePath: process.env.CHROME || undefined,
    args, env,
    protocolTimeout: 120000,
  });
}

/// 3s of requestAnimationFrame against a WebGL2 canvas: reports fps and
/// the unmasked renderer string, or that rAF never fired at all.
async function probe(headless) {
  const browser = await launch({headless});
  const page = await browser.newPage();
  await page.setContent('<canvas id="c" width="256" height="256"></canvas>');
  const r = await page.evaluate(() => new Promise(resolve => {
    const gl = document.getElementById('c').getContext('webgl2');
    let frames = 0;
    const t0 = performance.now();
    (function tick() {
      frames++;
      if (gl) { gl.clearColor(Math.random(), 0, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT); }
      if (performance.now() - t0 < 3000) requestAnimationFrame(tick);
      else resolve({frames, ms: performance.now() - t0, webgl2: !!gl,
                    renderer: gl ? gl.getParameter(
                      gl.getExtension('WEBGL_debug_renderer_info')
                        ?.UNMASKED_RENDERER_WEBGL || gl.RENDERER) : 'none'});
    })();
    setTimeout(() => resolve({frames, ms: 3500, webgl2: !!gl,
                              renderer: 'TIMEOUT (rAF never fired)'}), 3500);
  }));
  console.log(`frames=${r.frames} (${(r.frames / (r.ms / 1000)).toFixed(1)} fps)`
              + ` webgl2=${r.webgl2} renderer=${r.renderer}`);
  await browser.close();
}

/// Settle, zoom-burst, hold, stationary-quiet check, screenshot.
async function drive(headless, url, shot) {
  const browser = await launch({headless});
  const page = await browser.newPage();
  await page.setViewport({width: 1024, height: 720});
  const lines = [];
  page.on('console', m => lines.push(m.text()));
  page.on('pageerror', e => lines.push('PAGEERROR: ' + e.message));
  await page.goto(url, {waitUntil: 'load', timeout: 60000});
  const sleep = ms => new Promise(r => setTimeout(r, ms));

  await sleep(25000);                              // converge + settle
  const settled = lines.length;
  await page.mouse.move(512, 360);                 // exact rungs on demand
  for (let i = 0; i < 12; ++i) { await page.mouse.wheel({deltaY: -240}); await sleep(150); }
  await sleep(20000);                              // retess answers land
  const zoomed = lines.length;
  await sleep(15000);                              // must stay quiet
  const quiet = lines.length - zoomed;

  if (shot) await page.screenshot({path: shot});
  const errs = lines.filter(l => /PAGEERROR|exception/i.test(l));
  console.log(`console lines: settle=${settled} zoom+hold=${zoomed - settled}`
              + ` stationary15s=${quiet} errors=${errs.length}`);
  errs.slice(0, 10).forEach(l => console.log('  ' + l.slice(0, 200)));
  lines.slice(-6).forEach(l => console.log('  ' + l.slice(0, 160)));
  await browser.close();
  // Quiet means quiet only where frames pace for real; headless paces
  // slower than the settle threshold and stays legitimately chatty.
  if (!headless && quiet > 20) process.exit(2);
  if (errs.length) process.exit(1);
}

/// Open a page, touch nothing, and print every console line it produced.
///
/// `drive` counts lines because it is asking whether the viewer went
/// quiet; this prints them because the question is what they SAY. The
/// viewer narrates through the browser half of FC_RENDER_MSG, so with
/// ?leveldebug the renderer's own gate and level reporting lands here --
/// which is the only way to read it on this tier, the desktop's console
/// and --log-file being Gui-side.
async function watch(headless, url, seconds, shot) {
  const browser = await launch({headless});
  const page = await browser.newPage();
  await page.setViewport({width: 1024, height: 720});
  const lines = [];
  page.on('console', m => lines.push(m.text()));
  page.on('pageerror', e => lines.push('PAGEERROR: ' + e.message));
  await page.goto(url, {waitUntil: 'load', timeout: 60000});
  await new Promise(r => setTimeout(r, seconds * 1000));
  if (shot) await page.screenshot({path: shot});
  await browser.close();
  lines.forEach(l => console.log(l));
  console.log(`--- ${lines.length} console lines over ${seconds}s`);
}

module.exports = {launch, probe, drive, watch};

if (require.main === module) {
  const argv = process.argv.slice(2);
  const headless = argv.includes('--headless') || !!process.env.HEADLESS;
  const pos = argv.filter(a => !a.startsWith('--'));
  const cmd = pos[0];
  (async () => {
    if (cmd === 'probe') await probe(headless);
    else if (cmd === 'drive' && pos[1]) await drive(headless, pos[1], pos[2]);
    else if (cmd === 'watch' && pos[1])
      await watch(headless, pos[1], Number(pos[2] || 30), pos[3]);
    else {
      console.error('usage: node wasm-chrome.js probe [--headless]\n'
                    + '       node wasm-chrome.js drive <url> [out.png] [--headless]\n'
                    + '       node wasm-chrome.js watch <url> [seconds] [out.png] [--headless]');
      process.exit(1);
    }
  })().catch(e => { console.error('FAIL:', e.message); process.exit(1); });
}
