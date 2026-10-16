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
         layoutRefs, panelButtonOp, refId, selectionWrite,
         BUTTON_CANCEL, BUTTON_CLOSE, BUTTON_OK } from './protocol.ts';
import type { Frame } from './protocol.ts';
import { actRefusal, branchNameProblem, deletable, logAuthor, logOpText, logRefusal, logWhen,
         mayMove, mergeNewest, moveWhy, pickRows, requestActs, requestProblem, requestState,
         requestText, rowBranchWhy, rowUndoable, rowUndoWhy, rowsAppliable, sortRequests,
         switchable, undoLabel, versionText, versionWhy,
         changeName, changePicked, changeTakes, picksOf, previewText, sortChanges,
         unpickedConflicts, keepText, keptCountText, keptLines, keptObjectText, keptRowText,
         keptTabText, opKeepable, opKeptText, changeLeavable, changeLeftOut, keeperText,
         keepWhy, branchRead, bringWhy, importText, importWho, sentFileText, sizeText,
         squashChoices, trimWhy, layoutGraph, branchColour } from '../logmodel.ts';
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

  // a TASK PANEL's box: its own buttons, each asking what its flag means
  const asks = (flag: number): string => JSON.stringify(panelButtonOp(flag));
  check(t, "a task's OK accepts",
        asks(BUTTON_OK) === '{"op":{"event":"accept"},"leaves":false}', asks(BUTTON_OK));
  check(t, "a task's Close leaves, and a viewer may press it",
        asks(BUTTON_CLOSE) === '{"op":{"event":"reject"},"leaves":true}', asks(BUTTON_CLOSE));
  check(t, "a task's Cancel leaves",
        asks(BUTTON_CANCEL) === '{"op":{"event":"reject"},"leaves":true}', asks(BUTTON_CANCEL));
  check(t, "a task's Apply is clicked by its flag",
        asks(0x2000000) === '{"op":{"event":"clicked","args":[33554432]},"leaves":false}',
        asks(0x2000000));
  check(t, 'a button with no flag is its own click',
        panelButtonOp(0).op === undefined && !panelButtonOp(0).leaves);

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

/// The transaction log's panel (logpanel.tsx, docs/TransactionLog.md sec
/// 32.21): what it says of a row and an op, and which rows it keeps when
/// the host answers again.
function checkLog(): void {
  const t = 'log panel';
  const row = (seq: number, extra: Record<string, unknown> = {}): any => ({
    seq, parent: seq - 1, kind: 'user', origin: 'gui', name: `row ${seq}`, time: 0,
    author: 'host', authorKind: 'local', branch: 1, branchName: 'main', ops: 1,
    here: true, mine: true, within: true, ...extra });
  const seqs = (rows: Array<{ seq: number }>): string => rows.map((r) => r.seq).join(',');

  // the newest page again, with older ones loaded under it
  const old = [row(9), row(8), row(7), row(6), row(5), row(4)];
  const page = [row(11), row(10), row(9), row(8)];
  check(t, 'the newest page goes on top, the older ones stay under it',
        seqs(mergeNewest(old, page, true)) === '11,10,9,8,7,6,5,4',
        seqs(mergeNewest(old, page, true)));
  check(t, 'on another branch the older ones are not kept',
        seqs(mergeNewest(old, page, false)) === '11,10,9,8',
        seqs(mergeNewest(old, page, false)));
  check(t, 'a page with no rows is no rows', mergeNewest(old, [], true).length === 0);
  check(t, "what the page says of a row replaces what was said of it",
        mergeNewest([row(9, { here: true })], [row(9, { here: false })], true)[0].here === false);

  check(t, "the desktop's user stands alone", logAuthor(row(1)) === 'host', logAuthor(row(1)));
  check(t, 'a name nothing verified says so',
        logAuthor(row(1, { author: 'ann', authorKind: 'invited' })) === 'ann (invited)');
  check(t, 'a verified name stands alone',
        logAuthor(row(1, { author: 'ann', authorKind: 'verified' })) === 'ann');

  const noon = new Date(2026, 9, 13, 12, 0, 0);
  const sameDay = new Date(2026, 9, 13, 9, 5, 7).getTime() / 1000;
  const dayBefore = new Date(2026, 9, 12, 9, 5, 7).getTime() / 1000;
  check(t, 'a row of today says its time', logWhen(sameDay, noon).length > 0
        && logWhen(dayBefore, noon).length > logWhen(sameDay, noon).length,
        `${logWhen(sameDay, noon)} | ${logWhen(dayBefore, noon)}`);
  check(t, 'a row with no time says nothing', logWhen(0, noon) === '');

  // The graph (sec 32.35): main 1-2-3-6, side forked at 2 with 4-5, merged
  // into main at 7
  const g = [row(7, { parent: 6, merged: 5, kind: 'merge' }), row(6, { parent: 3 }),
             row(5, { parent: 4, branch: 2, branchName: 'side', here: false }),
             row(4, { parent: 2, branch: 2, branchName: 'side', here: false }),
             row(3), row(2), row(1, { parent: 0 })];
  const heads = [{ name: 'main', head: 7, current: true }, { name: 'side', head: 5, current: false }];
  const at = (l: ReturnType<typeof layoutGraph>, seq: number) => l.rows.get(seq)!;
  const lanes = (ls: Array<{ lane: number; to: number }>) =>
    ls.map((x) => `${x.lane}>${x.to}`).join(',');
  const whole = layoutGraph(g, g, heads);
  check(t, 'graph: two lanes, the merge a lane from its node down to the row merged in',
        whole.lanes === 2 && lanes(at(whole, 7).out) === '0>1' && at(whole, 7).out[0].branch === 2,
        `${whole.lanes} ${lanes(at(whole, 7).out)}`);
  check(t, "graph: the side's rows on their own lane, main's on the first",
        at(whole, 5).lane === 1 && at(whole, 4).lane === 1 && at(whole, 6).lane === 0
        && at(whole, 3).lane === 0, [7, 6, 5, 4, 3, 2, 1].map((s) => at(whole, s).lane).join(''));
  check(t, 'graph: the fork, both lanes converging on the row they part from',
        at(whole, 2).lane === 0 && lanes(at(whole, 2).top) === '0>0,1>0'
        && lanes(at(whole, 2).bottom) === '0>0', lanes(at(whole, 2).top));
  check(t, 'graph: the first row ends its lane', lanes(at(whole, 1).bottom) === ''
        && lanes(at(whole, 1).top) === '0>0');
  check(t, 'graph: each head labelled, the current one said',
        at(whole, 7).heads.join() === 'main' && at(whole, 7).current
        && at(whole, 5).heads.join() === 'side' && !at(whole, 5).current
        && at(whole, 6).heads.length === 0);
  const noRecord = g.map((r) => (r.seq === 3 ? { ...r, ops: 0 } : r));
  const filtered = layoutGraph(noRecord, noRecord.filter((r) => r.ops > 0), heads);
  check(t, 'graph: a row left out, the one above it goes on to its nearest shown ancestor',
        !filtered.rows.has(3) && lanes(at(filtered, 2).top) === '0>0,1>0',
        lanes(at(filtered, 2).top));
  check(t, 'graph: a record is drawn hollow', layoutGraph(noRecord, noRecord).rows.get(3)!.record);
  const firstPage = g.slice(0, 4);
  const paged = layoutGraph(firstPage, firstPage, heads);
  check(t, 'graph: a parent not loaded yet, the lane goes on below',
        lanes(at(paged, 4).bottom) === '0>0,1>1', lanes(at(paged, 4).bottom));
  check(t, "graph: a branch's colour is the desktop's",
        branchColour(1) === '#1f77b4' && branchColour(2) === '#d65f0e' && branchColour(9) === '#1f77b4');

  const op = (extra: Record<string, unknown>): any => ({
    idx: 0, op: 'set', kind: 'obj', prop: 'Height', type: 'App::PropertyLength',
    cid: 7, obj: 'Box', label: 'The box', ...extra });
  check(t, 'a set names the object by its label and the property',
        logOpText(op({})) === 'The box: Height', logOpText(op({})));
  check(t, 'an object made', logOpText(op({ op: 'create', prop: '' })) === 'The box made');
  check(t, 'an object that is gone is known by the name the log has',
        logOpText(op({ op: 'remove', prop: '', label: undefined })) === 'Box removed');
  check(t, 'one with no name by its number',
        logOpText(op({ label: undefined, obj: undefined })) === '#7: Height');
  check(t, "the document's own", logOpText(op({ kind: 'doc', prop: 'Comment' }))
        === 'document: Comment');

  check(t, 'a viewer is told why', logRefusal({ code: 'ViewOnly' }).includes('right to edit'));
  check(t, 'a document with no log says so', logRefusal({ code: 'NoLog' }).includes('no history'));
  check(t, 'anything else says what the host said',
        logRefusal({ code: 'LogError', message: 'boom' }) === 'boom');

  // acting on what is listed (sec 32.22)
  const host = { host: true, write: false };
  const reads = { host: false, write: false };
  const writes = { host: false, write: true };
  check(t, 'a row made by hand, here, that changed something can be taken back',
        rowUndoable(row(5), host));
  check(t, 'a record cannot', !rowUndoable(row(5, { ops: 0 }), host));
  check(t, 'a recompute cannot', !rowUndoable(row(5, { kind: 'recompute' }), host));
  check(t, "another branch's row cannot", !rowUndoable(row(5, { here: false }), host));
  check(t, 'an undo can be, as any step', rowUndoable(row(5, { kind: 'undo' }), host));

  // what the one who asks may (sec 32.25)
  const theirs = row(5, { mine: false, author: 'ann' });
  check(t, "the host takes back anyone's row", rowUndoable(theirs, host));
  check(t, 'a client its own, whether it writes the branch or reads it',
        rowUndoable(row(5), reads) && rowUndoable(row(5), writes));
  check(t, "not another's, and is told whose it is",
        !rowUndoable(theirs, reads) && !rowUndoable(theirs, writes)
        && rowUndoWhy(theirs, reads).includes('ann'), rowUndoWhy(theirs, reads));
  const before = row(3, { within: false });
  check(t, 'its own from before the point it writes from is still its own to take back',
        rowUndoable(before, writes) && rowUndoWhy(before, writes) === '');
  check(t, 'a row that is no step says nothing of who',
        rowUndoWhy(row(5, { ops: 0 }), reads) === '');
  check(t, 'the host may move the document, and one who writes its branch',
        mayMove(host) && mayMove(writes) && !mayMove(reads));
  check(t, 'one who reads is told what it needs',
        moveWhy(reads).includes('write access') && moveWhy(host) === ''
        && moveWhy(writes) === '');
  check(t, 'one who writes, not from before the point',
        moveWhy(writes, false).includes('before') && moveWhy(host, false) === '');
  check(t, 'a branch is made from a row the document may be moved from',
        rowBranchWhy(row(5), writes) === '' && rowBranchWhy(before, writes) !== ''
        && rowBranchWhy(row(5), reads) !== '' && rowBranchWhy(before, host) === '');
  const br = (name: string, extra: Record<string, unknown> = {}): any => ({
    id: 1, name, head: 1, fromVersion: 0, fromSeq: 0, time: 0, closed: false, current: false,
    write: false, made: false, ...extra });
  const all = [br('main'), br('given', { write: true }), br('try', { write: true, made: true }),
               br('shut', { write: true, closed: true }),
               br('now', { current: true, write: true })];
  const names = (list: Array<{ name: string }>): string => list.map((b) => b.name).join(',');
  check(t, 'the host deletes any branch but the one the document is on',
        names(deletable(all, host)) === 'main,given,try,shut', names(deletable(all, host)));
  check(t, 'anyone else those of its own making, not one it was given',
        names(deletable(all, writes)) === 'try', names(deletable(all, writes)));
  check(t, 'one who writes switches to the branches it writes, not to a closed one',
        names(all.filter((b) => switchable(b, writes))) === 'given,try,now',
        names(all.filter((b) => switchable(b, writes))));
  check(t, 'one who reads switches to none', names(all.filter((b) => switchable(b, reads)))
        === 'now');
  check(t, 'the host to any that is open',
        names(all.filter((b) => switchable(b, host))) === 'main,given,try,now');
  // Sec 32.30: a branch not read is listed by its name, greyed, and offered
  // for nothing
  const unread = br('theirs', { read: false });
  check(t, 'a branch not read is listed greyed, and switched to by no one but the host',
        !branchRead(unread) && branchRead(br('main')) && branchRead(br('x', { read: true }))
        && !switchable(unread, writes) && !switchable(unread, reads)
        && names(deletable([unread], writes)) === '');
  check(t, 'a trim and a squash are the host\'s; a version with no branch is trimmed by none',
        trimWhy(host, { branchName: 'main' }) === ''
        && trimWhy(writes, { branchName: 'main' }).includes("host's")
        && trimWhy(host, { branchName: '' }) !== '');
  check(t, 'a squash offers the versions behind, newest first as the host lists them',
        JSON.stringify(squashChoices([{ num: 4, name: 'b', seq: 9, rows: 3 },
                                      { num: 2, name: '', seq: 5, rows: 1 }]))
        === JSON.stringify([{ value: '4', label: 'version 4 (b): 3 rows' },
                            { value: '2', label: 'version 2: 1 row' }]),
        JSON.stringify(squashChoices([{ num: 4, name: 'b', seq: 9, rows: 3 }])));
  check(t, 'a file sent and an import are the host\'s to bring in and merge',
        bringWhy(host) === '' && bringWhy(writes).includes("host's"));
  check(t, 'a file sent says its name and size, and that nothing read it',
        sentFileText({ id: 3, name: 'mine.FCStd', size: 36780, by: 'lei', byKind: 'invited',
                       time: 0, mine: true }) === 'mine.FCStd, 35.9 KB, sent and not read'
        && sizeText(12) === '12 bytes' && sizeText(3 * 1024 * 1024) === '3.0 MB');
  const imp = (extra: Record<string, unknown> = {}): any => ({
    branch: 'mine', file: 'mine.FCStd', authors: ['lei'], time: 0, rows: 2,
    independent: false, stopped: false, ...extra });
  check(t, 'an import says its operations, and who made and sent it',
        importText(imp()) === 'mine: 2 operations'
        && importText(imp({ rows: 1, independent: true, stopped: true }))
          === 'mine: 1 operation, shares no history, stopped at a row'
        && importWho(imp()) === 'lei' && importWho(imp({ by: 'bob' })) === 'lei; sent by bob'
        && importWho(imp({ authors: [], by: 'bob' })) === 'sent by bob',
        importText(imp()));
  const ver = (extra: Record<string, unknown> = {}): any => ({
    num: 2, named: true, name: 'kept', branch: 1, branchName: 'main', seq: 5, time: 0,
    here: true, head: false, within: true, ...extra });
  check(t, 'a version is restored by the host, and by one who writes, since the point',
        versionWhy(ver(), host) === '' && versionWhy(ver(), writes) === ''
        && versionWhy(ver({ within: false }), writes) !== '' && versionWhy(ver(), reads) !== '');

  // rows of a branch that is read, applied here
  check(t, 'rows of another branch that set something can be applied here',
        rowsAppliable([row(7, { here: false }), row(8, { here: false })]));
  check(t, "not this branch's own, nor a record, nor none",
        !rowsAppliable([row(7, { here: false }), row(8)])
        && !rowsAppliable([row(7, { here: false, ops: 0 })]) && !rowsAppliable([]));

  // the requests to merge
  const rq = (extra: Record<string, unknown> = {}): any => ({
    id: 3, source: 'try', target: 'main', by: 'ann', byKind: 'invited', note: '', time: 0,
    state: 'open', mine: false, handles: false, here: false, ...extra });
  check(t, 'a request says what it asks', requestText(rq()) === 'merge try into main');
  check(t, 'of a branch that is gone, that it is',
        requestText(rq({ source: '' })).includes('gone'));
  check(t, 'how it stands, and who ended it',
        requestState(rq()) === 'open'
        && requestState(rq({ state: 'merged', closedBy: 'host' })) === 'merged by host');
  check(t, 'who asked withdraws it and does nothing else of it',
        JSON.stringify(requestActs(rq({ mine: true })))
        === '{"merge":false,"reject":false,"withdraw":true,"mergeWhy":""}',
        JSON.stringify(requestActs(rq({ mine: true }))));
  check(t, 'who writes the target rejects it, and merges it where the document is on it',
        requestActs(rq({ handles: true, here: true })).merge
        && requestActs(rq({ handles: true, here: true })).reject
        && !requestActs(rq({ handles: true })).merge
        && requestActs(rq({ handles: true })).mergeWhy.includes('main'),
        requestActs(rq({ handles: true })).mergeWhy);
  check(t, 'one that ended has nothing to do',
        JSON.stringify(requestActs(rq({ state: 'merged', mine: true, handles: true, here: true })))
        === '{"merge":false,"reject":false,"withdraw":false,"mergeWhy":""}');
  check(t, 'the open ones are listed first, then the newest',
        sortRequests([rq({ id: 1, state: 'merged' }), rq({ id: 2 }), rq({ id: 5, state: 'rejected' }),
                      rq({ id: 4 })]).map((r) => r.id).join(',') === '4,2,5,1');
  check(t, 'a merge is asked of two branches that differ',
        requestProblem('try', 'main') === '' && requestProblem('try', 'try') !== ''
        && requestProblem('', 'main') !== '');

  const listed = [9, 8, 7, 6, 5];
  const pick = (picked: number[], anchor: number | null, seq: number, how = {}): string => {
    const r = pickRows(listed, picked, anchor, seq, how);
    return `${r.picked.join(',')}@${r.anchor}`;
  };
  check(t, 'a click chooses the row alone', pick([9, 8], 9, 6) === '6@6', pick([9, 8], 9, 6));
  check(t, 'with the others where it is held', pick([9], 9, 6, { toggle: true }) === '9,6@6',
        pick([9], 9, 6, { toggle: true }));
  check(t, 'and it goes if it was one of them',
        pick([9, 6], 6, 9, { toggle: true }) === '6@6', pick([9, 6], 6, 9, { toggle: true }));
  check(t, 'those between, as listed, with shift',
        pick([8], 8, 5, { range: true }) === '8,7,6,5@8', pick([8], 8, 5, { range: true }));
  check(t, 'upward the same', pick([6], 6, 9, { range: true }) === '9,8,7,6@6',
        pick([6], 6, 9, { range: true }));
  check(t, 'with nothing chosen before, shift is a click',
        pick([], null, 7, { range: true }) === '7@7', pick([], null, 7, { range: true }));
  check(t, 'a range added to what was chosen',
        pick([9, 6], 6, 5, { range: true, toggle: true }) === '9,6,5@6',
        pick([9, 6], 6, 5, { range: true, toggle: true }));

  check(t, 'one row to undo is named by its number', undoLabel(1, 12) === 'Undo row 12');
  check(t, 'several by how many', undoLabel(3, 12) === 'Undo 3 rows');

  check(t, 'a branch needs a name', branchNameProblem('  ', ['main']) !== '');
  check(t, 'not one that is taken', branchNameProblem(' main ', ['main']).includes('main'));
  check(t, 'a new one is fine', branchNameProblem('try', ['main']) === '');

  check(t, 'a version is its name', versionText({ num: 4, name: 'first cut' }) === 'first cut');
  check(t, 'or its number', versionText({ num: 4, name: '' }) === 'Version 4');

  check(t, "a refused act says the host's reason",
        actRefusal({ code: 'Refused', message: 'row 7 changed Box.Height since' })
        === 'row 7 changed Box.Height since');
  check(t, "what is the host's to do says so, in the host's words where it gave them",
        actRefusal({ code: 'Forbidden' }).includes('host')
        && actRefusal({ code: 'Forbidden', message: 'before the serving point' })
           === 'before the serving point');
  check(t, 'what meets what was set here says how, in the host\'s words',
        actRefusal({ code: 'Conflicts', message: '2 of what it sets was also set here' })
        .startsWith('2 of'));
  check(t, "a row that is another's says so",
        actRefusal({ code: 'NotYours', message: "row 5 is ann's" }) === "row 5 is ann's");
  check(t, 'an edit in the way says to leave it',
        actRefusal({ code: 'Editing' }).includes('leave it'));
  check(t, 'a viewer is told why of an act too',
        actRefusal({ code: 'ViewOnly' }).includes('right to edit'));

  // the merge's own list (sec 32.26): conflicts first, a side for each
  const change = (key: string, kind: string, extra: Record<string, unknown> = {}): any => ({
    key, kind, op: 'set', ckind: 'obj', object: key.split('.')[0], prop: key.split('.')[1] ?? '',
    derived: false, ours: '1', theirs: '2', base: '0', ...extra });
  const sorted = sortChanges([change('Box.Width', 'take'), change('Box.Shape', 'derived'),
                              change('view:Box.Color', 'view'), change('Box.Height', 'conflict')]);
  check(t, 'a preview lists the conflicts first, what goes in, the view, the derived',
        sorted.map((c) => c.kind).join(',') === 'conflict,take,view,derived',
        sorted.map((c) => c.kind).join(','));
  check(t, 'a change is named by the object\'s label and the property',
        changeName(change('Box.Height', 'conflict', { label: 'Bracket' })) === 'Bracket.Height'
        && changeName(change('.Label', 'take', { ckind: 'doc', object: '' })) === '(document).Label',
        changeName(change('Box.Height', 'conflict', { label: 'Bracket' })));
  check(t, 'a conflict and a view are given a side; the rest say what goes in',
        changePicked(change('a.b', 'conflict')) && changePicked(change('a.b', 'view'))
        && !changePicked(change('a.b', 'take'))
        && changeTakes(change('a.b', 'take'), {}) === 'theirs'
        && changeTakes(change('a.b', 'merge'), {}) === 'both'
        && changeTakes(change('a.b', 'view'), {}) === 'ours'
        && changeTakes(change('a.b', 'conflict'), {}) === 'pick a side'
        && changeTakes(change('a.b', 'conflict'), { 'a.b': 'theirs' }) === 'theirs');
  const two = [change('Box.Height', 'conflict'), change('Box.Length', 'conflict'),
               change('view:Box.Color', 'view'), change('Box.Width', 'take')];
  check(t, 'the act waits for a side for each conflict, not for a view',
        unpickedConflicts(two, {}) === 2
        && unpickedConflicts(two, { 'Box.Height': 'ours' }) === 1
        && unpickedConflicts(two, { 'Box.Height': 'ours', 'Box.Length': 'theirs' }) === 0);
  const sent = picksOf(two, { 'Box.Height': 'ours', 'Box.Length': 'theirs', 'Box.Width': 'theirs',
                              'gone.Key': 'theirs' });
  check(t, 'the picks sent are of the changes that ask a side, and of no other',
        JSON.stringify(sent) === '{"Box.Height":"ours","Box.Length":"theirs"}',
        JSON.stringify(sent));
  check(t, 'the line says how much, or that nothing here moved',
        previewText({ what: 'rows', count: 3, conflicts: 1, forward: 0 }, 'Apply row 7')
          === 'Apply row 7: 3 changes, 1 conflict'
        && previewText({ what: 'merge', count: 0, conflicts: 0, forward: 2 }, 'Merge a into b')
          === 'Merge a into b: nothing here moved since, 2 rows taken as they are');

  // a change of theirs left out, as the desktop unticks it
  check(t, 'what goes in of theirs can be left out; a conflict, a view, a derived cannot',
        changeLeavable(change('a.b', 'take')) && changeLeavable(change('a.b', 'merge'))
        && !changeLeavable(change('a.b', 'conflict')) && !changeLeavable(change('a.b', 'view'))
        && !changeLeavable(change('a.b', 'derived')));
  const outs = [change('Sketch.Geometry', 'merge'), change('Sketch.Constraints', 'merge',
                { key: 'Sketch.Geometry' }), change('Box.Width', 'take'),
                change('Box.Height', 'conflict')];
  const leftSides: Record<string, 'ours' | 'theirs'> = { 'Sketch.Geometry': 'ours',
                                                         'Box.Height': 'theirs' };
  check(t, 'left out it says so, with every change of its key, and is sent as ours',
        changeLeftOut(outs[0], leftSides) && changeLeftOut(outs[1], leftSides)
        && !changeLeftOut(outs[2], leftSides)
        && changeTakes(outs[0], leftSides) === 'left out'
        && changeTakes(outs[2], leftSides) === 'theirs'
        && JSON.stringify(picksOf(outs, leftSides))
          === '{"Sketch.Geometry":"ours","Box.Height":"theirs"}',
        JSON.stringify(picksOf(outs, leftSides)));

  // what is kept in the file (sec 32.27)
  const lines = keptLines(
    [{ ref: 'aa', cid: 3, obj: 'Cut', label: 'Cut', here: true, prop: 'Shape', seq: 7, row: true,
       release: true },
     { ref: 'bb', cid: 9, here: false, prop: 'Shape', seq: 2, row: false, release: false,
       why: 'the host lets it go', keeper: 'ann~1a2b3c', keeperKind: 'invited' }],
    [{ cid: 1, obj: 'Box', label: 'Bracket', here: true, release: false, why: 'host',
       values: [{ ref: 'cc', prop: 'Shape', seq: 4, alone: false, keep: false,
                  keepWhy: 'before' },
                { ref: 'aa', prop: 'Shape', seq: 7, alone: true, mine: true, keeper: 'me',
                  release: true }] }]);
  check(t, 'the kept are listed values first, then each object with its values under it',
        lines.map((l) => l.kind).join(',') === 'value,value,object,under,under'
        && lines[2].count === 2 && lines[4].alone && !lines[3].alone,
        lines.map((l) => l.kind).join(','));
  check(t, 'an object is named by its label, or said to be gone',
        keptObjectText({ cid: 1, obj: 'Box', label: 'Bracket', here: true }) === 'Bracket (Box)'
        && keptObjectText({ cid: 3, obj: 'Cut', label: 'Cut', here: true }) === 'Cut'
        && keptObjectText({ cid: 9, here: false }) === 'obj:9, not in the document');
  check(t, 'a value kept by a client says who, or that it was the asker; the host\'s says nothing',
        keeperText(lines[0]) === '' && keeperText(lines[1]) === 'by ann~1a2b3c'
        && keeperText(lines[4]) === 'by you' && keeperText(lines[2]) === '',
        lines.map(keeperText).join('|'));
  check(t, 'a value under an object carries whether the asker may keep it by itself',
        !lines[3].keep && lines[3].keepWhy === 'before' && !lines[4].keep && lines[4].release);
  check(t, 'keeping at a version: the host, or one who writes from before it',
        keepWhy({ host: true, write: false }, false) === ''
        && keepWhy({ host: false, write: true }, true) === ''
        && keepWhy({ host: false, write: true }, false).includes('before the point')
        && keepWhy({ host: false, write: false }, false).includes('write access'));
  check(t, 'a row gone says so; an object has no row',
        keptRowText(lines[0]) === '7' && keptRowText(lines[1]) === '2, gone'
        && keptRowText(lines[2]) === '');
  check(t, 'every key is its own, so a line stays chosen across a refresh',
        new Set(lines.map((l) => l.key)).size === lines.length);
  check(t, 'the tab carries the count; an object how many values',
        keptTabText(0) === 'Kept' && keptTabText(3) === 'Kept (3)'
        && keptCountText(1) === '1 value' && keptCountText(2) === '2 values');
  const keptOp = (extra: Record<string, unknown>): any => ({ idx: 0, op: 'set', kind: 'obj',
    prop: 'Shape', type: '', after: 'aa', obj: 'Cut', here: true, ...extra });
  check(t, 'an op says its value is kept, by itself or with its object',
        opKeptText(keptOp({})) === '' && opKeptText(keptOp({ keptValue: true })) === 'kept'
        && opKeptText(keptOp({ keptObject: true })) === 'all of it kept'
        && opKeptText(keptOp({ keptValue: true, keptObject: true })) === 'kept, and all of it');
  check(t, "a value is kept from an op that set one, of an object still here",
        opKeepable(keptOp({})) && !opKeepable(keptOp({ after: undefined }))
        && !opKeepable(keptOp({ kind: 'doc' })) && !opKeepable(keptOp({ here: false }))
        && !opKeepable(keptOp({ op: 'create' })));
  check(t, 'a keep says how many, and nothing where the log has none',
        keepText(1, true, 'of Cut at row 7') === '1 value of Cut at row 7 kept: a save writes it '
          + 'into the file'
        && keepText(2, false, 'of Cut') === '2 values of Cut let go'
        && keepText(0, true, '').startsWith('Nothing kept'));
}

checkLog();
main();
