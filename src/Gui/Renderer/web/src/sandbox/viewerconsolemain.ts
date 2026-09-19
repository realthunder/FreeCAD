// The console in the real viewer page (docs/Sandbox.md 7.20, C4): injected by
// scripts/console-drive.js into the served fcviewer page opened with
// ?console, it drives the panel main.tsx mounted and leaves its verdict on
// window.fcxConsoleViewer (tests/gui/sandbox-console-viewer-browser.py).
//
// What only this page can show: the console's bridge rides the viewer's own
// scene socket, so the console IS the viewer's connection -- the owner's
// view-only switch for the viewer holds for its console, and the viewer's
// document switch moves the console with it.  The test on the desktop side
// is asked for what it must do through this connection's roster label
// (window.fcviewerSetClient), which a view-only connection can still set.

import { booted, enter, makeReport, output, until } from './consoledrive';

const { report, check } = makeReport();
const params = new URLSearchParams(location.search);
const doc1 = params.get('doc') ?? '';
const doc2 = params.get('doc2') ?? '';

/// Ask the desktop side for something, and wait until it is done.
async function ask(what: string, done: () => boolean) {
  window.fcviewerSetClient?.('fcx-ask:' + what);
  await until('the desktop to ' + what, done, 20000);
}

(async () => {
  const t0 = performance.now();
  try {
    await until('the console panel', () => !!document.querySelector('.fc-console'), 60000);
    await booted();
    report.bootMs = Math.round(performance.now() - t0);
    check("the console rides the viewer's connection", /on the viewer's connection/.test(output()),
          output());

    let got = await enter('App.ActiveDocument.Name');
    check('an expression reads the served document', got.includes(`'${doc1}'`), got);

    await enter("b = App.ActiveDocument.getObject('Box')");
    await enter('b.Length = 40');
    got = await enter('b.Length.Value');
    check('a statement writes the document', /\b40\.0\b/.test(got), got);

    await ask('viewonly', () => window.fcviewerViewOnly === true);
    got = await enter('b.Height = 99');
    const read = await enter('b.Height.Value');
    check("the viewer's view-only mode holds for its console",
          /PermissionError/.test(got) && /\b10\.0\b/.test(read), { got, read });

    await ask('edit', () => window.fcviewerViewOnly === false);
    got = await enter('b.Height = 12');
    check('edit access given back reaches the console', !/Error/.test(got), got);

    if (doc2) {
      window.fcviewerSwitchDoc?.(doc2);
      got = await enter('App.ActiveDocument.Name');
      check("the viewer's document switch moves its console", got.includes(`'${doc2}'`), got);
      got = await enter("App.ActiveDocument.addObject('Part::Box', 'FromViewer').Width = 5");
      check('a statement writes the document switched to', !/Error/.test(got), got);
    }

    const stats: any = (window as any).fcxConsoleStats?.();
    report.stats = stats;
    check('only bridge answers reach the bridge', !!stats && stats.ops > 0 && stats.otherBytesDown === 0,
          stats);
    window.fcviewerSetClient?.('fcx-ask:done');
    report.ok = report.checks.every((c) => c.pass);
  } catch (e: any) {
    report.error = String(e?.stack ?? e);
    console.log('FAIL drive | ' + report.error + '\n' + output());
  }
  (window as any).fcxConsoleViewer = report;
})();
