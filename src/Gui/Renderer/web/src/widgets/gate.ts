// The browser gate for the widget walker (docs/Sandbox.md 7.22): replay the
// frames the desktop really sent and assert what the walker must hold true.
//
// The ruled shape (question 5): a PURE plan, no DOM, no new dependency. It
// runs on the only node here, emsdk's 24, which strips TypeScript natively
// (`process.features.typescript === 'strip'`), so this imports the core's
// .ts with no compile step:
//
//   node src/Gui/Renderer/web/src/widgets/gate.ts
//
// The fixtures are recorded by SandboxPanelMirror.py behind
// SANDBOX_PANEL_FIXTURES (7.22, "The fixture corpus"); regenerating them
// needs that gate module alone, since a root's id is a process-wide serial.
//
// tsconfig excludes this file, for one reason: it imports node builtins
// (`node:fs`, `process`) and @types/node is not installed -- adding it
// for a single script would spend the dependency question 5 withheld.
// Running the gate is its check. The `.ts` specifiers below are NOT
// why: `allowImportingTsExtensions` covers those, so the core this
// imports is typechecked normally.

import { readFileSync, readdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { CHECK_ON, CHOOSER_DIRECTORY, CHOOSER_FILE, WidgetStore, acceptFromFilter,
         cssColor, dialogClickOp, dialogRejectOp, fileSelectedOp, isDarkColor,
         isDialogRoot, isPanelMirrorId, itemClickOp, itemEditOp, itemExpandOp,
         layoutRefs, refId, selectionWrite } from './protocol.ts';
import type { Frame } from './protocol.ts';
import { isKnownLayoutClass, planLayout } from './layout.ts';
import type { LayoutPlan } from './layout.ts';
import { filterSet, inputModeFor, setFromGuest, splice, stillApplies, triggerFor, wordAt }
  from './complete.ts';
import type { CompletionSet } from './complete.ts';
import { IconCache, dataUrl, formatNumber, iconKey, iconKind, imageKey, localeTag,
         mouseArgs, parseLocaleNumber, pictureAt, qtButton, qtModifiers,
         wheelArgs } from './images.ts';

interface Fixture {
  case: string;
  subscribeReplies: Record<string, unknown>[];
  frames: { client: number; frame: Frame }[];
}

const here = dirname(fileURLToPath(import.meta.url));
const fixtureDir = join(here, 'fixtures');

let failures = 0;

function check(caseName: string, what: string, ok: boolean, detail = ''): void {
  if (!ok) failures++;
  const mark = ok ? 'PASS' : 'FAIL';
  console.log(`${mark}  ${caseName.padEnd(22)} ${what}${detail ? '  -- ' + detail : ''}`);
}

function replay(fixture: Fixture): void {
  const name = fixture.case;
  const store = new WidgetStore();

  let unknownMethod = '';
  let missingTarget = '';
  let unresolvedRef = '';
  let reopened = 0;
  let peak = 0;

  for (const { frame } of fixture.frames) {
    if (frame.method === 'open') {
      // Children arrive before their container, so every ref a layout
      // names must already be here. This is the one ordering promise the
      // walker builds on: if it held only by luck, a container would
      // render with holes in it.
      for (const ref of layoutRefs(frame.layout)) {
        if (!store.has(ref) && !unresolvedRef) unresolvedRef = `${frame.id} -> ${ref}`;
      }
      if (store.has(frame.id)) reopened++;
    }
    else if (!store.has(frame.id) && frame.method !== 'close') {
      if (!missingTarget) missingTarget = `${frame.method} ${frame.id}`;
    }
    if (!store.apply(frame) && !unknownMethod) unknownMethod = `${frame.method} ${frame.id}`;
    peak = Math.max(peak, store.size);
  }

  check(name, 'every frame applied', !unknownMethod, unknownMethod);
  check(name, 'children before containers', !unresolvedRef, unresolvedRef);
  check(name, 'no patch to an unknown model', !missingTarget, missingTarget);
  // The corpus's own finding: the mirror's list opens twice at subscribe.
  check(name, 're-open tolerated', reopened > 0, `${reopened} re-opened`);
  check(name, 'models were built', peak > 5, `peak ${peak}`);

  // The subscribe reply is the walker's boot state.
  const reply = fixture.subscribeReplies[0] ?? {};
  check(name, 'subscribe reply usable',
        reply.ok === true && 'panel' in reply && 'locale' in reply,
        `locale=${String(reply.locale)}`);
}

/// Every layout in a fixture, planned, with what the plan must hold true.
/// The corpus carries QVBox/QHBox/QGrid/QForm only, and a `pos` that is two
/// wide as often as a form has rows -- both gated here, because a span read
/// out of a missing pos[2] is NaN and a panel would collapse silently.
function checkLayouts(fixture: Fixture): void {
  const name = fixture.case;
  let unknownClass = '';
  let badSpan = '';
  let badKind = '';
  let planned = 0;
  let twoWide = 0;

  const walk = (plan: LayoutPlan): void => {
    planned++;
    if (!isKnownLayoutClass(plan.className) && !unknownClass) unknownClass = plan.className;
    for (const item of plan.items) {
      if (item.kind === 'layout' && item.layout) walk(item.layout);
      if (item.kind === 'spacer' && !item.spacer && !badKind) badKind = 'spacer without extent';
      if (item.kind === 'widget' && !item.id && !badKind) badKind = 'widget without a ref';
      if (plan.kind === 'stack') continue;
      // a placed item must have usable spans: never NaN, never below 1
      if (item.row === undefined) continue;
      const rs = item.rowSpan ?? 0;
      const cs = item.columnSpan ?? 0;
      if (!Number.isFinite(rs) || !Number.isFinite(cs) || rs < 1 || cs < 1) {
        if (!badSpan) badSpan = `${plan.className} r=${item.row} rs=${rs} cs=${cs}`;
      }
    }
  };

  for (const { frame } of fixture.frames) {
    if (frame.layout) {
      for (const item of frame.layout.items ?? []) {
        if (item.pos && item.pos.length === 2) twoWide++;
      }
      walk(planLayout(frame.layout));
    }
    const spec = frame.content?.layoutSpec;
    if (spec && typeof spec === 'object') walk(planLayout(spec as never));
  }

  check(name, 'layout classes known', !unknownClass, unknownClass);
  check(name, 'placed spans usable', !badSpan, badSpan);
  check(name, 'item shapes complete', !badKind, badKind);
  check(name, 'layouts planned', planned > 0, `${planned} planned, ${twoWide} two-wide pos`);
}

function main(): void {
  const files = readdirSync(fixtureDir).filter((f: string) => f.endsWith('.json')).sort();
  if (files.length === 0) {
    console.error(`no fixtures in ${fixtureDir}`);
    process.exit(1);
  }
  for (const file of files) {
    const fixture = JSON.parse(readFileSync(join(fixtureDir, file), 'utf-8')) as Fixture;
    replay(fixture);
    checkLayouts(fixture);
  }

  // Sketcher's list is the item-op case: 21 customs that must leave real
  // rows behind, not an empty tree (8.4: three ops a row, 162 in a solve).
  const sketcher = JSON.parse(
    readFileSync(join(fixtureDir, 'sketcher_constraints.json'), 'utf-8'),
  ) as Fixture;
  const store = new WidgetStore();
  let viewId = '';
  for (const { frame } of sketcher.frames) {
    store.apply(frame);
    if (frame.method === 'custom' && frame.content?.item && !viewId) viewId = frame.id;
  }
  const rows = viewId ? (store.get(viewId)?.items ?? []) : [];
  const texts = rows.flatMap((r) => r.cells.map((c) => c.text)).filter(Boolean);
  check('sketcher_constraints', 'item ops leave rows', rows.length > 0, `${rows.length} rows`);
  check('sketcher_constraints', 'cells carry text', texts.length > 0, String(texts.slice(0, 3)));

  checkItems();
  checkDialogs();
  checkCompletion();
  checkImages();
  checkFileChooser();

  console.log(failures === 0 ? '\nALL GREEN' : `\n${failures} FAILURE(S)`);
  process.exit(failures === 0 ? 0 : 1);
}

/// Dialog roots (docs/Sandbox.md 7.22, W4). Until W4 not one check here
/// looked at a dialog: the six fixtures were replayed for their frames and
/// their layouts, and `nested_messagebox` -- the whole point of which is
/// the dialog -- was gated no differently from a form panel.
///
/// The corpus case is a panel slot's QMessageBox: it arrives as a
/// `dialog:<n>` root BESIDE the task panel's own root, is answered, and
/// goes. Everything asserted below is the host's, not this client's, so a
/// change on either side that breaks the contract fails here.
function checkDialogs(): void {
  const t = 'dialogs';
  const fixture = JSON.parse(
    readFileSync(join(fixtureDir, 'nested_messagebox.json'), 'utf-8'),
  ) as Fixture;
  const upto = (n: number): WidgetStore => {
    const store = new WidgetStore();
    for (const { frame } of fixture.frames.slice(0, n)) store.apply(frame);
    return store;
  };
  /// QMessageBox::StandardButton, the two the corpus was built with.
  const YES = 0x4000;
  const NO = 0x10000;

  const closeAt = fixture.frames.findIndex(
    (f) => f.frame.method === 'close' && isDialogRoot(f.frame.id));
  check(t, 'the corpus closes a dialog root', closeAt > 0, `frame ${closeAt}`);

  // while the box is on screen and the host's slot is blocked in exec()
  const up = upto(closeAt);
  const roots = up.ids().filter(isDialogRoot);
  check(t, 'a dialog root is in the store', roots.length === 1, String(roots));
  const box = up.get(roots[0]);
  check(t, 'the root is a QDialogModel', box?.model === 'QDialogModel', String(box?.model));
  check(t, 'the root keeps its real class', box?.qtClass === 'QMessageBox',
        String(box?.qtClass));
  check(t, 'the root says it is modal', box?.state.modal === true, String(box?.state.modal));
  check(t, 'the root carries its title', box?.state.windowTitle === 'Really',
        String(box?.state.windowTitle));
  check(t, 'the root hangs off the list, not the panel root',
        box?.parent === 'panel', String(box?.parent));

  // the list container's layout is the show order the layer stacks by
  const order = planLayout(up.get('panel')?.layout).items
    .map((it) => it.id).filter((id): id is string => !!id);
  check(t, 'the list holds both roots in show order',
        order.length === 2 && order[0].startsWith('panel:') && isDialogRoot(order[1]),
        String(order));

  // Qt's own message box parts, which M1's `qt_` rule skips as machinery
  // and M3 un-skips as content -- so their absence here would be silent
  const models = up.ids().map((id) => up.get(id)).filter((m): m is NonNullable<typeof m> => !!m);
  const labels = models.filter((m) => m.model === 'QLabelModel');
  check(t, "the box's text arrived", labels.some((m) => m.state.text === 'Proceed?'),
        String(labels.map((m) => m.state.text)));
  check(t, 'the box icon arrived as a picture',
        labels.filter((m) => String(m.state.pixmap ?? '').startsWith('img:')).length === 1);

  const flags = models
    .filter((m) => m.model === 'QPushButtonModel')
    .map((m) => m.state.standardButton)
    .filter((f): f is number => typeof f === 'number' && f !== 0);
  check(t, 'the buttons carry their standard flags',
        flags.includes(YES) && flags.includes(NO), String(flags));

  // the answer: the flag, through the ROOT. The host turns it into
  // done(button), which is the exec code the blocked slot returns.
  check(t, 'an answer names the button flag',
        JSON.stringify(dialogClickOp(YES)) === '{"event":"clicked","args":[16384]}',
        JSON.stringify(dialogClickOp(YES)));
  check(t, 'Escape rejects', JSON.stringify(dialogRejectOp()) === '{"event":"reject"}');

  // and what the close leaves behind
  const after = upto(closeAt + 1);
  check(t, 'the dialog root goes when the box closes',
        after.ids().filter(isDialogRoot).length === 0);
  check(t, 'the panel root outlives the dialog',
        after.ids().some((id) => id.startsWith('panel:')));
  // M3 sends ONE close, for the root, and leaves the subtree silent, so the
  // box's inner widgets stay in the store. Nothing draws them (the card
  // renders `panel:<n>` and `dialog:<n>` roots only), but they are still
  // held -- recorded here so that a host that starts pruning, or a client
  // that starts leaking them onto the screen, shows up as a change.
  check(t, 'the closed box leaves its subtree in the store (M3, by design)',
        after.ids().some((id) => id.startsWith('pw:')));
}

/// The item views (docs/Sandbox.md 7.22, W2). The recorded corpus carries
/// clear/insert/set with text and icon cells and nothing else -- Sketcher's
/// list never nests, never checks and never deletes a row -- so what a real
/// tree does is CONSTRUCTED here against the same store. The op names and
/// their payloads are the host's own (`Fw::ItemView::applyItemOp`), which
/// is the only reason this can stand in for a fixture.
function checkItems(): void {
  const t = 'item views';
  const store = new WidgetStore();
  store.apply({
    method: 'open', id: 'v', model: 'QTreeWidgetModel', qtClass: 'QTreeWidget',
    state: { q_columns: ['A', 'B'] },
  } as Frame);
  const custom = (content: Record<string, unknown>): boolean =>
    store.apply({ method: 'custom', id: 'v', content } as Frame);
  const rows = () => store.get('v')?.items ?? [];

  custom({ item: 'insert', parent: 0, index: 0, rows: [
    { id: 1, cells: [{ text: 'one' }, { check: 0 }], children: [{ id: 2, cells: [{ text: 'kid' }] }] },
    { id: 3, cells: [{ text: 'two' }] },
  ] });
  check(t, 'a nested insert keeps the tree',
        rows().length === 2 && rows()[0].children?.length === 1,
        `${rows().length} top`);

  check(t, 'a row op expands',
        custom({ item: 'row', id: 1, row: { expanded: true } }) && rows()[0].expanded === true);
  check(t, 'a row op hides',
        custom({ item: 'row', id: 3, row: { hidden: true } }) && rows()[1].hidden === true);
  check(t, 'a row op reaches a nested row',
        custom({ item: 'row', id: 2, row: { flags: 32 } })
        && rows()[0].children?.[0].flags === 32);

  custom({ item: 'set', id: 1, col: 0, cell: { icon: 'img:x' } });
  check(t, 'a set merges rather than replaces',
        rows()[0].cells[0].text === 'one' && rows()[0].cells[0].icon === 'img:x',
        JSON.stringify(rows()[0].cells[0]));

  // Cell colours (W5, paying off W2's debt). The host packs a QColor as
  // four floats 0..1 -- `colorList` in FwQtView.cpp -- and the view left
  // them undrawn rather than guess that. These pin the packing, so a host
  // that ever changed it fails here instead of painting a panel's red
  // warning in some other colour.
  custom({ item: 'set', id: 3, col: 0, cell: { fg: [1, 0, 0, 1], bg: [1, 0.878, 0.51, 1] } });
  const painted = rows().find((r) => r.id === 3)?.cells[0];
  check(t, 'a colour survives the store as the host packed it',
        JSON.stringify(painted?.fg) === JSON.stringify([1, 0, 0, 1]),
        JSON.stringify(painted));
  check(t, 'four floats become a CSS colour',
        cssColor([1, 0, 0, 1]) === 'rgba(255, 0, 0, 1)', String(cssColor([1, 0, 0, 1])));
  check(t, 'a half alpha is an alpha, not a channel',
        cssColor([0, 0, 0, 0.5]) === 'rgba(0, 0, 0, 0.5)', String(cssColor([0, 0, 0, 0.5])));
  check(t, 'three floats are a colour too (Qt sends alpha, but the type says may)',
        cssColor([0, 0.5, 1]) === 'rgba(0, 128, 255, 1)', String(cssColor([0, 0.5, 1])));
  check(t, 'no colour is no colour, not black',
        cssColor(undefined) === undefined && cssColor([]) === undefined);
  // The contrast rule: a light wash with no foreground beside it must not
  // leave the card's pale text on it.
  check(t, 'a yellow wash asks for dark text', !isDarkColor([1, 0.878, 0.51, 1]));
  check(t, 'a navy wash asks for pale text', isDarkColor([0.05, 0.08, 0.3, 1]));

  check(t, 'a remove takes its children with it',
        custom({ item: 'remove', id: 1 }) && rows().length === 1 && rows()[0].id === 3,
        `${rows().length} left`);

  custom({ item: 'insert', parent: 0, index: 0, rows: [{ id: 4, cells: [{ text: 'aaa' }] }] });
  check(t, 'sort orders one level by the column text',
        custom({ item: 'sort', id: 0, col: 0, order: 0 })
        && (rows()[0].cells[0].text ?? '') === 'aaa',
        rows().map((r) => r.cells[0]?.text).join(','));
  check(t, 'sort descending reverses it',
        custom({ item: 'sort', id: 0, col: 0, order: 1 })
        && (rows()[0].cells[0].text ?? '') === 'two',
        rows().map((r) => r.cells[0]?.text).join(','));

  // Whose frame is it? Both mirrors push down the one `widgets` lane, and
  // a panel client that applies the tool bar's frames builds a store it
  // can never draw. The rule is the host's own (`PanelMirror::owns`), so
  // these pin the spellings on both sides of it.
  check(t, 'the list container is the panel mirror\'s', isPanelMirrorId('panel'));
  check(t, 'a panel root is', isPanelMirrorId('panel:2'));
  check(t, 'a dialog root is', isPanelMirrorId('dialog:1'));
  check(t, 'a mirrored widget is', isPanelMirrorId('pw:37'));
  check(t, 'a tool bar widget is NOT', !isPanelMirrorId('widget:File#0'));
  check(t, 'a tool bar action is NOT', !isPanelMirrorId('action:Std_New#1'));
  check(t, 'a name that merely starts like one is not a root',
        !isPanelMirrorId('panels') && !isPanelMirrorId('dialogue:1'));

  check(t, 'an unknown item op is refused', !custom({ item: 'wat' }));
  check(t, 'a set on a row that is gone is refused',
        !custom({ item: 'set', id: 99, col: 0, cell: { text: 'x' } }));
  // An event is not an item op: the store reports it and changes nothing.
  check(t, 'an event frame still applies', custom({ event: 'itemClicked', args: [3, 0] }));

  // The write path, and the distinction the whole thing turns on: what a
  // client sends must be an item OP. `commCustom` hands an `item` to
  // `applyItemOp`, which calls `emitItemOp` and so reaches the desktop's
  // real widget and the panel slot behind it; an `event` reaches
  // `dispatchEvent`, which updates the model's own copy and calls no
  // backend -- a write that passes every check in this file and does
  // nothing on screen. (`itemEdited` goes the other way: the backend
  // emits it when the DESKTOP user edits a cell.) The host's own test
  // writes a check this way and asserts the constraint really moves --
  // Mod/Test/SandboxPanelMirror.py `test_sketcher_constraints`.
  const edit = itemEditOp(7, 1, { check: CHECK_ON });
  check(t, 'a check write is an item op, not an event',
        edit.item === 'set' && !('event' in edit), JSON.stringify(edit));
  check(t, 'the check write names the row, the column and the cell',
        edit.id === 7 && edit.col === 1
        && JSON.stringify(edit.cell) === JSON.stringify({ check: 2 }),
        JSON.stringify(edit));
  const expand = itemExpandOp(7, true);
  check(t, 'an expand is the row op the real tree follows',
        expand.item === 'row' && !('event' in expand)
        && JSON.stringify(expand.row) === JSON.stringify({ expanded: true }),
        JSON.stringify(expand));
  check(t, 'a click stays an event: it notifies, it does not select',
        itemClickOp(7, 0).event === 'itemClicked');

  // What a client sends is the shape the host pushes, so the same store
  // applies it -- the cheapest proof that the two halves speak one wire.
  const back = new WidgetStore();
  back.apply({ method: 'open', id: 'w', model: 'QTreeWidgetModel', state: {} } as Frame);
  back.apply({
    method: 'custom', id: 'w',
    content: { item: 'insert', parent: 0, index: 0, rows: [{ id: 7, cells: [{}, { check: 0 }] }] },
  } as Frame);
  check(t, 'a client op is the shape the store already applies',
        back.apply({ method: 'custom', id: 'w', content: edit } as Frame)
        && back.get('w')?.items?.[0]?.cells?.[1]?.check === CHECK_ON,
        JSON.stringify(back.get('w')?.items));
  const sel = selectionWrite([7], 7, 1);
  check(t, 'selection is state, never an event',
        !('event' in sel) && JSON.stringify(sel.selection) === '[7]'
        && sel.currentId === 7 && sel.currentColumn === 1,
        JSON.stringify(sel));
}

/// The completion rules (docs/Sandbox.md 7.23). No fixture: these are
/// decisions about typing, and the ones that matter are the phone's --
/// a decimal point must not trigger, and a decimal keyboard has no
/// letters. Both are invisible on a desktop, so they are asserted here
/// rather than discovered on a handset.
function checkCompletion(): void {
  const c = 'completion';
  check(c, 'a dot after a name triggers', triggerFor('Pad.', 4) === 'dot');
  check(c, 'a dot after a digit does NOT', triggerFor('10.', 3) === null,
        String(triggerFor('10.', 3)));
  check(c, 'a dot after a close bracket triggers',
        triggerFor('Sketch.Constraints[0].', 22) === 'dot');
  check(c, 'two word characters arm a request', triggerFor('Pa', 2) === 'word');
  check(c, 'one does not', triggerFor('P', 1) === null);
  check(c, 'a bare number does not', triggerFor('10', 2) === null);
  check(c, 'the word at the caret is the member',
        wordAt('Other.Wid', 9) === 'Wid', wordAt('Other.Wid', 9));

  const set: CompletionSet = {
    text: 'Other.', pos: 6, start: 0, end: 6,
    items: ['Other.Width', 'Other.Placement', 'Other.Label'],
    details: ['', '', ''],
  };
  check(c, 'the set serves the same segment', stillApplies(set, 'Other.Wid', 9));
  check(c, 'another dot voids it', !stillApplies(set, 'Other.Width.', 12));
  check(c, 'an edit before it voids it', !stillApplies(set, 'Othen.Wid', 9));
  check(c, 'local filtering narrows',
        filterSet(set, 'Other.Wid', 9).join(',') === 'Other.Width',
        filterSet(set, 'Other.Wid', 9).join(','));
  check(c, 'a prefix match outranks a substring',
        filterSet(set, 'Other.La', 8)[0] === 'Other.Label',
        filterSet(set, 'Other.La', 8).join(','));

  // What the user typed while the answer was in flight is part of the
  // name being replaced -- getting this wrong leaves `Other.WidOther.Width`.
  const picked = splice(set, 'Other.Width', 'Other.Wid', 9);
  check(c, 'splicing replaces what was typed since',
        picked.text === 'Other.Width' && picked.pos === 11,
        `${picked.text}@${picked.pos}`);
  const inline = splice({ ...set, start: 1, end: 7, text: '=Other.' },
                        'Other.Width', '=Other.Wid', 10);
  check(c, 'splicing keeps the lead char',
        inline.text === '=Other.Width', inline.text);

  check(c, 'a quantity field offers a decimal keyboard',
        inputModeFor('10.00', true) === 'decimal');
  check(c, 'an expression turns it into a text keyboard',
        inputModeFor('=Pad', true) === 'text');
  check(c, 'a plain field is always text', inputModeFor('10', false) === 'text');

  // The Python console's guest (sandbox/console.py) answers `start` but no
  // `end`: its rlcompleter runs on the source up to the caret. The adapter
  // fills `end` in from that caret, and everything the controller does
  // afterwards -- narrowing, splicing -- rests on it being right. Asserted
  // here because the console's completion was readline-shaped until a
  // handset showed (2026-09-17) that it never listed, never narrowed and
  // never came up on its own.
  const guest = setFromGuest('App.', 4, ['App.ActiveDocument', 'App.Units'], 0);
  check(c, 'a guest answer ends at the caret',
        guest.start === 0 && guest.end === 4, `${guest.start}..${guest.end}`);
  check(c, 'a guest answer serves the next characters',
        stillApplies(guest, 'App.Ac', 6));
  check(c, 'a guest answer narrows locally',
        filterSet(guest, 'App.Ac', 6).join(',') === 'App.ActiveDocument',
        filterSet(guest, 'App.Ac', 6).join(','));
  const gpick = splice(guest, 'App.ActiveDocument', 'App.Ac', 6);
  check(c, 'a guest completion replaces the whole dotted name',
        gpick.text === 'App.ActiveDocument' && gpick.pos === 18,
        `${gpick.text}@${gpick.pos}`);

  // rlcompleter answers a first segment with the bare name, from 0 -- and
  // appends a space to a keyword, which is part of what it means.
  const bare = setFromGuest('im', 2, ['import '], 0);
  check(c, 'a bare-name answer splices whole',
        splice(bare, 'import ', 'im', 2).text === 'import ',
        splice(bare, 'import ', 'im', 2).text);
  check(c, 'a start past the caret is clamped',
        setFromGuest('ab', 1, [], 9).start === 1,
        String(setFromGuest('ab', 1, [], 9).start));
}

/// Pictures, icons, theme and locale (docs/Sandbox.md 7.22, W3).
///
/// The fixture half is the one the sizing ruled: `svg_picture` carries a
/// QSvgWidget's grab and two button icons, and the rule is that the host is
/// asked ONCE per distinct id however many widgets name it. The rest are
/// decisions about formatting and about the pointer, which no fixture can
/// hold: the host replays the mouse into a real widget, so the argument
/// ORDER and Qt's own numbering are what make the difference between a
/// click landing and nothing happening.
/// The file chooser (docs/Sandbox.md 7.22, W4b).
///
/// There is NO fixture: not one panel in the corpus carries a
/// `Gui::FileChooser`, so the model is constructed against the same store
/// the host's own frames drive -- the way `checkItems` constructs the
/// trees Sketcher never sends. What is asserted is the host's contract:
/// the bag keys `Fw::FileChooser` declares (Gui/Fw/FwWidgets.cpp), and
/// the request name `View::onRequest` answers to.
function checkFileChooser(): void {
  const t = 'file chooser';
  const store = new WidgetStore();
  store.apply({
    method: 'open', id: 'pw:9', model: 'FileChooserModel', qtClass: 'Gui::FileChooser',
    parent: 'IPY_MODEL_panel:1',
    state: {
      q_objectName: 'fontFile', q_fileName: '/home/u/a.ttf', q_mode: CHOOSER_FILE,
      q_acceptMode: 0, q_buttonText: '', q_filter: 'Fonts (*.ttf *.otf);;All files (*)',
    },
  } as Frame);
  const chooser = store.get('pw:9');
  check(t, 'a chooser is a leaf of its own class',
        chooser?.model === 'FileChooserModel' && chooser?.qtClass === 'Gui::FileChooser',
        `${chooser?.model} / ${chooser?.qtClass}`);
  check(t, 'the path crosses as data', chooser?.state.fileName === '/home/u/a.ttf',
        String(chooser?.state.fileName));
  check(t, 'the filter crosses', String(chooser?.state.filter ?? '').startsWith('Fonts'),
        String(chooser?.state.filter));

  // The host's own write reaches the field: a slot that corrects or
  // completes the path is the origin echo of every other leaf.
  store.apply({ method: 'update', id: 'pw:9', content: { q_fileName: '/tmp/up/b.ttf' } } as Frame);
  check(t, "the host's own change reaches the field",
        store.get('pw:9')?.state.fileName === '/tmp/up/b.ttf',
        String(store.get('pw:9')?.state.fileName));

  // The write path, and the whole reason it is not a property write: a
  // panel's slot is connected to fileNameSelected, which a `q_fileName`
  // update does not fire.
  check(t, 'a pick is a request, not a value write',
        JSON.stringify(fileSelectedOp('/tmp/up/b.ttf'))
          === '{"event":"fileSelected","args":["/tmp/up/b.ttf"]}',
        JSON.stringify(fileSelectedOp('/tmp/up/b.ttf')));

  // A directory chooser names a folder ON THE HOST, which no browser
  // picker can answer and which may not be browsed either.
  store.apply({ method: 'update', id: 'pw:9', content: { q_mode: CHOOSER_DIRECTORY } } as Frame);
  check(t, 'a directory chooser is recognized',
        store.get('pw:9')?.state.mode === CHOOSER_DIRECTORY,
        String(store.get('pw:9')?.state.mode));

  // Qt's name filter as the picker's `accept`.
  check(t, "a Qt filter becomes the web's extensions",
        acceptFromFilter('Fonts (*.ttf *.otf);;All files (*)') === '.ttf,.otf',
        acceptFromFilter('Fonts (*.ttf *.otf);;All files (*)'));
  check(t, 'an all-files filter accepts everything, not nothing',
        acceptFromFilter('All files (*)') === '',
        `"${acceptFromFilter('All files (*)')}"`);
  check(t, 'no filter accepts everything', acceptFromFilter('') === '');
  check(t, 'an extension is offered once',
        acceptFromFilter('A (*.svg);;B (*.SVG)') === '.svg',
        acceptFromFilter('A (*.svg);;B (*.SVG)'));
  check(t, 'a compound suffix survives',
        acceptFromFilter('Meshes (*.stl *.obj *.step)') === '.stl,.obj,.step',
        acceptFromFilter('Meshes (*.stl *.obj *.step)'));
}

function checkImages(): void {
  const t = 'images';

  check(t, 'an img: id is fetched by content', iconKind('img:abc') === 'image');
  check(t, 'a bare name is fetched by theme', iconKind('Std_ViewFitAll') === 'name');
  check(t, 'an empty value names no picture', iconKind('') === 'none');
  check(t, 'a missing value names no picture', iconKind(undefined) === 'none');

  check(t, 'an svg reply is percent-encoded, not base64',
        (dataUrl({ format: 'svg', data: '<svg fill="#f00"/>' }) ?? '')
          .startsWith('data:image/svg+xml;charset=utf-8,%3Csvg'),
        String(dataUrl({ format: 'svg', data: '<svg fill="#f00"/>' })).slice(0, 48));
  check(t, 'a png reply is a base64 data url',
        dataUrl({ format: 'png', data: 'AAA' }) === 'data:image/png;base64,AAA');
  check(t, 'an empty reply resolves to nothing', dataUrl({ format: 'png', data: '' }) === null);

  // The C locale is not English: it means unformatted, and formatting it
  // as English would put thousands separators into numbers the host
  // prints without them.
  check(t, 'the C locale is no locale', localeTag('C') === null);
  check(t, 'a Qt locale becomes a web tag', localeTag('en_US') === 'en-US');
  check(t, 'an encoding suffix is dropped', localeTag('de_DE.UTF-8') === 'de-DE',
        String(localeTag('de_DE.UTF-8')));
  check(t, 'no locale is no locale', localeTag('') === null);

  check(t, 'no locale formats plainly', formatNumber(10, 2, null) === '10.00',
        formatNumber(10, 2, null));
  check(t, 'a locale groups and decimates its own way',
        formatNumber(1234.567, 2, 'de-DE') === '1.234,57',
        formatNumber(1234.567, 2, 'de-DE'));

  // The round trip is the point: what the field SHOWS has to parse back,
  // or a write sends the host a text where it wanted a number.
  for (const locale of [null, 'en-US', 'de-DE']) {
    const shown = formatNumber(1234.5, 2, locale);
    check(t, `what ${locale ?? 'C'} shows parses back`,
          Math.abs(parseLocaleNumber(shown, locale) - 1234.5) < 1e-9,
          `${shown} -> ${parseLocaleNumber(shown, locale)}`);
  }
  check(t, 'a suffix does not stop it parsing',
        parseLocaleNumber('10.00 mm', null) === 10, String(parseLocaleNumber('10.00 mm', null)));
  check(t, 'an empty field is not a zero', Number.isNaN(parseLocaleNumber('', null)));

  // Qt's buttons are bit values and the DOM's are an index; the modifiers
  // are Qt's own bits. Both are replayed into a real widget, so a wrong
  // number is a click that lands as the wrong button.
  check(t, 'the DOM left button is Qt left', qtButton(0) === 1);
  check(t, 'the DOM middle button is Qt middle', qtButton(1) === 4, String(qtButton(1)));
  check(t, 'the DOM right button is Qt right', qtButton(2) === 2, String(qtButton(2)));
  check(t, 'the modifiers are Qt bits',
        qtModifiers({ shiftKey: true, ctrlKey: true }) === 0x06000000,
        qtModifiers({ shiftKey: true, ctrlKey: true }).toString(16));

  const mouse = mouseArgs('press', 12.4, 7.6, 2, 2, { altKey: true });
  check(t, 'a mouse arg list is [type, x, y, button, buttons, mods]',
        JSON.stringify(mouse) === JSON.stringify(['press', 12, 8, 2, 2, 0x08000000]),
        JSON.stringify(mouse));
  // A DOM wheel delta is positive downward and Qt's is positive upward.
  const wheel = wheelArgs(1, 2, 0, 100, 0, {});
  check(t, 'a wheel notch is flipped and quantized',
        JSON.stringify(wheel) === JSON.stringify([1, 2, 0, -120, 0, 0]),
        JSON.stringify(wheel));

  const at = pictureAt({ left: 10, top: 20, width: 32, height: 32 },
                       { width: 64, height: 64 }, 26, 36);
  check(t, 'a pointer maps into the picture own pixels',
        at.x === 32 && at.y === 32, JSON.stringify(at));

  // The ruled fixture check. Every picture the wire named, in order, then
  // resolved through one cache: the host must be asked once per DISTINCT
  // id, however many widgets carry it and however often it is re-sent.
  const fixture = JSON.parse(
    readFileSync(join(fixtureDir, 'svg_picture.json'), 'utf-8'),
  ) as Fixture;
  const named: string[] = [];
  for (const { frame } of fixture.frames) {
    for (const bag of [frame.state, frame.content]) {
      if (!bag) continue;
      for (const key of ['q_icon', 'q_pixmap', 'q_windowIcon']) {
        const value = (bag as Record<string, unknown>)[key];
        if (iconKind(value) !== 'none') named.push(value as string);
      }
    }
  }
  const distinct = new Set(named);
  check(t, 'the fixture carries a picture and its buttons icons',
        distinct.size >= 3, `${named.length} named, ${distinct.size} distinct`);

  const cache = new IconCache();
  const reply = () => Promise.resolve({ format: 'png', data: 'AAA' });
  // TWICE, because that is what the card does: every repaint resolves
  // every picture on it again, and the fixture happens to name each of its
  // ids once, so a single pass would prove nothing about the cache.
  for (const pass of [0, 1]) {
    void pass;
    for (const name of named) void cache.resolve(imageKey(name), reply);
  }
  check(t, 'the image op is asked once per distinct id',
        cache.asked === distinct.size, `asked ${cache.asked} for ${distinct.size} ids`);
  // A repaint that changed something sends a NEW id, and that one is
  // fetched: the cache must not be a "fetched once, never again" rule.
  const grown = cache.asked;
  void cache.resolve(imageKey('img:something-new'), reply);
  check(t, 'a changed picture is a new id and a new fetch',
        cache.asked === grown + 1, `asked ${cache.asked}`);
  // Asking for one already in hand adds nothing, whoever asks.
  void cache.resolve(imageKey([...distinct][0]), reply);
  check(t, 'a second widget naming the same id adds no fetch',
        cache.asked === grown + 1, `asked ${cache.asked}`);

  // A NAMED icon is not content-addressed: the same name is different
  // bytes under another icon theme, and a PNG is rasterized at the size it
  // was asked for. Both belong in the key, or a theme change would serve
  // the old icons for as long as the page stayed up.
  const themed = new IconCache();
  void themed.resolve(iconKey('', 'Std_ViewFitAll', 16), reply);
  void themed.resolve(iconKey('', 'Std_ViewFitAll', 16), reply);
  check(t, 'a named icon is fetched once per theme and size',
        themed.asked === 1, `asked ${themed.asked}`);
  void themed.resolve(iconKey('dark', 'Std_ViewFitAll', 16), reply);
  check(t, 'another theme is other bytes', themed.asked === 2, `asked ${themed.asked}`);
  void themed.resolve(iconKey('dark', 'Std_ViewFitAll', 24), reply);
  check(t, 'another size is another fetch', themed.asked === 3, `asked ${themed.asked}`);
}

main();
