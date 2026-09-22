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
//   FC_PANEL_RAISE=<label>    click that card button and report EVERY
//                             dialog layer without answering any of them:
//                             the modeless root (which must dim nothing
//                             and take no pointer) and two stacked boxes
//                             (the only thing the depth z-offset is for)
//   FC_PANEL_ESC=<label>      raise a dialog from that card button, then
//                             press Escape instead of clicking a button.
//                             The host's own label is the verdict: a
//                             reject returns 0, no button
//   FC_PANEL_EXPAND=<n>       click the nth twisty on the card and report
//                             what the host says it expanded. An expand
//                             that only happened in the page looks the
//                             same on screen, and only an item OP reaches
//                             the real tree
//   FC_PANEL_DROP=<command>   run this command to take the HOST away
//                             while the card is open, then report what
//                             the card says about it -- W5's "what a
//                             panel does when the socket drops". A
//                             command rather than Chrome's offline mode,
//                             which does not close a socket already open
//   FC_PANEL_BACK=<command>   bring it back, and report whether the card
//                             recovers on its own
//   FC_PANEL_STATS=1          report the client's own figures -- open to
//                             first frame, open to first paint, frames,
//                             wakes, ms inside apply, the last write's
//                             round trip. The browser half of 8.4
//   FC_PANEL_UPLOAD=<path>    choose this LOCAL file in the card's file
//                             chooser (W4b) and report what the host made
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
    // The same buttons WITH their state. A mirrored directory chooser
    // draws its Browse disabled and says why in the title -- no browser
    // picker can name a folder on the serving machine, and the ruling
    // forbids showing the page one -- and a list of labels cannot tell
    // that from a button that works.
    btnState: [...card.querySelectorAll('.fc-panel-btn')].map((b) => ({
      text: text(b), off: !!b.disabled, why: b.title || '',
    })),
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
            // W5: the colours the host asked for, read back off the
            // COMPUTED style. That is the only reading that proves the
            // four-float packing was decoded rather than merely carried --
            // a cell whose fg/bg never reached CSS reports the card's own
            // colour here and nothing else would show it.
            paints: [...r.querySelectorAll(':scope > span')]
              .filter((s) => !s.classList.contains('fc-panel-twisty'))
              .map((s) => {
                const cs = getComputedStyle(s);
                const on = cs.backgroundColor;
                return on === 'rgba(0, 0, 0, 0)' || on === 'transparent'
                  ? cs.color : `${cs.color} on ${on}`;
              }),
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
    // W5's finish: what a THUMB has to hit. Reported as the smallest of
    // each kind rather than a list, because one undersized control is the
    // finding -- 44 CSS px is the figure both platforms' guidance uses,
    // and the completion buttons already keep it under `max-width: 640px`.
    touch: (() => {
      const least = (sel) => {
        const els = [...card.querySelectorAll(sel)];
        if (!els.length) return null;
        return Math.round(Math.min(...els.map((el) => {
          const r = el.getBoundingClientRect();
          // The hit area is USUALLY the box -- but not where a control is
          // too small to grow (a tree's twisty, which would make every row
          // 44 px tall) and carries a transparent ::after instead. Reading
          // the box alone reports those as unfixed when they are fixed, so
          // the pseudo-element counts when it is bigger than its own box.
          const after = getComputedStyle(el, '::after');
          const grown = after && after.content !== 'none'
            ? Math.min(parseFloat(after.width) || 0, parseFloat(after.height) || 0)
            : 0;
          return Math.max(Math.min(r.height, r.width), grown || 0);
        })));
      };
      return {
        buttons: least('.fc-panel-btn'),
        twisties: least('button.fc-panel-twisty'),
        checks: least('input[type=checkbox]'),
        fields: least('input.fc-panel-field, select.fc-panel-field'),
        close: least('.fc-panel-close'),
        search: least('.fc-panel-search'),
        rows: least('.fc-panel-rows .fc-panel-row'),
      };
    })(),
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

/// EVERY dialog layer, innermost last (W5).
///
/// readDialog answers about the first backdrop only, which was enough
/// while no scene could raise two. Two stacked is the only thing the
/// card's depth z-offset exists for, and telling "both drew, in this
/// order" from "the second replaced the first" needs them all.
function readDialogs() {
  const text = (el) => (el ? (el.textContent || '').trim() : '');
  return [...document.querySelectorAll('.fc-dlg-backdrop')].map((back) => {
    const box = back.querySelector('.fc-dlg');
    const rect = box ? box.getBoundingClientRect() : null;
    // What is on top at this box's centre. With two layers up, the one
    // that should take the click is the one with the higher z -- and a
    // stack that renders in the wrong order is invisible in a list of
    // titles but obvious here.
    const at = rect
      ? document.elementFromPoint(rect.x + rect.width / 2, rect.y + rect.height / 2)
      : null;
    return {
      title: text(back.querySelector('.fc-dlg-head')),
      buttons: [...back.querySelectorAll('.fc-panel-btn')].map(text),
      labels: [...back.querySelectorAll('.fc-panel-label')].map(text),
      modal: !back.classList.contains('fc-dlg-modeless'),
      // A modeless layer must not dim the page: its backdrop is
      // transparent and takes no pointer events, which is precisely what
      // "the desktop is still live behind it" means in a page.
      dims: getComputedStyle(back).backgroundColor,
      takesPointer: getComputedStyle(back).pointerEvents,
      z: getComputedStyle(back).zIndex,
      topAtCentre: at ? `${at.tagName}.${at.className || ''}` : 'none',
      height: rect ? Math.round(rect.height) : 0,
    };
  });
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
    // A page console line saying "404 (Not Found)" names no URL, which is
    // why the 403/404 the viewer logs at load has stayed a rumour through
    // several sessions. This names it.
    page.on('response', (r) => {
      if (r.status() >= 400) console.log('[http] ' + r.status() + ' ' + r.url());
    });
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
    // W4b's round trip: choose a LOCAL file in the card's own picker. The
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
    // W2's nesting, driven at last: click a twisty and report what the
    // HOST says it expanded. The arrow moves locally the moment it is
    // clicked (the card applies the row op as well as sending it), so the
    // page alone cannot tell a real expand from a drawn one -- the verdict
    // is scripts/demo-treepanel.py's label, rewritten by the real tree's
    // own itemExpanded signal.
    const expandNth = process.env.FC_PANEL_EXPAND;
    if (expandNth !== undefined && card) {
      const nth = +expandNth;
      const labels = () => [...document.querySelectorAll('.fc-panel .fc-panel-label')]
        .map((el) => (el.textContent || '').trim());
      const before = await page.evaluate(labels);
      const clicked = await page.evaluate((i) => {
        const twisties = [...document.querySelectorAll('button.fc-panel-twisty')];
        if (!twisties[i]) return null;
        const was = (twisties[i].textContent || '').trim();
        twisties[i].click();
        return was;
      }, nth);
      if (clicked === null) {
        card.expand = { error: `no twisty ${nth} on the card`, before };
      }
      else {
        await new Promise((r) => setTimeout(r, 2500));
        card.expand = {
          index: nth, was: clicked, before,
          after: await page.evaluate(labels),
          // The card again AFTER the round trip, read whole rather than
          // counted: a real expand brings the children with it, and the
          // children are where a panel's cell COLOURS live -- a wash on a
          // row nobody has expanded yet is a wash nobody has seen.
          items: await page.evaluate(readCard).then((c) => (c ? c.items : null)),
          twisties: await page.evaluate(() => [...document.querySelectorAll('button.fc-panel-twisty')]
            .map((b) => (b.textContent || '').trim())),
        };
      }
    }
    // W4's unproven halves: a MODELESS root and two dialogs stacked.
    // Raised and read, never answered -- what is being checked is how the
    // layers drew, and answering one takes it off the screen.
    const raiseLabel = process.env.FC_PANEL_RAISE;
    if (raiseLabel !== undefined && card) {
      const clicked = await page.evaluate((want) => {
        const btn = [...document.querySelectorAll('.fc-panel .fc-panel-btn')]
          .find((b) => (b.textContent || '').trim() === want);
        if (!btn) return false;
        btn.click();
        return true;
      }, raiseLabel);
      if (!clicked) {
        card.raise = { error: `no "${raiseLabel}" button on the card` };
      }
      else {
        // Two boxes arrive a tick apart (the inner one is exec'd from
        // inside the outer's loop), so waiting for ONE layer and reading
        // immediately would report a stack as a single dialog.
        await page.waitForSelector('.fc-dlg-backdrop', { timeout: 15000 }).catch(() => {});
        await new Promise((r) => setTimeout(r, 2000));
        const layers = await page.evaluate(readDialogs);
        if (shot && layers.length) {
          const upShot = shot.endsWith('.png') ? shot.slice(0, -4) + '-up.png'
                                               : shot + '-up.png';
          await page.screenshot({ path: upShot });
          console.log('shot ' + upShot);
        }
        // A modeless layer leaves the card reachable; a modal one does
        // not. Asking the page what is at the card's centre is the one
        // reading that tells those apart without a human looking.
        card.raise = {
          button: raiseLabel,
          layers,
          cardReachable: await page.evaluate(() => {
            const el = document.querySelector('.fc-panel');
            if (!el) return null;
            const r = el.getBoundingClientRect();
            const at = document.elementFromPoint(r.x + r.width / 2, r.y + 40);
            return at ? `${at.tagName}.${at.className || ''}` : 'none';
          }),
        };
      }
    }
    // W4's Escape as reject. The op's shape was gated from the day it was
    // built and no drive had ever pressed the key; the host's label is the
    // verdict, because QDialog::reject() returns 0 -- no button -- and
    // that is a different string from any button's answer.
    const escFrom = process.env.FC_PANEL_ESC;
    if (escFrom !== undefined && card) {
      const askLabel = escFrom || process.env.FC_PANEL_ASK || 'Ask';
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
        card.escape = { error: `no "${askLabel}" button on the card`, before };
      }
      else {
        await page.waitForSelector('.fc-dlg-backdrop', { timeout: 15000 }).catch(() => {});
        const up = await page.evaluate(readDialogs);
        await page.keyboard.press('Escape');
        await new Promise((r) => setTimeout(r, 2500));
        card.escape = {
          asked: askLabel, layersUp: up.length, before,
          after: await page.evaluate(labels),
          gone: await page.evaluate(() => !document.querySelector('.fc-dlg-backdrop')),
        };
      }
    }
    // What a panel does when the socket DROPS, and what it does when the
    // socket comes back (W5's finish).
    //
    // Chrome's own offline mode rather than killing the serve: it drops
    // the page's WebSocket for real, needs no hook in the product, and --
    // the half that matters -- it can be switched back, so the same run
    // sees the loss AND the recovery. Killing the host would only ever
    // show the first.
    if (process.env.FC_PANEL_DROP !== undefined && card) {
      // Chrome's own view of the socket, over CDP. Without this the run
      // cannot tell "the card ignored the drop" from "the socket never
      // dropped" -- the first attempt reported a card that had not
      // changed and proved neither, because nothing in the page or the
      // serve log says a WebSocket closed.
      const sockets = { created: 0, closed: 0, errors: [] };
      const cdp = await page.target().createCDPSession();
      await cdp.send('Network.enable');
      cdp.on('Network.webSocketCreated', () => { sockets.created += 1; });
      cdp.on('Network.webSocketClosed', () => { sockets.closed += 1; });
      cdp.on('Network.webSocketFrameError', (e) =>
        sockets.errors.push(String(e.errorMessage).slice(0, 80)));
      const read = () => page.evaluate(() => {
        const el = document.querySelector('.fc-panel');
        const empty = el && el.querySelector('.fc-panel-empty');
        const strip = el && el.querySelector('.fc-panel-offline');
        return {
          card: !!el,
          says: empty ? (empty.textContent || '').trim() : '',
          // The strip the card shows while the socket is down: content
          // kept, staleness admitted.
          offline: strip ? (strip.textContent || '').trim() : '',
          fields: el ? el.querySelectorAll('input.fc-panel-field').length : 0,
          rows: el ? el.querySelectorAll('.fc-panel-rows .fc-panel-row').length : 0,
        };
      });
      const before = await read();
      // The page's OWN signal, which is what the card reacts to
      // (control.ts dispatches `fc:connection` on the window). Recorded
      // from the harness rather than added to the product: a socket that
      // closes WITHOUT this firing would explain a card that never
      // noticed, and that is a different bug from a card that ignores it.
      await page.evaluate(() => {
        window.__fcConn = [];
        window.addEventListener('fc:connection', (e) => window.__fcConn.push(!!e.detail));
      });
      // Chrome's offline mode was tried first and does NOT close a
      // WebSocket that is already open: the run came back with
      // `closed: 0`, no `fc:connection` event, and a card that had not
      // changed -- which proved nothing at all. So the host is really
      // taken away, by a command this harness is handed.
      require('child_process').execSync(process.env.FC_PANEL_DROP, { stdio: 'inherit' });
      await new Promise((r) => setTimeout(r, 12000));
      const dropped = await read();
      if (shot) {
        const offShot = shot.endsWith('.png') ? shot.slice(0, -4) + '-offline.png'
                                              : shot + '-offline.png';
        await page.screenshot({ path: offShot });
        console.log('shot ' + offShot);
      }
      // Back, if the caller said how. The card re-asks on the connection
      // event (panel.tsx), and a serve takes a few seconds to come up and
      // put its panel back, so this waits generously rather than
      // reporting a card that simply had not been told yet.
      if (process.env.FC_PANEL_BACK) {
        require('child_process').execSync(process.env.FC_PANEL_BACK, { stdio: 'inherit' });
        await new Promise((r) => setTimeout(r, 30000));
      }
      card.drop = {
        before,
        dropped,
        back: await read(),
        sockets,
        connectionEvents: await page.evaluate(() => window.__fcConn || []),
      };
    }
    // The client's own figures (W5). Read LAST, so whatever the run did
    // above is counted in them.
    if (process.env.FC_PANEL_STATS !== undefined && card) {
      card.stats = await page.evaluate(() => {
        const read = window.fcxPanelStats;
        if (typeof read !== 'function') return { error: 'no fcxPanelStats on the page' };
        const s = read();
        if (!s) return { error: 'the card has no client (is a panel open?)' };
        const ms = (v) => Math.round(v * 10) / 10;
        return {
          firstFrameMs: ms(s.firstFrameMs), paintedMs: ms(s.paintedMs),
          subscribeMs: ms(s.subscribeMs), frames: s.frames, idle: s.idle,
          wakes: s.wakes, applyMs: ms(s.applyMs),
          // The coalescing W2 exists for: 8.4's Sketcher solve is 162
          // pushes on the host's side and must not be 162 reflows here.
          framesPerWake: s.wakes ? Math.round((s.frames / s.wakes) * 10) / 10 : 0,
          // Frames on the lane that belong to the TOOL BAR's mirror and
          // are dropped unread. Before W5 these were applied into the
          // panel's own store: a wide viewport counted 198-396 frames
          // where a phone, whose tool-bar strip never subscribes, counted
          // 9.
          foreign: s.foreign,
          writes: s.writes, writeMs: ms(s.writeMs),
          // W3's locale, which no run could ever see: the host reports it
          // in the subscribe reply, and it is `C` under the gate and under
          // this harness alike unless the SERVE was started in another
          // one. Reported beside the numbers the page formatted with it.
          locale: s.locale, theme: s.theme,
        };
      });
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
  // A panel of BUTTONS is a drawn panel too: demo-messageboxpanel.py's is
  // a button and a label, and calling that empty reported W4's whole
  // dialog stage as a failure while its layers were on screen.
  process.exit(card.fields.length || card.combos.length || drewItems
               || (card.buttons || []).length ? 0 : 1);
})();
