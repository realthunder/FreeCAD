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
//
// Memory (7.20 C5): the page may call `await window.fcxMark(label)`, which
// answers the browser's processes as /proc sees them at that moment -- each
// one's type (browser, renderer, gpu-process, ...), RSS and PSS in MB.  The
// browser runs with --expose-gc so a page can collect before it marks.
const fs = require('fs');
const puppeteer = require(process.env.PUPPETEER_PATH || 'puppeteer-core');

/// The browser process and every descendant, with their memory.  Linux only;
/// elsewhere an empty list.
function processMemory(root) {
  const parent = new Map();
  let pids = [];
  try {
    pids = fs.readdirSync('/proc').filter((d) => /^\d+$/.test(d));
  } catch {
    return [];
  }
  for (const pid of pids) {
    try {
      const stat = fs.readFileSync('/proc/' + pid + '/stat', 'utf8');
      parent.set(+pid, +stat.slice(stat.lastIndexOf(')') + 2).split(' ')[1]);
    } catch {}
  }
  const tree = new Set([root]);
  for (let grew = true; grew;) {
    grew = false;
    for (const [pid, ppid] of parent)
      if (!tree.has(pid) && tree.has(ppid)) {
        tree.add(pid);
        grew = true;
      }
  }
  const out = [];
  for (const pid of tree) {
    try {
      // A child rewrites its argv as one space-separated string.
      const cmd = fs.readFileSync('/proc/' + pid + '/cmdline', 'utf8');
      const type = ((cmd.match(/--type=([\w-]+)/) || [])[1]) || 'browser';
      const roll = fs.readFileSync('/proc/' + pid + '/smaps_rollup', 'utf8');
      const kb = (key) => +((roll.match(new RegExp('^' + key + ':\\s+(\\d+)', 'm')) || [])[1] || 0);
      out.push({ pid, type, rssMB: kb('Rss') / 1024, pssMB: kb('Pss') / 1024 });
    } catch {}
  }
  return out;
}

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
    args: ['--no-sandbox', '--use-angle=swiftshader', '--enable-unsafe-swiftshader',
           '--js-flags=--expose-gc'],
    env,
  });
  let report = null;
  try {
    console.log('browser ' + await browser.version());
    const page = await browser.newPage();
    const root = browser.process() ? browser.process().pid : 0;
    await page.exposeFunction('fcxMark', (label) => {
      const procs = root ? processMemory(root) : [];
      console.log('mark ' + label + ' ' + JSON.stringify(procs));
      return procs;
    });
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
