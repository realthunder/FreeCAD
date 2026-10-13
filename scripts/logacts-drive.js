// Drive a real browser viewer through what the transaction log's panel does
// with what it lists (docs/TransactionLog.md sec 32.22).
//
// tests/gui/serve-log-acts-browser.py serves the document and the page,
// which is opened with `?history` so that the card is up on load. What only
// a browser can show is read here: that a row chosen can be taken back and
// several at once, that a refusal is said with the host's reason, that a
// snapshot is asked a name and listed, that a branch is made, switched from
// and renamed from the card, that a version is restored -- and that the
// card follows the host each time, by itself.
//
// Usage:  node scripts/logacts-drive.js <url> <settle_ms>
// LOGACTS_PHASES and LOGACTS_RESULT name files (node buffers stdout to a
// pipe): the phases as they pass, and the readings as JSON. LOGACTS_SHOTS
// names a directory the pictures are written to.
const puppeteer = require(process.env.PUPPETEER_PATH || 'puppeteer-core');
const fs = require('fs');

const sleep = ms => new Promise(r => setTimeout(r, ms));

const phaseFile = process.env.LOGACTS_PHASES;
// A phase is when the serving side reads its own state, or acts: it is
// given the time to, before the page goes on.
async function phase(name) {
  console.log('PHASE ' + name);
  if (phaseFile) {
    try { fs.appendFileSync(phaseFile, name + '\n'); }
    catch (e) { console.error('phase file:', e.message); }
  }
  await sleep(900);
}

async function shot(page, label) {
  if (!process.env.LOGACTS_SHOTS)
    return;
  try { await page.screenshot({path: process.env.LOGACTS_SHOTS + '/' + label + '.png'}); }
  catch (e) { console.error('shot file:', e.message); }
}

const HELPERS = () => {
  // What a cell says, without the tags beside it (the row's kind, its
  // branch, "now"): its own text
  const first = (el) => (el ? [...el.childNodes].filter((n) => n.nodeType === 3)
    .map((n) => n.textContent).join('').trim() : '');
  window.__fcRows = () => [...document.querySelectorAll('.fc-log-rows:not(.fc-log-versions) tbody tr')]
    .map((tr) => ({seq: Number(tr.dataset.seq), what: first(tr.querySelectorAll('td')[1]),
                   sel: tr.classList.contains('fc-log-sel'),
                   away: tr.classList.contains('fc-log-away')}));
  window.__fcVersions = () => [...document.querySelectorAll('.fc-log-versions tbody tr')]
    .map((tr) => ({num: Number(tr.dataset.num),
                   name: first(tr.querySelectorAll('td')[1]),
                   now: !!tr.querySelector('.fc-log-kind'),
                   branch: tr.querySelectorAll('td')[2].innerText.trim()}));
  // A click on the row of that name, with the keys held
  window.__fcClickRow = (name, keys) => {
    const tr = [...document.querySelectorAll('.fc-log-rows:not(.fc-log-versions) tbody tr')]
      .find((r) => first(r.querySelectorAll('td')[1]) === name);
    if (tr) tr.dispatchEvent(new MouseEvent('click', Object.assign({bubbles: true}, keys || {})));
    return !!tr;
  };
  window.__fcAct = (name) => document.querySelector(`.fc-log [data-act="${name}"]`);
  window.__fcActSays = (name) => {
    const el = window.__fcAct(name);
    return el ? {text: (el.textContent || '').trim(), disabled: !!el.disabled} : null;
  };
  // (asked before the click: the act disables its button while it runs)
  window.__fcClickAct = (name) => {
    const el = window.__fcAct(name);
    const can = !!el && !el.disabled;
    if (can) el.click();
    return can;
  };
  window.__fcTab = (name) => {
    const el = document.querySelector(`.fc-log [data-tab="${name}"]`);
    if (el) el.click();
    return !!el;
  };
  window.__fcStatus = () => {
    const el = document.querySelector('.fc-log-status');
    return el ? {text: el.innerText.trim(), bad: el.classList.contains('fc-log-error')} : null;
  };
  window.__fcBranch = () => {
    const el = document.querySelector('.fc-log-branchsel');
    return el ? {value: el.value, options: [...el.options].map((o) => o.value)} : null;
  };
  window.__fcSwitch = (name) => {
    const el = document.querySelector('.fc-log-branchsel');
    if (!el) return false;
    el.value = name;
    el.dispatchEvent(new Event('change', {bubbles: true}));
    return true;
  };
};

async function waitFor(page, fn, ms, ...args) {
  try {
    await page.waitForFunction(fn, {timeout: ms}, ...args);
    return true;
  }
  catch (e) { return false; }
}

// What the card says once an act is answered: the status after `before`
async function said(page, ms) {
  await waitFor(page, () => !!window.__fcStatus(), ms);
  return page.evaluate(() => window.__fcStatus());
}

// The question an act asks first: answered with `text`, or with a yes alone
async function answer(page, text) {
  const asked = await waitFor(page, () => !!document.querySelector('.fc-log-ask'), 10000);
  if (!asked) return false;
  if (text !== undefined) {
    // Typed over what the question starts from
    await page.evaluate(() => {
      const el = document.querySelector('.fc-log-askinput');
      el.focus();
      el.select();
    });
    await page.keyboard.type(text);
  }
  await sleep(150);
  return page.evaluate(() => window.__fcClickAct('yes'));
}

(async () => {
  const url = process.argv[2];
  const settleMs = parseInt(process.argv[3] || '9000', 10);
  if (!url) {
    console.error('usage: node logacts-drive.js <url> [settle_ms]');
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
    page.on('pageerror', e => console.log('  PAGEERROR: ' + (e.stack || e.message)));
    await page.goto(url, {waitUntil: 'domcontentloaded', timeout: 60000});
    await page.evaluate(HELPERS);
    await sleep(settleMs);

    out.card = await waitFor(page, () => !!document.querySelector('.fc-log'), 15000);
    out.listed = await waitFor(page, () => window.__fcRows().length > 0, 30000);
    out.branchListed = await waitFor(page, () => !!window.__fcBranch(), 15000);
    out.rows = await page.evaluate(() => window.__fcRows());
    out.branch = await page.evaluate(() => window.__fcBranch());
    const first = out.branch ? out.branch.value : '';
    out.noActs = await page.evaluate(() => !document.querySelector('.fc-log-rowacts'));
    out.noDelete = await page.evaluate(() => !window.__fcAct('delete-branch'));
    await phase('listed');

    // One row taken back
    out.picked = await page.evaluate(() => window.__fcClickRow('wider'));
    await sleep(300);
    out.undoSays = await page.evaluate(() => window.__fcActSays('undo'));
    out.undoAsked = await page.evaluate(() => window.__fcClickAct('undo'));
    out.undoSaid = await said(page, 20000);
    out.undoListed = await waitFor(
      page, () => window.__fcRows().length && window.__fcRows()[0].what === 'Undo wider', 15000);
    await phase('undone');

    // Two at once: the second with the key held
    await page.evaluate(() => window.__fcClickRow('deeper'));
    await page.evaluate(() => window.__fcClickRow('taller', {ctrlKey: true}));
    await sleep(300);
    out.twoPicked = await page.evaluate(
      () => window.__fcRows().filter((r) => r.sel).map((r) => r.what));
    out.twoSays = await page.evaluate(() => window.__fcActSays('undo'));
    out.branchHereSays = await page.evaluate(() => window.__fcActSays('branch-here'));
    out.twoAsked = await page.evaluate(() => window.__fcClickAct('undo'));
    await waitFor(page, () => (window.__fcStatus() || {}).text === '2 rows undone', 20000);
    out.twoSaid = await page.evaluate(() => window.__fcStatus());
    await phase('undone2');

    // One the host refuses: taken back already
    await page.evaluate(() => window.__fcClickRow('wider'));
    await sleep(300);
    out.refusedAsked = await page.evaluate(() => window.__fcClickAct('undo'));
    await waitFor(page, () => (window.__fcStatus() || {}).bad === true, 20000);
    out.refusedSaid = await page.evaluate(() => window.__fcStatus());
    await phase('refused');

    // A version, asked a name
    out.snapAsked = await page.evaluate(() => window.__fcClickAct('snapshot'));
    out.snapAnswered = await answer(page, 'kept');
    await waitFor(page, () => /kept/.test((window.__fcStatus() || {}).text || ''), 20000);
    out.snapSaid = await page.evaluate(() => window.__fcStatus());
    await page.evaluate(() => window.__fcTab('versions'));
    out.versionsListed = await waitFor(
      page, () => window.__fcVersions().some((v) => v.name === 'kept'), 15000);
    out.versions = await page.evaluate(() => window.__fcVersions());
    // (chosen for the picture: its acts under the list)
    await page.evaluate(() => {
      const tr = document.querySelector('.fc-log-versions tbody tr');
      if (tr) tr.click();
    });
    await sleep(300);
    await shot(page, 'versions');
    await page.evaluate(() => window.__fcTab('rows'));
    await phase('snapshot');

    // A branch from here: no name is not one, a name is
    out.branchAsked = await page.evaluate(() => window.__fcClickAct('new-branch'));
    await waitFor(page, () => !!document.querySelector('.fc-log-ask'), 10000);
    out.branchNoName = await page.evaluate(() => window.__fcActSays('yes'));
    out.branchAnswered = await answer(page, 'try');
    await waitFor(page, () => (window.__fcBranch() || {}).value === 'try', 20000);
    out.branchSaid = await page.evaluate(() => window.__fcStatus());
    out.onTry = await page.evaluate(() => window.__fcBranch());
    // The host commits a row on it at this phase
    await phase('branched');
    out.tryRow = await waitFor(
      page, () => window.__fcRows().some((r) => r.what === 'on try'), 15000);

    // Back to the first
    out.switched = await page.evaluate((name) => window.__fcSwitch(name), first);
    await waitFor(page, (name) => (window.__fcBranch() || {}).value === name
                                  && /On branch/.test((window.__fcStatus() || {}).text || ''),
                  20000, first);
    out.switchSaid = await page.evaluate(() => window.__fcStatus());
    out.onFirst = await page.evaluate(() => window.__fcBranch());
    await waitFor(page, () => window.__fcRows().some((r) => r.what === 'on try' && r.away),
                  15000);
    out.rowsSwitched = await page.evaluate(() => window.__fcRows());
    await page.evaluate(() => window.__fcClickRow('on try'));
    await sleep(300);
    out.awaySays = await page.evaluate(() => window.__fcActSays('undo'));
    await shot(page, 'acts');
    // The host changes the document at this phase
    await phase('switched');
    await waitFor(page, () => window.__fcRows().some((r) => r.what === 'taller still'), 15000);

    // Restored to the version kept
    await page.evaluate(() => window.__fcTab('versions'));
    await waitFor(page, () => window.__fcVersions().some((v) => v.name === 'kept'), 15000);
    out.versionsChanged = await page.evaluate(() => window.__fcVersions());
    out.versionPicked = await page.evaluate(() => {
      const kept = window.__fcVersions().find((v) => v.name === 'kept');
      const tr = kept && document.querySelector(
        `.fc-log-versions tbody tr[data-num="${kept.num}"]`);
      if (tr) tr.click();
      return !!tr;
    });
    await sleep(300);
    out.versionActs = await page.evaluate(
      () => ['restore', 'name-version', 'unname-version', 'branch-version']
        .map((a) => (window.__fcActSays(a) || {}).text || null));
    out.restoreAsked = await page.evaluate(() => window.__fcClickAct('restore'));
    await waitFor(page, () => /Restored/.test((window.__fcStatus() || {}).text || ''), 30000);
    out.restoreSaid = await page.evaluate(() => window.__fcStatus());
    await page.evaluate(() => window.__fcTab('rows'));
    await phase('restored');

    // The branch renamed
    out.renameAsked = await page.evaluate(() => window.__fcClickAct('rename-branch'));
    await waitFor(page, () => !!document.querySelector('.fc-log-askinput'), 10000);
    out.renameStarts = await page.evaluate(
      () => (document.querySelector('.fc-log-askinput') || {}).value);
    out.renameAnswered = await answer(page, 'trunk');
    await waitFor(page, () => (window.__fcBranch() || {}).value === 'trunk', 20000);
    out.renamed = await page.evaluate(() => window.__fcBranch());
    out.renameSaid = await page.evaluate(() => window.__fcStatus());
    await phase('renamed');
  }
  catch (e) {
    out.failure = String((e && e.stack) || e);
    console.log('drive failed: ' + out.failure);
  }
  if (process.env.LOGACTS_RESULT) {
    try { fs.writeFileSync(process.env.LOGACTS_RESULT, JSON.stringify(out)); }
    catch (e) { console.error('result file:', e.message); }
  }
  console.log(JSON.stringify(out));
  await phase('done');
  await browser.close();
})();
