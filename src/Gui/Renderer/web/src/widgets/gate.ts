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
// tsconfig excludes this file: it imports node builtins and names
// './protocol.ts' with the extension node's stripper wants, neither of
// which the bundle's config (vite/client types, bundler resolution)
// describes. Adding @types/node for one script would break the ruling
// that this gate takes no new dependency; running it is the check.

import { readFileSync, readdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { WidgetStore, layoutRefs, refId } from './protocol.ts';
import type { Frame } from './protocol.ts';

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

function main(): void {
  const files = readdirSync(fixtureDir).filter((f: string) => f.endsWith('.json')).sort();
  if (files.length === 0) {
    console.error(`no fixtures in ${fixtureDir}`);
    process.exit(1);
  }
  for (const file of files) {
    replay(JSON.parse(readFileSync(join(fixtureDir, file), 'utf-8')) as Fixture);
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

  console.log(failures === 0 ? '\nALL GREEN' : `\n${failures} FAILURE(S)`);
  process.exit(failures === 0 ? 0 : 1);
}

main();
