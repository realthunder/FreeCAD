// Open a page headless and wait for the report it leaves on window
// (docs/Sandbox.md 7.20; tests/gui/sandbox-console-browser.py).
//
// Usage:  node scripts/console-drive.js <url> <window-var> [timeout_ms] [module]
//
// `module`, relative to the page, is loaded into it as a module script once
// the page is up -- how a gate drives a page that does not carry its own
// drive, the served viewer (tests/gui/sandbox-console-viewer-browser.py).
//
// Relays the page's console as it goes and prints the report last, as one
// line `REPORT <json>`; exits 0 when the report says ok, 1 when it does not,
// 2 when none arrived.  PUPPETEER_PATH names a puppeteer-core install,
// CHROME the browser.  CHROME_LIBS is prepended to the browser's
// LD_LIBRARY_PATH: a Chrome for Testing on a box without the system
// libasound finds one there (docs/Testing.md).
const puppeteer = require(process.env.PUPPETEER_PATH || 'puppeteer-core');

(async () => {
  const [url, variable, timeoutArg, inject] = process.argv.slice(2);
  if (!url || !variable) {
    console.error('usage: node console-drive.js <url> <window-var> [timeout_ms]');
    process.exit(2);
  }
  const timeout = +(timeoutArg || 180000);
  const env = Object.assign({}, process.env);
  if (process.env.CHROME_LIBS)
    env.LD_LIBRARY_PATH = process.env.CHROME_LIBS
      + (env.LD_LIBRARY_PATH ? ':' + env.LD_LIBRARY_PATH : '');
  const browser = await puppeteer.launch({
    executablePath: process.env.CHROME,
    headless: true,
    // WebGL through swiftshader, as edit-drive.js has it: the viewer page
    // needs a context, the sandbox pages do not mind one.
    args: ['--no-sandbox', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'],
    env,
  });
  let report = null;
  try {
    console.log('browser ' + await browser.version());
    const page = await browser.newPage();
    page.on('console', (m) => console.log('[page] ' + m.text()));
    page.on('pageerror', (e) => console.log('[pageerror] ' + e.message));
    page.on('requestfailed', (r) =>
      console.log('[requestfailed] ' + r.url() + ' ' + (r.failure() || {}).errorText));
    await page.goto(url);
    if (inject)
      await page.addScriptTag({ url: new URL(inject, url).href, type: 'module' });
    await page.waitForFunction('window.' + variable, { timeout });
    report = await page.evaluate('window.' + variable);
  } catch (e) {
    console.log('no report: ' + e.message);
  } finally {
    await browser.close();
  }
  console.log('REPORT ' + JSON.stringify(report));
  process.exit(report ? (report.ok ? 0 : 1) : 2);
})();
