// Open the viewer page headless in a browser profile that is kept, and say
// which browser the page says it is and what the door let it do
// (docs/TransactionLog.md sec 30.32): the id a browser makes once and keeps
// is half of who a connection is where a grant counts the browsers it is
// for.
//
// Usage:  node scripts/browser-id-drive.js <url> <profile dir> [timeout_ms]
//   the url wants `?doc=<name>&token=<the grant's token>`; the profile
//   directory is the browser: the same one again is the same browser, and
//   another is another.
//
// Prints one line `REPORT <json>`: the id the page keeps (`device`, its
// first 8 characters only), whether it had one before this visit
// (`kept`), and the access the host answered with (`access`: `edit`,
// `view`, or null when the door refused the hello). Exits 0 when the page
// was let in, 1 when it was refused.
//
// PUPPETEER_PATH names a puppeteer-core install, CHROME the browser, and
// CHROME_LIBS is prepended to its LD_LIBRARY_PATH -- the same three knobs
// as scripts/request-drive.js.
const puppeteer = require(process.env.PUPPETEER_PATH || 'puppeteer-core');

(async () => {
  const [url, profile, timeoutArg] = process.argv.slice(2);
  if (!url || !profile) {
    console.error('usage: node browser-id-drive.js <url> <profile dir> [timeout_ms]');
    process.exit(2);
  }
  const timeout = +(timeoutArg || 60000);
  const env = Object.assign({}, process.env);
  if (process.env.CHROME_LIBS)
    env.LD_LIBRARY_PATH = process.env.CHROME_LIBS
      + (env.LD_LIBRARY_PATH ? ':' + env.LD_LIBRARY_PATH : '');
  const browser = await puppeteer.launch({
    executablePath: process.env.CHROME,
    headless: true,
    userDataDir: profile,
    args: ['--no-sandbox', '--window-size=1280,1000',
           '--use-angle=swiftshader', '--enable-unsafe-swiftshader'],
    env,
  });
  const report = { device: null, kept: false, access: null, refused: false };
  try {
    const page = await browser.newPage();
    await page.setViewport({ width: 1280, height: 1000 });
    const lines = [];
    page.on('console', (m) => lines.push(m.text()));
    page.on('pageerror', (e) => console.log('[pageerror] ' + e.message));
    // What the browser held before the page ran is whether it is one the
    // host has met: read it from the origin before the viewer loads.
    const origin = new URL(url).origin;
    await page.goto(origin + '/fcviewer.stamp', { waitUntil: 'domcontentloaded', timeout })
      .catch(() => {});
    report.kept = await page.evaluate(() => {
      try { return !!window.localStorage.getItem('fcviewer.device'); }
      catch (e) { return false; }
    });
    await page.goto(url, { waitUntil: 'domcontentloaded', timeout });
    await page.waitForSelector('.fc-launch', { timeout });
    await new Promise((r) => setTimeout(r, +(process.env.FC_ID_SETTLE || 6000)));
    const seen = await page.evaluate(() => {
      let id = null;
      try { id = window.localStorage.getItem('fcviewer.device'); } catch (e) {}
      return { id, access: window.fcviewerAccess || null };
    });
    report.device = seen.id ? seen.id.slice(0, 8) : null;
    report.refused = lines.some((l) => /Refused|refused/.test(l));
    report.access = report.refused ? null : (seen.access || 'edit');
  } catch (e) {
    console.log('[drive] ' + e.message);
  } finally {
    console.log('REPORT ' + JSON.stringify(report));
    await browser.close();
  }
  process.exit(report.access ? 0 : 1);
})();
