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

import { WidgetStore, layoutRefs, refId } from './protocol.ts';
import type { Frame } from './protocol.ts';
import { isKnownLayoutClass, planLayout } from './layout.ts';
import type { LayoutPlan } from './layout.ts';
import { filterSet, inputModeFor, splice, stillApplies, triggerFor, wordAt }
  from './complete.ts';
import type { CompletionSet } from './complete.ts';

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

  checkCompletion();

  console.log(failures === 0 ? '\nALL GREEN' : `\n${failures} FAILURE(S)`);
  process.exit(failures === 0 ? 0 : 1);
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
}

main();
