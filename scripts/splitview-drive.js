// Drive the browser viewer's split view chrome through a real browser
// (docs/SplitViews.md sec 9.4 and 22, docs/HandsOnQueue.md entry 57).
//
// The chrome in src/Gui/Renderer/web/src/splitview.tsx follows the
// desktop's view cells: a drag of a corner zone or of a border is shown
// as frames and carried out when the primary button is released, and
// Escape, another button or the page losing the pointer give it up. None
// of that needs a scene, so the page is opened with no document served
// and the layout is read off the DOM: the cells, the frames of the drag
// under way (.fc-split-frame, data-kind) and what the drag is
// (.fc-split-root, data-op). A split that cannot be has no frame: the
// document has the class fc-split-forbidden, which is the cursor.
//
// Usage:  node scripts/splitview-drive.js <url of fcviewer.html> <result.json>
// Needs PUPPETEER_PATH (a puppeteer-core install) and CHROME.
const ppPath = process.env.PUPPETEER_PATH || 'puppeteer-core';
const puppeteer = require(ppPath);
const fs = require('fs');

const sleep = ms => new Promise(r => setTimeout(r, ms));
const [url, outFile] = process.argv.slice(2);
const results = [];
const check = (name, ok, detail) => {
  results.push({name, ok: !!ok, detail: detail === undefined ? '' : JSON.stringify(detail)});
  console.log((ok ? 'PASS ' : 'FAIL ') + name + (detail === undefined ? '' : ' | ' + JSON.stringify(detail)));
  return !!ok;
};
const near = (a, b, slack = 2) => Math.abs(a - b) <= slack;

(async () => {
  const browser = await puppeteer.launch({
    executablePath: process.env.CHROME,
    headless: 'new',
    args: ['--enable-unsafe-swiftshader', '--use-angle=swiftshader', '--no-sandbox',
           '--window-size=1400,900'],
  });
  const errors = [];
  try {
    const page = await browser.newPage();
    page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
    await page.setViewport({width: 1400, height: 900});
    // a store of its own: a layout kept from another run would be restored
    await page.goto(url, {waitUntil: 'domcontentloaded'});
    await page.evaluate(() => { try { localStorage.clear(); } catch (e) {} });
    await page.reload({waitUntil: 'domcontentloaded'});
    await page.waitForSelector('.fc-split-zone-tr', {timeout: 180000});
    await sleep(1500);

    const cells = () => page.evaluate(() =>
      Array.from(document.querySelectorAll('.fc-split-cell')).map(el => {
        const r = el.getBoundingClientRect();
        return {x: Math.round(r.left), y: Math.round(r.top), w: Math.round(r.width),
                h: Math.round(r.height)};
      }).sort((a, b) => a.x - b.x || a.y - b.y));
    const frames = () => page.evaluate(() => ({
      op: document.querySelector('.fc-split-root').dataset.op || '',
      list: Array.from(document.querySelectorAll('.fc-split-frame')).map(el => {
        const r = el.getBoundingClientRect();
        return {kind: el.dataset.kind, x: Math.round(r.left), y: Math.round(r.top),
                w: Math.round(r.width), h: Math.round(r.height),
                cross: !!el.querySelector('.fc-split-cross'),
                clip: getComputedStyle(el).clipPath,
                plus: !!el.querySelector('.fc-split-plus')};
      }),
    }));
    const kinds = f => f.list.map(x => x.kind).join(',');
    // the top right zone of a cell: 14 px, in its corner
    const zone = c => ({x: c.x + c.w - 7, y: c.y + 7});
    const drag = async (from, to) => {
      await page.mouse.move(from.x, from.y);
      await page.mouse.down();
      await page.mouse.move((from.x + to.x) / 2, (from.y + to.y) / 2, {steps: 3});
      await page.mouse.move(to.x, to.y, {steps: 3});
      await sleep(120);
    };
    const release = async () => { await page.mouse.up(); await sleep(250); };
    const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

    // ---- one cell to start from
    let cs = await cells();
    if (!check('one cell to start from, the size of the page', cs.length === 1 && cs[0].w >= 1200,
               cs))
      throw new Error('no single cell');
    const whole = cs[0];

    // ---- split: shown, not done; back at the press point it goes; released, done
    let from = zone(whole);
    await drag(from, {x: from.x - 500, y: from.y + 60});
    let f = await frames();
    check('split: nothing is split while the button is down', (await cells()).length === 1);
    if (check('split: two frames, the cell kept and the fresh one', f.op === 'split'
              && kinds(f) === 'kept,fresh', f)) {
      check('split: the two frames tile the cell, the border under the cursor',
            f.list[0].x === whole.x && near(f.list[0].w, from.x - 500 - whole.x)
            && near(f.list[0].x + f.list[0].w, f.list[1].x)
            && near(f.list[1].x + f.list[1].w, whole.x + whole.w), f.list);
      check('split: the fresh one has a plus', f.list[1].plus && !f.list[0].plus);
    }
    await page.mouse.move(from.x - 3, from.y + 3, {steps: 3});
    await sleep(120);
    check('split: dragged back to where it was pressed, the frames go',
          (await frames()).op === '', await frames());
    await page.mouse.move(from.x - 500, from.y + 60, {steps: 3});
    await sleep(120);
    f = await frames();
    await release();
    cs = await cells();
    check('split: released, two cells of the sizes the frames had, and no frames',
          cs.length === 2 && f.list.length === 2 && near(cs[0].w, f.list[0].w)
          && near(cs[1].w, f.list[1].w) && (await frames()).op === '', {cs, f: f.list});
    if (cs.length !== 2)
      throw new Error('the split did not happen');

    // ---- the look of a frame (the desktop's OverlayDragFrame::paintFrame)
    from = zone(cs[0]);
    await drag(from, {x: from.x - 350, y: from.y + 30});
    const style = await page.evaluate(() => {
      const el = document.querySelector('.fc-split-frame-kept');
      if (!el) return null;
      const s = getComputedStyle(el);
      return {background: s.backgroundColor, shadow: s.boxShadow, border: s.borderTopColor,
              borderWidth: s.borderTopWidth};
    });
    check('look: the face of a frame is the accent at 0.3',
          style && /rgba\(85, 123, 182, 0\.3\)/.test(style.background), style);
    check('look: ... inside a white border 2 px wide',
          style && /rgb\(255, 255, 255\)/.test(style.shadow) && /2px inset|inset/.test(style.shadow)
          && / 2px /.test(' ' + style.shadow + ' '), style && style.shadow);
    check('look: ... inside a thin dark line', style && style.borderWidth === '1px'
          && /rgba\(0, 0, 0, 0\.7/.test(style.border), style && [style.border, style.borderWidth]);

    // ---- giving a drag up: Escape, another button, the page losing the pointer
    let before = await cells();
    await page.keyboard.press('Escape');
    await sleep(150);
    check('cancel: Escape during a split drag takes the frames away', (await frames()).op === '');
    await release();
    check('cancel: ... and the release then splits nothing', same(await cells(), before),
          await cells());

    for (const button of ['right', 'middle']) {
      await drag(from, {x: from.x - 350, y: from.y + 30});
      const shown = (await frames()).op;
      await page.mouse.down({button});
      await sleep(150);
      const after = (await frames()).op;
      await page.mouse.up({button});
      await release();
      check('cancel: the ' + button + ' button during a split drag takes the frames away, '
            + 'and the release splits nothing',
            shown === 'split' && after === '' && same(await cells(), before),
            {shown, after, cells: await cells()});
    }
    const menus = await page.evaluate(() => document.querySelectorAll('.fc-ctxmenu').length);
    check('cancel: the context menu of the page does not come up for that right click',
          menus === 0, menus);

    await drag(from, {x: from.x - 350, y: from.y + 30});
    await page.evaluate(() => window.dispatchEvent(new Event('blur')));
    await sleep(150);
    check('cancel: the window losing the front takes the frames away', (await frames()).op === '');
    await release();
    check('cancel: ... and the release then splits nothing', same(await cells(), before));

    // ---- the border: frames at the new sizes, moved at the release
    cs = await cells();
    const border = {x: cs[1].x, y: cs[1].y + Math.round(cs[1].h / 2)};
    await drag(border, {x: border.x + 90, y: border.y});
    f = await frames();
    check('border: no cell changes size while the button is down', same(await cells(), cs));
    const ordered = f.list.slice().sort((a, b) => a.x - b.x);
    check('border: each of the two cells has a frame at the size the move gives it',
          f.op === 'resize' && kinds(f) === 'kept,kept' && near(ordered[0].w, cs[0].w + 90)
          && near(ordered[1].w, cs[1].w - 90) && near(ordered[1].x, cs[1].x + 90), f);
    await release();
    let now = await cells();
    check('border: released, the cells have the sizes of their frames',
          near(now[0].w, cs[0].w + 90) && near(now[1].w, cs[1].w - 90), now);
    await drag({x: now[1].x, y: border.y}, {x: now[1].x - 60, y: border.y});
    await page.keyboard.press('Escape');
    await sleep(150);
    const gone = (await frames()).op === '';
    await release();
    check('border: Escape gives a border drag up', gone && same(await cells(), now), await cells());

    // ---- the minimum cell size, 300: the border stops at it, and past it closes
    cs = await cells();
    const least = 300;
    const limit = cs[1].x + (cs[1].w - least);       // where the right cell is 300 wide
    await drag({x: cs[1].x, y: border.y}, {x: limit + 8, y: border.y});
    f = await frames();
    const right = f.list.slice().sort((a, b) => a.x - b.x)[1];
    check('minimum: a border dragged a little past it stops at the minimum cell size',
          f.op === 'resize' && right && near(right.w, least), f);
    await page.mouse.move(limit + 120, border.y, {steps: 3});
    await sleep(150);
    f = await frames();
    check('close: dragged well past it, the cell is shown as going, crossed out, '
          + 'the other framed over both',
          f.op === 'close' && kinds(f) === 'kept,going' && f.list[1].cross
          && near(f.list[0].w, whole.w) && near(f.list[1].x, cs[1].x)
          && near(f.list[1].w, cs[1].w), f);
    check('close: nothing is closed while the button is down', (await cells()).length === 2);
    await page.keyboard.press('Escape');
    await sleep(150);
    await release();
    check('close: Escape gives that up too', same(await cells(), cs), await cells());
    await drag({x: cs[1].x, y: border.y}, {x: limit + 120, y: border.y});
    await release();
    check('close: released, the cell is closed', (await cells()).length === 1, await cells());

    // ---- a join: one frame over both, a stop sign on the cell that goes
    cs = await cells();
    from = zone(cs[0]);
    await drag(from, {x: from.x - 600, y: from.y + 60});
    await release();
    cs = await cells();
    if (check('join: two cells again to join', cs.length === 2, cs)) {
      from = zone(cs[0]);
      await drag(from, {x: cs[1].x + Math.round(cs[1].w / 2), y: from.y + 80});
      f = await frames();
      check('join: the cell that stays is framed over both, the other is going, crossed out',
            f.op === 'join' && kinds(f) === 'kept,going' && near(f.list[0].w, whole.w)
            && near(f.list[1].x, cs[1].x) && f.list[1].cross, f);
      const going = await page.evaluate(() => {
        const el = document.querySelector('.fc-split-frame-going');
        const s = getComputedStyle(el);
        return {background: s.backgroundColor, border: s.borderTopWidth,
                borderColor: s.borderTopColor};
      });
      check('join: the cell that goes is framed red',
            /rgba\(200, 40, 40, 0\.27\)/.test(going.background) && going.border === '2px'
            && /rgb\(200, 40, 40\)/.test(going.borderColor), going);
      check('join: the face of the frame of the cell that stays is left off it',
            /inset\(0px [0-9.]+px 0px 0px\)/.test(f.list[0].clip), f.list[0].clip);
      check('join: nothing is joined while the button is down', (await cells()).length === 2);
      await release();
      check('join: released, one cell and no frames',
            (await cells()).length === 1 && (await frames()).op === '', await cells());
    }

    // ---- three cells in a row: a border takes room from the cell next to it and from no
    // other; at that cell's minimum the drag closes it, and the third cell never moves
    cs = await cells();
    from = zone(cs[0]);
    await drag(from, {x: 500, y: from.y + 60});
    await release();
    cs = await cells();
    from = zone(cs[1]);
    await drag(from, {x: 950, y: from.y + 60});
    await release();
    cs = await cells();
    if (check('row: three cells in a row to start from', cs.length === 3
              && near(cs[0].w, 500) && near(cs[1].w, 450) && near(cs[2].w, 450), cs)) {
      const row = cs;
      const y = Math.round(row[0].h / 2);
      const stop = row[1].x + (row[1].w - least);       // where the middle cell is 300 wide
      await drag({x: row[1].x, y}, {x: stop + 8, y});
      f = await frames();
      const got = f.list.slice().sort((a, b) => a.x - b.x);
      check('row: the first border dragged a little past the middle cell\'s minimum frames '
            + 'the two cells beside it, the middle one at the minimum',
            f.op === 'resize' && got.length === 2 && near(got[0].w, row[0].w + row[1].w - least)
            && near(got[1].w, least), f.list);
      check('row: ... and the third cell has no frame: its border is not pushed along',
            !f.list.some(x => near(x.x, row[2].x) && near(x.w, row[2].w))
            && !f.list.some(x => x.x + x.w > row[2].x + 2), f.list);
      await page.mouse.move(stop + 130, y, {steps: 3});
      await sleep(150);
      f = await frames();
      const kept = f.list.find(x => x.kind === 'kept');
      const going = f.list.find(x => x.kind === 'going');
      check('row: dragged well past it, the middle cell is the one going, the first framed '
            + 'over the room of both, the third untouched',
            f.op === 'close' && f.list.length === 2 && going && kept && near(going.x, row[1].x)
            && near(going.w, row[1].w) && going.cross && near(kept.x, row[0].x)
            && near(kept.w, row[0].w + row[1].w), f.list);
      await release();
      now = await cells();
      check('row: released, the middle cell is closed, the first has its room, '
            + 'the third is where and as wide as it was',
            now.length === 2 && near(now[0].w, row[0].w + row[1].w) && near(now[1].x, row[2].x)
            && near(now[1].w, row[2].w), now);
      // down to one cell again for what follows
      cs = await cells();
      if (cs.length === 2) {
        from = zone(cs[0]);
        await drag(from, {x: cs[1].x + Math.round(cs[1].w / 2), y: from.y + 80});
        await release();
      }
    }

    // ---- a corner only creates: under the minimum a split is a forbidden cursor and a
    // message at each turn of the cursor, no frame
    await page.setViewport({width: 520, height: 420});
    await sleep(600);
    cs = await cells();
    from = zone(cs[0]);
    const forbidden = () => page.evaluate(
      () => document.documentElement.classList.contains('fc-split-forbidden'));
    const cursor = () => page.evaluate(() => {
      const el = document.elementFromPoint(260, 210) || document.body;
      return getComputedStyle(el).cursor;
    });
    const saidNow = () => page.evaluate(() => {
      const el = document.querySelector('.fc-split-note');
      return el ? el.textContent : '';
    });
    const refusals = () => errors.filter(e => /is not split/.test(e)).length;
    errors.length = 0;
    await drag(from, {x: from.x - 200, y: from.y + 20});
    f = await frames();
    check('refusal: a corner drag that would leave a cell under the minimum shows no frame',
          f.op === '' && f.list.length === 0, f);
    check('refusal: ... the cursor is the forbidden one, wherever the pointer is',
          (await forbidden()) && (await cursor()) === 'not-allowed', await cursor());
    check('refusal: ... and the reason is said at once, on the page and as an error',
          /is not split/.test(await saidNow()) && refusals() === 1,
          {said: await saidNow(), refusals: refusals()});
    await page.mouse.move(from.x - 2, from.y + 2, {steps: 3});
    await sleep(120);
    check('refusal: back where it was pressed the cursor is no longer the forbidden one',
          !(await forbidden()));
    await page.mouse.move(from.x - 200, from.y + 20, {steps: 3});
    await sleep(120);
    check('refusal: every turn to the forbidden cursor says it once more',
          (await forbidden()) && refusals() === 2, refusals());
    await release();
    check('refusal: the release splits nothing, closes nothing and says nothing more',
          (await cells()).length === 1 && refusals() === 2 && !(await forbidden()),
          {cells: (await cells()).length, refusals: refusals()});
  }
  catch (e) {
    check('the drive ran', false, String(e && e.stack || e).slice(0, 600));
  }
  finally {
    if (outFile)
      fs.writeFileSync(outFile, JSON.stringify({results, errors: errors.slice(0, 20)}, null, 1));
    await browser.close();
  }
})();
