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
// Completion (docs/Sandbox.md 7.23) is driven by environment, so the
// positional arguments stay what they were:
//
//   FC_PANEL_COMPLETE=<text>  type this into the first enabled field with
//                             real key events (so the input handler runs,
//                             which setting .value does not) and report
//                             what completion drew
//   FC_PANEL_FX=1             click the field's fx button first, which is
//                             the path a user takes to an expression
//   FC_PANEL_PHONE=1          a phone viewport with touch, so the list is
//                             pinned above the keyboard instead of in flow
//   FC_PANEL_DIALOG=<label>   click the card's ask button, wait for the
//                             mirrored dialog layer (W4), answer it with
//                             the button carrying this label, and report
//                             what the host's panel says afterwards
//   FC_PANEL_ASK=<label>      the card button that raises the dialog
//                             (default "Ask")
//   FC_PANEL_UPLOAD=<path>    choose this LOCAL file in the card's file
//                             chooser (W5) and report what the host made
//                             of it. The bytes go up over the control
//                             lane and the host answers with the path it
//                             wrote, which the card then announces as a
//                             pick -- so the verdict is the HOST's label
//                             changing, not the page agreeing with itself
//
// One limit, stated rather than pretended: headless Chrome has no
// on-screen keyboard, so visualViewport never shrinks and the list's
// inset is always 0 here. What this checks is that the list is the shape
// it should be; where it sits when a keyboard is up needs a handset.
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
    // W2's item views, read STRUCTURALLY rather than as text: the flat
    // `rows` above cannot tell a header from a row, a nested child from a
    // top-level one, or an item's check box from the QCheckBox widget the
    // `checks` field reports. One entry per item view on the card.
    items: [...card.querySelectorAll('.fc-panel-rows')].map((box) => {
      const isHead = (el) => el.classList.contains('fc-panel-head-row');
      const depth = (el) => {
        let n = 0;
        for (let p = el.parentElement; p && p !== box; p = p.parentElement)
          if (p.classList.contains('fc-panel-kids')) n++;
        return n;
      };
      const body = [...box.querySelectorAll('.fc-panel-row')].filter((r) => !isHead(r));
      return {
        header: [...box.querySelectorAll(':scope > .fc-panel-head-row span')]
          .map(text).filter((s) => s !== ''),
        // The header and the rows must share one template or nothing lines
        // up, so report it from a row rather than from the header.
        tracks: body.length ? getComputedStyle(body[0]).gridTemplateColumns : '',
        count: body.length,
        checked: body.filter((r) => r.querySelector('input[type=checkbox]:checked')).length,
        selected: body.filter((r) => r.classList.contains('fc-panel-row-on')).length,
        maxDepth: body.reduce((m, r) => Math.max(m, depth(r)), 0),
        rows: body.slice(0, 12).map((r) => {
          // A leaf renders its twisty as an empty span, not a button, so
          // this is null for most rows -- `text` takes an element.
          const twisty = r.querySelector('button.fc-panel-twisty');
          return {
            depth: depth(r),
            // The twisty is a span too, and counting it as a cell shifts
            // every column in the report by one.
            cells: [...r.querySelectorAll(':scope > span')]
              .filter((s) => !s.classList.contains('fc-panel-twisty')).map(text),
            boxes: [...r.querySelectorAll('input[type=checkbox]')].map((c) => c.checked),
            twisty: twisty ? text(twisty) : '',
            on: r.classList.contains('fc-panel-row-on'),
            dim: r.classList.contains('fc-panel-row-off'),
          };
        }),
      };
    }),
    pictures: card.querySelectorAll('.fc-panel-pic').length,
    // W3: what actually resolved. A picture or an icon that never came
    // back leaves an element with no src, or no element at all, and
    // naturalWidth is 0 until the bytes decode -- so this reports the
    // bytes arriving, not merely the tag being written.
    pics: [...card.querySelectorAll('.fc-panel-pic')].map((el) => ({
      w: el.naturalWidth, h: el.naturalHeight, kind: (el.src || '').slice(5, 14),
    })),
    icons: [...card.querySelectorAll('.fc-panel-icon')].map((el) => ({
      w: el.naturalWidth, kind: (el.src || '').slice(5, 14),
    })),
    // A grid that planned a NaN track would collapse; report the tracks so
    // the corpus's two-wide form rows are visible in the result.
    grids: [...card.querySelectorAll('.fc-panel-grid')].map(
      (el) => getComputedStyle(el).gridTemplateColumns),
    // W4: the card's own entry to search, and whether the chrome's
    // launcher is in the DOM at all. The two belong together -- the entry
    // exists BECAUSE the launcher hides behind a card on a narrow
    // viewport and a phone has no '/' key, so a run that reports the
    // entry present and the launcher absent is the case it was added for.
    search: !!card.querySelector('.fc-panel-search'),
    launcher: !!document.querySelector('.fc-launcher'),
    height: Math.round(card.getBoundingClientRect().height),
  };
}

/// The dialog layer (W4): a `dialog:<n>` root drawn over the card.
///
/// Read separately from the card, and it has to be: the layer is PORTALLED
/// to the body, so it is NOT inside `.fc-panel` and every selector in
/// readCard misses it by construction.
function readDialog() {
  const back = document.querySelector('.fc-dlg-backdrop');
  if (!back) return null;
  const box = back.querySelector('.fc-dlg');
  const text = (el) => (el ? (el.textContent || '').trim() : '');
  const rect = box ? box.getBoundingClientRect() : null;
  // What is actually on top at the box's centre. A layer that renders but
  // sits UNDER the chrome takes no clicks, and in a text report that
  // failure is indistinguishable from "the dialog never arrived" -- the
  // trap the HUD-over-the-editor hunt already cost once.
  const over = (() => {
    if (!rect) return null;
    const at = document.elementFromPoint(rect.x + rect.width / 2, rect.y + rect.height / 2);
    return at ? `${at.tagName}.${at.className || ''}` : 'none';
  })();
  return {
    count: document.querySelectorAll('.fc-dlg-backdrop').length,
    title: text(back.querySelector('.fc-dlg-head')),
    labels: [...back.querySelectorAll('.fc-panel-label')].map(text),
    buttons: [...back.querySelectorAll('.fc-panel-btn')].map(text),
    // A message box's icon is a real picture leaf, so it proves the image
    // round trip inside a dialog and not only on the card.
    pics: [...back.querySelectorAll('.fc-panel-pic')].map(
      (el) => ({ w: el.naturalWidth, h: el.naturalHeight })),
    modal: !back.classList.contains('fc-dlg-modeless'),
    z: getComputedStyle(back).zIndex,
    coveredBy: over && !/fc-dlg|fc-panel/.test(over) ? over : null,
    height: rect ? Math.round(rect.height) : 0,
  };
}

(async () => {
  const [url, shot, timeoutArg, typed] = process.argv.slice(2);
  if (!url) {
    console.error('usage: node panel-drive.js <url> [out.png] [timeout_ms] [type]');
    process.exit(2);
  }
  const timeout = +(timeoutArg || 60000);
  const phone = !!process.env.FC_PANEL_PHONE;
  const completeWith = process.env.FC_PANEL_COMPLETE;
  const clickFx = !!process.env.FC_PANEL_FX;
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
    // A phone viewport with touch renders the narrow layout, where the
    // completion list is pinned above the keyboard: the same list as in
    // flow, but placed within a thumb's reach.
    await page.setViewport(phone
      ? { width: 390, height: 844, isMobile: true, hasTouch: true, deviceScaleFactor: 3 }
      : { width: 1280, height: 1000 });
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
    // Pictures and icons are a round trip BEHIND the widgets that name them
    // (W3): the bag carries an `img:` id or an icon name, and the bytes are
    // fetched after the card has already drawn. A read that fires the
    // moment a field exists therefore reports every icon missing, which is
    // exactly what it reported first. Wait for the ones on the card to
    // decode -- and do not insist there are any, because most panels have
    // none.
    await page.waitForFunction(() => {
      const imgs = [...document.querySelectorAll(
        '.fc-panel .fc-panel-icon, .fc-panel .fc-panel-pic')];
      return imgs.length > 0 && imgs.every((el) => el.complete && el.naturalWidth > 0);
    }, { timeout: 15000, polling: 250 }).catch(() => {});
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
    // W2's write path: click an item's check box (FC_PANEL_CHECK=<n>, the
    // nth box across the card's item views) and report what the card holds
    // after the round trip. This half only shows the page agreeing with
    // itself -- the real check is on the HOST, where a Sketcher constraint
    // must move into virtual space, and that is read from the process
    // rather than from here.
    const checkNth = process.env.FC_PANEL_CHECK;
    if (checkNth !== undefined && card) {
      const nth = +checkNth;
      const before = await page.evaluate((i) => {
        const boxes = [...document.querySelectorAll(
          '.fc-panel-rows .fc-panel-row input[type=checkbox]')];
        if (!boxes[i]) return null;
        const was = boxes[i].checked;
        boxes[i].click();
        return was;
      }, nth);
      await new Promise((r) => setTimeout(r, 2500));
      const after = await page.evaluate((i) => {
        const boxes = [...document.querySelectorAll(
          '.fc-panel-rows .fc-panel-row input[type=checkbox]')];
        return boxes[i] ? boxes[i].checked : null;
      }, nth);
      card.checkWrite = { index: nth, before, after };
    }
    // W3's picture: a custom-painted leaf is an IMAGE in the page, so a
    // click on it does nothing locally -- the pointer is sent back and
    // replayed into the real widget on the host. The proof is therefore a
    // change the HOST makes arriving: scripts/demo-picturepanel.py counts
    // the clicks in a label and names where they landed, so the whole
    // round trip is one readable string.
    if (process.env.FC_PANEL_PIC_CLICK !== undefined && card) {
      const labels = () => [...document.querySelectorAll('.fc-panel .fc-panel-label')]
        .map((el) => (el.textContent || '').trim());
      const before = await page.evaluate((read) => {
        const el = document.querySelector('.fc-panel .fc-panel-pic');
        if (!el) return null;
        const box = el.getBoundingClientRect();
        return {
          at: [Math.round(box.x + box.width / 2), Math.round(box.y + box.height / 2)],
          labels: new Function('return (' + read + ')()')(),
          natural: [el.naturalWidth, el.naturalHeight],
          src: (el.src || '').slice(5, 14),
        };
      }, labels.toString());
      if (!before) {
        card.picClick = { error: 'no picture on the card' };
      }
      else {
        await page.mouse.click(before.at[0], before.at[1]);
        await new Promise((r) => setTimeout(r, 2500));
        const after = await page.evaluate((read) => ({
          labels: new Function('return (' + read + ')()')(),
          natural: (() => {
            const el = document.querySelector('.fc-panel .fc-panel-pic');
            return el ? [el.naturalWidth, el.naturalHeight] : null;
          })(),
        }), labels.toString());
        card.picClick = {
          at: before.at, natural: before.natural, src: before.src,
          before: before.labels, after: after.labels, naturalAfter: after.natural,
        };
      }
    }
    // W5's round trip: choose a LOCAL file in the card's own picker. The
    // bytes go up over the control lane, the host writes them into a
    // directory of its own choosing and answers with the path, and the
    // card announces that path as a pick. The verdict is therefore the
    // HOST's label -- rewritten by the panel's slot with what it was
    // handed and how big the file is on its own disk -- not the page
    // agreeing with itself about a value it just typed.
    const uploadPath = process.env.FC_PANEL_UPLOAD;
    if (uploadPath !== undefined && card) {
      const labels = () => [...document.querySelectorAll('.fc-panel .fc-panel-label')]
        .map((el) => (el.textContent || '').trim());
      const before = await page.evaluate(labels);
      // The real <input type=file> is hidden behind the Browse button
      // (it cannot be styled), and setting its files is how a driver
      // picks without a native dialog -- there is no dialog to drive.
      const input = await page.$('.fc-panel input.fc-panel-filepick');
      if (!input) {
        card.upload = { error: 'no file chooser on the card', before };
      }
      else {
        await input.uploadFile(uploadPath);
        await new Promise((r) => setTimeout(r, 3000));
        card.upload = {
          sent: uploadPath,
          before,
          after: await page.evaluate(labels),
          shown: await page.evaluate(() => {
            const el = document.querySelector('.fc-panel input.fc-panel-field');
            return el ? el.value : null;
          }),
          failed: await page.evaluate(() => {
            const el = document.querySelector('.fc-panel .fc-panel-error');
            return el ? (el.textContent || '').trim() : '';
          }),
        };
      }
    }
    // Completion (docs/Sandbox.md 7.23). Real key events, not a .value
    // write: the whole controller hangs off the input handler, and
    // setting the property fires nothing.
    if (completeWith && card) {
      // Expression entry is a dialog, not the field (7.23): open it from
      // fx and type in its editor. FC_PANEL_FX=0 types into the plain
      // field instead, which is what a value edit does.
      const fx = clickFx ? await page.$('.fc-panel .fc-panel-fx') : null;
      if (clickFx && !fx) {
        card.completion = { error: 'no fx button (is the field bound?)' };
      }
      else {
        if (fx) {
          await fx.click();
          await page.waitForSelector('.fc-expr-edit', { timeout: 5000 }).catch(() => {});
        }
        const editor = await page.$(fx ? '.fc-expr-edit'
                                       : '.fc-panel input.fc-panel-field:not([disabled])');
        if (!editor) throw new Error('no editor to type into');
        await editor.click({ clickCount: 3 });   // take what is there
        await page.keyboard.type(completeWith, { delay: 30 });
        await new Promise((r) => setTimeout(r, 900));
        card.completion = await page.evaluate(() => {
          const text = (el) => (el.textContent || '').trim();
          // One list everywhere (docs/Sandbox.md 7.26): in the dialog's
          // flow on a wide viewport, pinned above the keyboard on a phone.
          const rowText = (el) => text(el.querySelector('.fc-pick-title') || el);
          const listEls = [...document.querySelectorAll('.fc-complete:not(.fc-complete-pinned) .fc-pick-row')];
          const pinnedEls = [...document.querySelectorAll('.fc-complete-pinned .fc-pick-row')];
          const sheet = document.querySelector('.fc-complete-sheet');
          const box = sheet ? sheet.getBoundingClientRect() : null;
          const dlg = document.querySelector('.fc-expr-edit');
          const input = dlg || [...document.querySelectorAll('.fc-panel input.fc-panel-field')]
            .find((el) => !el.disabled);
          const okBtn = [...document.querySelectorAll('.fc-expr-buttons button')]
            .find((b) => (b.textContent || '').trim() === 'OK');
          // What is actually at the editor's centre. A covered element
          // still focuses, clicks land on whatever is on top, and the
          // keystrokes go nowhere -- which reads as "typing did not
          // work" and cost a long hunt once (the HUD card over the
          // expression dialog on a phone viewport).
          const over = (() => {
            if (!input) return null;
            const r = input.getBoundingClientRect();
            const at = document.elementFromPoint(r.x + r.width / 2, r.y + r.height / 2);
            return at ? `${at.tagName}.${at.className || ''}` : 'none';
          })();
          return {
            dialog: !!dlg,
            coveredBy: over && !/fc-expr-edit|fc-panel-field/.test(over) ? over : null,
            // What it would evaluate to, the dialog's whole point.
            resultLine: text(document.querySelector('.fc-expr-result')
                             || document.createElement('i')),
            okEnabled: okBtn ? !okBtn.disabled : null,
            typed: input ? input.value : null,
            // The phone rule: a decimal keyboard has no letters, so a
            // field being used for an expression must not ask for one.
            inputMode: input ? input.getAttribute('inputmode') : null,
            list: listEls.map(rowText).slice(0, 12),
            pinned: pinnedEls.map(rowText).slice(0, 12),
            rowHeight: pinnedEls.length
              ? Math.round(pinnedEls[0].getBoundingClientRect().height) : 0,
            // 0 headless (no on-screen keyboard to inset it), but it
            // proves the pinned list is anchored to the visual viewport.
            sheetBottom: box ? Math.round(window.innerHeight - box.bottom) : null,
            error: text(document.querySelector('.fc-panel-error') || document.createElement('i')),
          };
        });
      }
    }
    // W4's round trip: a panel slot that blocks in QMessageBox::exec().
    // The answer is NOT the page agreeing with itself -- the host's label
    // is rewritten by the slot with the code exec() returned, and that
    // comes back over the wire, so `after` is the real verdict.
    const answerWith = process.env.FC_PANEL_DIALOG;
    if (answerWith !== undefined && card) {
      const askLabel = process.env.FC_PANEL_ASK || 'Ask';
      const labels = () => [...document.querySelectorAll('.fc-panel .fc-panel-label')]
        .map((el) => (el.textContent || '').trim());
      const before = await page.evaluate(labels);
      const clicked = await page.evaluate((want) => {
        const btn = [...document.querySelectorAll('.fc-panel .fc-panel-btn')]
          .find((b) => (b.textContent || '').trim() === want);
        if (!btn) return false;
        btn.click();
        return true;
      }, askLabel);
      if (!clicked) {
        card.dialog = { error: `no "${askLabel}" button on the card`, before };
      }
      else {
        // The box is walked on the tick AFTER its Show while the slot sits
        // in a nested loop, so the layer is a round trip behind the click:
        // "not yet" rather than "missing", the lesson W3's pictures taught.
        await page.waitForSelector('.fc-dlg-backdrop', { timeout: 15000 }).catch(() => {});
        const up = await page.evaluate(readDialog);
        // A shot WHILE the box is up. The one at the end of the run is
        // taken after the answer, when the layer is gone by design, so it
        // cannot show the very thing this stage draws.
        if (shot && up) {
          const upShot = shot.endsWith('.png') ? shot.slice(0, -4) + '-up.png'
                                               : shot + '-up.png';
          await page.screenshot({ path: upShot });
          console.log('shot ' + upShot);
        }
        let answered = null;
        if (up) {
          answered = await page.evaluate((want) => {
            const btn = [...document.querySelectorAll('.fc-dlg-backdrop .fc-panel-btn')]
              .find((b) => (b.textContent || '').trim() === want);
            if (!btn) return false;
            btn.click();
            return true;
          }, answerWith);
          await new Promise((r) => setTimeout(r, 2500));
        }
        card.dialog = {
          asked: askLabel, answerWith, answered, up, before,
          after: await page.evaluate(labels),
          gone: await page.evaluate(() => !document.querySelector('.fc-dlg-backdrop')),
        };
      }
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
  // An item view alone is a drawn panel too (W2): Sketcher's Elements and
  // Constraints carry no field between them, and calling that empty would
  // report the very thing this harness exists to check as a failure.
  const drewItems = (card.items || []).some((view) => view.count > 0);
  process.exit(card.fields.length || card.combos.length || drewItems ? 0 : 1);
})();
