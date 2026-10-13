// Drive a real browser viewer through the transaction log's panel
// (docs/TransactionLog.md sec 32.21).
//
// tests/gui/serve-log-browser.py serves the document and the page, which is
// opened with `?history` so that the card is up on load. What only a browser
// can show is read here: that the card lists the rows, that a row the host
// commits meanwhile comes into it by itself, the ops of a row chosen and a
// value of one of those, that "Show" puts the page in the edit of that row's
// change with the host's panel of it up and its two states drawn, that the
// panel has the host's own Close, and that Close leaves.
//
// Usage:  node scripts/logpanel-drive.js <url> <settle_ms>
// LOGPANEL_PHASES and LOGPANEL_RESULT name files (node buffers stdout to a
// pipe): the phases as they pass, and the readings as JSON. LOGPANEL_SHOTS
// names a directory the pictures are written to.
const puppeteer = require(process.env.PUPPETEER_PATH || 'puppeteer-core');
const fs = require('fs');

const sleep = ms => new Promise(r => setTimeout(r, ms));

const phaseFile = process.env.LOGPANEL_PHASES;
// A phase is when the serving side reads its own state, or acts: it is
// given the time to, before the page goes on.
async function phase(name) {
  console.log('PHASE ' + name);
  if (phaseFile) {
    try { fs.appendFileSync(phaseFile, name + '\n'); }
    catch (e) { console.error('phase file:', e.message); }
  }
  await sleep(700);
}

async function shot(page, label) {
  if (!process.env.LOGPANEL_SHOTS)
    return;
  try { await page.screenshot({path: process.env.LOGPANEL_SHOTS + '/' + label + '.png'}); }
  catch (e) { console.error('shot file:', e.message); }
}

const HELPERS = () => {
  // Pixels of the colour a changed face is drawn in: strong red and blue,
  // little green. Decoded in the page, which has the image decoder.
  window.__fcMagenta = async (b64) => {
    const img = new Image();
    img.src = 'data:image/png;base64,' + b64;
    await img.decode();
    const c = document.createElement('canvas');
    c.width = img.width;
    c.height = img.height;
    const g = c.getContext('2d', {willReadFrequently: true});
    g.drawImage(img, 0, 0);
    const d = g.getImageData(0, 0, c.width, c.height).data;
    let n = 0;
    for (let i = 0; i < d.length; i += 4) {
      const r = d[i], gr = d[i + 1], bl = d[i + 2];
      if (r > 150 && bl > 150 && gr < 0.6 * Math.min(r, bl))
        ++n;
    }
    return n;
  };
  window.__fcLogRows = () => [...document.querySelectorAll('.fc-log-rows tbody tr')].map(
    (tr) => {
      const td = [...tr.querySelectorAll('td')].map((c) => c.innerText.trim());
      return {seq: Number(tr.dataset.seq), what: td[1], who: td[2], when: td[3],
              record: tr.classList.contains('fc-log-record'),
              away: tr.classList.contains('fc-log-away')};
    });
  window.__fcLogOps = () => [...document.querySelectorAll('.fc-log-op')].map(
    (el) => ({idx: Number(el.dataset.idx),
              text: el.querySelector('.fc-log-optext').innerText.trim(),
              show: !!el.querySelector('.fc-log-show')}));
  window.__fcLogClickRow = (name) => {
    const tr = [...document.querySelectorAll('.fc-log-rows tbody tr')].find(
      (r) => r.querySelectorAll('td')[1].innerText.trim().split('\n')[0].trim() === name);
    if (tr) tr.click();
    return !!tr;
  };
  window.__fcLogClickOp = (text, button) => {
    const el = [...document.querySelectorAll('.fc-log-op')].find(
      (o) => o.querySelector('.fc-log-optext').innerText.trim() === text);
    if (!el) return false;
    if (!button) { el.click(); return true; }
    const b = el.querySelector('.fc-log-show');
    if (b) b.click();
    return !!b;
  };
  window.__fcPanelButtons = () => [...document.querySelectorAll(
    '.fc-panel-buttons .fc-panel-btn')].map((b) => (b.textContent || '').trim());
};

// The page as it is seen: the cards over the canvas are dark, and no part
// of what is counted (a canvas of WebGL read back by itself is empty)
async function magenta(page) {
  const b64 = await page.screenshot({encoding: 'base64'});
  return page.evaluate(d => window.__fcMagenta(d), b64);
}

async function waitFor(page, fn, ms, ...args) {
  try {
    await page.waitForFunction(fn, {timeout: ms}, ...args);
    return true;
  }
  catch (e) { return false; }
}

(async () => {
  const url = process.argv[2];
  const settleMs = parseInt(process.argv[3] || '9000', 10);
  if (!url) {
    console.error('usage: node logpanel-drive.js <url> [settle_ms]');
    process.exit(2);
  }
  const browser = await puppeteer.launch({
    executablePath: process.env.CHROME,
    headless: true,
    protocolTimeout: 180000,
    args: ['--no-sandbox', '--enable-unsafe-swiftshader',
           '--use-angle=swiftshader', '--window-size=1100,900',
           '--disable-background-timer-throttling',
           '--disable-backgrounding-occluded-windows',
           '--disable-renderer-backgrounding'],
  });
  const out = {};
  try {
    const page = await browser.newPage();
    page.setDefaultTimeout(60000);
    await page.setViewport({width: 1100, height: 900});
    page.on('console', m => {
      const t = m.text();
      if (/edit mode|edit refused|PAGEERROR/i.test(t))
        console.log('  page: ' + t);
    });
    page.on('pageerror', e => console.log('  PAGEERROR: ' + (e.stack || e.message)));
    await page.goto(url, {waitUntil: 'domcontentloaded', timeout: 60000});
    await page.evaluate(HELPERS);
    await sleep(settleMs);

    out.card = await waitFor(page, () => !!document.querySelector('.fc-log'), 15000);
    out.listedInTime = await waitFor(
      page, () => document.querySelectorAll('.fc-log-rows tbody tr').length > 0, 30000);
    out.rows = await page.evaluate(() => window.__fcLogRows());
    await waitFor(page, () => !!document.querySelector('.fc-log-branchsel'), 15000);
    out.branch = await page.evaluate(
      () => (document.querySelector('.fc-log-branchsel') || {}).value || null);
    out.error = await page.evaluate(
      () => (document.querySelector('.fc-log-error') || {}).innerText || null);
    // The host commits a row at this phase
    await phase('listed');
    out.pushed = await waitFor(
      page, () => window.__fcLogRows().some((r) => r.what.split('\n')[0].trim() === 'deeper'),
      15000);
    out.rowsPushed = await page.evaluate(() => window.__fcLogRows());

    // The rows that changed nothing are left out until asked for
    await page.evaluate(() => document.querySelector('.fc-log-records input').click());
    await sleep(400);
    out.rowsWithRecords = await page.evaluate(() => window.__fcLogRows());
    await page.evaluate(() => document.querySelector('.fc-log-records input').click());
    await sleep(300);
    await phase('pushed');

    out.chose = await page.evaluate(() => window.__fcLogClickRow('taller'));
    out.opsInTime = await waitFor(
      page, () => document.querySelectorAll('.fc-log-op').length > 0, 15000);
    out.ops = await page.evaluate(() => window.__fcLogOps());

    out.choseOp = await page.evaluate(() => window.__fcLogClickOp('Box: Height', false));
    out.valueInTime = await waitFor(
      page, () => !!document.querySelector('pre.fc-log-value'), 15000);
    out.valueAfter = await page.evaluate(
      () => (document.querySelector('pre.fc-log-value') || {}).innerText || null);
    out.sides = await page.evaluate(
      () => [...document.querySelectorAll('.fc-log-side')].map(
        (b) => [b.innerText.trim(), b.classList.contains('fc-log-sel')]));
    await page.evaluate(() => {
      const b = [...document.querySelectorAll('.fc-log-side')].find(
        (s) => s.innerText.trim() === 'Before');
      if (b) b.click();
    });
    await sleep(1500);
    out.valueBefore = await page.evaluate(
      () => (document.querySelector('pre.fc-log-value') || {}).innerText || null);
    await shot(page, 'log');

    // A row's change, shown as the edit it is
    out.magentaBefore = await magenta(page);
    out.panelBefore = await page.evaluate(
      () => document.querySelectorAll('input.fc-panel-slider').length);
    out.asked = await page.evaluate(() => window.__fcLogClickOp('Cut: Shape', true));
    out.entered = await waitFor(page, () => window.fcviewerEditing === 'Cut', 20000);
    out.panel = await waitFor(
      page, () => !!document.querySelector('input.fc-panel-slider'), 15000);
    out.panelText = await page.evaluate(() => {
      const s = document.querySelector('input.fc-panel-slider');
      let p = s;
      while (p && !(p.classList && p.classList.contains('fc-panel-box')))
        p = p.parentElement;
      return p ? p.innerText : null;
    });
    out.buttons = await page.evaluate(() => window.__fcPanelButtons());
    const t0 = Date.now();
    out.magentaIn = await magenta(page);
    while (out.magentaIn < 20000 && Date.now() - t0 < 15000) {
      await sleep(250);
      out.magentaIn = await magenta(page);
    }
    await sleep(1500);
    await shot(page, 'shown');
    await phase('shown');

    out.closed = await page.evaluate(() => {
      const b = [...document.querySelectorAll('.fc-panel-buttons .fc-panel-btn')].find(
        (x) => (x.textContent || '').trim() === 'Close');
      if (b) b.click();
      return !!b;
    });
    out.left = await waitFor(page, () => !window.fcviewerEditing, 20000);
    await sleep(1500);
    out.panelAfter = await page.evaluate(
      () => document.querySelectorAll('input.fc-panel-slider').length);
    out.cardAfter = await page.evaluate(() => !!document.querySelector('.fc-log'));
    await phase('left');
  }
  catch (e) {
    out.failure = String((e && e.stack) || e);
    console.log('drive failed: ' + out.failure);
  }
  if (process.env.LOGPANEL_RESULT) {
    try { fs.writeFileSync(process.env.LOGPANEL_RESULT, JSON.stringify(out)); }
    catch (e) { console.error('result file:', e.message); }
  }
  console.log(JSON.stringify(out));
  await phase('done');
  await browser.close();
})();
