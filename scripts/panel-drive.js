// Open the viewer page headless and report what the task-panel card drew
// (docs/Sandbox.md 7.22, G7, W1's rendering check).
//
// W1's replay gate proves the reduction -- frames in, a view plan out -- in
// node, with no DOM. What it cannot prove is that the plan reaches the
// screen: the leaves, the grid tracks, the write going back. That is the
// "hand-opened page against a live serving FreeCAD" the sizing ruled, and
// this is the hand, so the check is repeatable.
//
// Usage:  node scripts/panel-drive.js <url> [out.png] [timeout_ms] [type]
//   the url wants `?panel` (the card opens on load) and `?doc=<name>`:
//   http://127.0.0.1:8077/fcviewer.html?doc=TaskPanel&panel
//   `type` writes that value into the first enabled field and reports what
//   the host sent back, which is the write path end to end.
//
// Prints the card's shape as one line `REPORT <json>`: the title, the box
// headings, every field with its value, the buttons, and the counts. Exits
// 0 when a panel root rendered with at least one field, 1 when the card
// came up empty (the honest failure: "No task panel is open on the
// desktop." means the mirror answered but has nothing), 2 when the card
// never appeared at all.
//
// PUPPETEER_PATH names a puppeteer-core install, CHROME the browser, and
// CHROME_LIBS is prepended to its LD_LIBRARY_PATH -- the same three knobs
// console-drive.js takes, for the same reasons (docs/Testing.md).
const puppeteer = require(process.env.PUPPETEER_PATH || 'puppeteer-core');

/// What the card holds, read from the DOM rather than from the client's
/// own state: the point of this harness is that the two agree.
function readCard() {
  const card = document.querySelector('.fc-panel');
  if (!card) return null;
  const text = (el) => (el.textContent || '').trim();
  const empty = card.querySelector('.fc-panel-empty');
  return {
    title: text(card.querySelector('.fc-panel-title')),
    empty: empty ? text(empty) : '',
    heads: [...card.querySelectorAll('.fc-panel-box-head')].map(text),
    labels: [...card.querySelectorAll('.fc-panel-label')].map(text),
    fields: [...card.querySelectorAll('input.fc-panel-field')].map((el) => ({
      value: el.value, title: el.title || '', disabled: el.disabled,
    })),
    combos: [...card.querySelectorAll('select.fc-panel-field')].map((el) => ({
      value: el.value, options: [...el.options].map((o) => o.text),
    })),
    checks: [...card.querySelectorAll('.fc-panel-check')].map((el) => ({
      text: text(el), checked: !!el.querySelector('input')?.checked,
    })),
    buttons: [...card.querySelectorAll('.fc-panel-btn')].map(text),
    rows: [...card.querySelectorAll('.fc-panel-row')].map(text),
    pictures: card.querySelectorAll('.fc-panel-pic').length,
    // A grid that planned a NaN track would collapse; report the tracks so
    // the corpus's two-wide form rows are visible in the result.
    grids: [...card.querySelectorAll('.fc-panel-grid')].map(
      (el) => getComputedStyle(el).gridTemplateColumns),
    height: Math.round(card.getBoundingClientRect().height),
  };
}

(async () => {
  const [url, shot, timeoutArg, typed] = process.argv.slice(2);
  if (!url) {
    console.error('usage: node panel-drive.js <url> [out.png] [timeout_ms] [type]');
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
    // The viewer page wants a WebGL context even though this harness only
    // reads the chrome over it.
    args: ['--no-sandbox', '--window-size=1280,1000',
           '--use-angle=swiftshader', '--enable-unsafe-swiftshader'],
    env,
  });
  let card = null;
  try {
    console.log('browser ' + await browser.version());
    const page = await browser.newPage();
    await page.setViewport({ width: 1280, height: 1000 });
    page.on('console', (m) => console.log('[page] ' + m.text()));
    page.on('pageerror', (e) => console.log('[pageerror] ' + e.message));
    page.on('requestfailed', (r) =>
      console.log('[requestfailed] ' + r.url() + ' ' + (r.failure() || {}).errorText));
    await page.goto(url, { waitUntil: 'domcontentloaded' });
    // The card renders as soon as it is open; the panel inside it waits on
    // the subscribe reply and the snapshot that follows it. The wait is for
    // CONTENT, never for the empty message: the card says "Waiting for the
    // viewer..." from the moment it opens (the WASM module installs the
    // control uplink seconds into the load), so a wait that accepted that
    // would report the panel missing on every run.
    await page.waitForSelector('.fc-panel', { timeout });
    await page.waitForFunction(
      () => !!document.querySelector(
        '.fc-panel input, .fc-panel select, .fc-panel .fc-panel-row'),
      { timeout, polling: 250 }).catch(() => {});
    card = await page.evaluate(readCard);
    // The write half: type into the first enabled field and read the card
    // back. What comes back is the HOST's value -- either the echo of an
    // accepted write or the origin echo of a corrected one -- so a field
    // that still shows what was typed after the round trip is the check
    // passing, and one that shows something else is the host correcting.
    if (typed !== undefined && card && card.fields.length) {
      const before = await page.evaluate((value) => {
        const el = [...document.querySelectorAll('.fc-panel input.fc-panel-field')]
          .find((e) => !e.disabled);
        if (!el) return null;
        const was = el.value;
        el.focus();
        el.value = value;
        el.dispatchEvent(new Event('input', { bubbles: true }));
        el.dispatchEvent(new Event('change', { bubbles: true }));
        el.blur();
        return was;
      }, typed);
      await new Promise((r) => setTimeout(r, 2000));
      const after = await page.evaluate(() => {
        const el = [...document.querySelectorAll('.fc-panel input.fc-panel-field')]
          .find((e) => !e.disabled);
        return el ? el.value : null;
      });
      card.typed = { wrote: typed, before, after };
    }
    if (shot) {
      await page.screenshot({ path: shot });
      console.log('shot ' + shot);
    }
  } catch (e) {
    console.log('no card: ' + e.message);
  } finally {
    await browser.close();
  }
  console.log('REPORT ' + JSON.stringify(card));
  if (!card) process.exit(2);
  process.exit(card.fields.length || card.combos.length ? 0 : 1);
})();
