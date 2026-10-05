// Open the viewer page headless and send a file as a request through its
// menu (docs/TransactionLog.md sec 30.23): the launcher's "Send my copy to
// merge..." row, the browser's own file chooser given a local file, and
// what the page then tells its user.
//
// Usage:  node scripts/request-drive.js <url> <file> [timeout_ms]
//   the url wants `?doc=<name>`, and a connection that may edit -- under a
//   shared token that is `&token=<secret>&client=<name>` with the serve
//   started as FC_SERVE_TOKEN=<secret> FC_SERVE_INVITE=<name>:
//   http://127.0.0.1:8077/fcviewer.html?doc=Ours&token=s3cret&client=lei
//
// Prints one line `REPORT <json>`: whether the row was there, and the text
// of the alert the page raised. Exits 0 when the alert says the file was
// sent, 1 when it says anything else, 2 when the row or the alert never
// appeared -- a view-only connection has no such row, which is the point of
// running it once as someone who may not write.
//
// What this proves is the browser half: the row, the chooser, the bytes on
// the control lane and the host's answer. That the host KEPT the file and
// did not read it is the host's to say (Gui.serveRequests(), the log
// panel), and scripts/transaction-log-request-check.py checks that half.
//
// PUPPETEER_PATH names a puppeteer-core install, CHROME the browser, and
// CHROME_LIBS is prepended to its LD_LIBRARY_PATH -- the same three knobs
// as scripts/panel-drive.js.
const puppeteer = require(process.env.PUPPETEER_PATH || 'puppeteer-core');

(async () => {
  const [url, file, timeoutArg] = process.argv.slice(2);
  if (!url || !file) {
    console.error('usage: node request-drive.js <url> <file> [timeout_ms]');
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
    args: ['--no-sandbox', '--window-size=1280,1000',
           '--use-angle=swiftshader', '--enable-unsafe-swiftshader'],
    env,
  });
  const report = { row: false, rows: [], alert: null };
  let code = 2;
  try {
    const page = await browser.newPage();
    await page.setViewport({ width: 1280, height: 1000 });
    page.on('pageerror', (e) => console.log('[pageerror] ' + e.message));
    const said = new Promise((resolve) => {
      page.on('dialog', async (dialog) => {
        const text = dialog.message();
        await dialog.accept();
        resolve(text);
      });
    });
    await page.goto(url, { waitUntil: 'domcontentloaded', timeout });
    // The first launcher is the viewer menu; the second is the selection
    // menu, which has no such row.
    await page.waitForSelector('.fc-launch', { timeout });
    // The row is there only for a connection that may write, and the page
    // learns what it may do from the host's answer to its hello: until
    // then it draws the row. Give the answer time to arrive before reading
    // the menu, or a view-only page is reported as having one.
    await new Promise((r) => setTimeout(r, +(process.env.FC_REQUEST_SETTLE || 4000)));
    const rows = async () => {
      if (!(await page.$('.fc-menu'))) await page.click('.fc-launch');
      await page.waitForSelector('.fc-menu .fc-menu-item', { timeout });
      return page.evaluate(() =>
        [...document.querySelectorAll('.fc-menu .fc-menu-item')].map((el) => {
          const box = el.getBoundingClientRect();
          return { text: (el.textContent || '').trim(),
                   x: box.left + box.width / 2, y: box.top + box.height / 2 };
        }));
    };
    const found = await rows();
    report.rows = found.map((r) => r.text);
    const row = found.find((r) => r.text.startsWith('Send my copy'));
    report.row = !!row;
    if (row) {
      // A real click, by where the row is: a browser opens a file chooser
      // only on its user's own gesture, and the menu redraws its rows, so a
      // handle to one may be of a row that is gone.
      const [chooser] = await Promise.all([
        page.waitForFileChooser({ timeout }),
        page.mouse.click(row.x, row.y),
      ]);
      await chooser.accept([file]);
      report.alert = await Promise.race([
        said,
        new Promise((r) => setTimeout(() => r(null), timeout)),
      ]);
      if (report.alert !== null)
        code = report.alert.startsWith('Sent ') ? 0 : 1;
    }
  } catch (e) {
    console.log('[drive] ' + e.message);
  } finally {
    console.log('REPORT ' + JSON.stringify(report));
    await browser.close();
  }
  process.exit(code);
})();
