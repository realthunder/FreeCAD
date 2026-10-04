// The completion controller (docs/Sandbox.md 7.23, 7.26): one for the
// console, the expression dialog and the sheet, over any source that
// answers a CompletionSet -- the host's `widgets.complete`, the guest's
// rlcompleter, the sheet's `sheet.complete`.
//
// The pure rules stay in complete.ts, where the node gate asserts them.
// This is the stateful half that used to be written twice, once in
// console.tsx and once in field.tsx, and drifted (two debounces, a
// highlight that walked past the rows drawn): when to ask, what to show
// between asks, which keys the list owns, and the splice of a pick.
// The list itself is the shared PickList; on a phone it is pinned above
// the keyboard where the thumb is, on a wide viewport it sits in the
// editor's own flow.

import { Show, createSignal, onCleanup } from 'solid-js';
import type { Accessor, JSX } from 'solid-js';
import { NARROW, onKeyboardInset } from '../panel.ts';
import { filterSet, splice, stillApplies, triggerFor } from './complete.ts';
import type { CompletionSet } from './complete.ts';
import { PickList, moveInList } from './picker.tsx';
import type { PickRow } from './picker.tsx';

/// A word trigger fires only after typing pauses; the dot fires at once.
/// One number for every editor: the console's guest is a call and the
/// panel's host a round trip, but a menu that flickers on every keystroke
/// is worse than one that waits either way.
export const DEBOUNCE_MS = 150;

/// Who answers, or null while nobody can (a console that is busy).
export type Source = (text: string, pos: number) => Promise<CompletionSet>;

/// The editor the controller completes into.  It reads the text and the
/// caret from here rather than keeping its own, so the editor's signal
/// stays the one truth; `apply` is the pick landing.
export interface Editor {
  text: () => string;
  caret: () => number;
  apply: (text: string, pos: number) => void;
}

export interface Completion {
  items: Accessor<string[]>;
  rows: Accessor<CompletionRow[]>;
  active: Accessor<number>;
  setActive: (i: number) => void;
  open: () => boolean;
  /// The explicit ask: the button, Tab, Ctrl+Space.  Asks whatever the
  /// caret is at, trigger or not.
  ask: () => void;
  /// After every edit; the editor has already updated its own text.
  onInput: (e: InputEvent) => void;
  /// Before the editor's own key handling; true when the list took it.
  onKey: (e: KeyboardEvent) => boolean;
  pick: (i: number) => void;
  close: () => void;
}

export interface CompletionRow extends PickRow {
  item: string;
}

export function createCompletion(opts: {
  source: () => Source | null;
  editor: Editor;
  /// What a pick did, for an editor with something to refresh after.
  onPicked?: (text: string) => void;
}): Completion {
  const [items, setItems] = createSignal<string[]>([]);
  const [active, setActive] = createSignal(0);
  let set: CompletionSet | null = null;
  let askTimer = 0;
  /// Only the latest ask may answer: a slow answer to `Ap` must not land
  /// after the fast one to `App.` did.
  let serial = 0;

  const close = () => {
    setItems([]);
    setActive(0);
  };

  const show = (narrowed: string[]) => {
    setItems(narrowed);
    setActive(0);
  };

  const askAt = async (value: string, at: number) => {
    const source = opts.source();
    if (!source) return;
    const mine = ++serial;
    let answered: CompletionSet;
    try {
      answered = await source(value, at);
    }
    catch {
      if (mine === serial) close();
      return;
    }
    if (mine !== serial) return;
    // Text and caret may both have moved on while the answer was in
    // flight.  A typed-further caret inside the same segment narrows the
    // answer; anything else makes it stale, and comparing the text alone
    // (as before) let an arrow key's moved caret splice at the old start.
    const now = opts.editor.text();
    const caret = opts.editor.caret();
    if (!stillApplies(answered, now, caret)) return;
    set = answered;
    show(filterSet(answered, now, caret));
  };

  const ask = () => {
    clearTimeout(askTimer);
    void askAt(opts.editor.text(), opts.editor.caret());
  };

  const onInput = (e: InputEvent) => {
    clearTimeout(askTimer);
    // A soft keyboard composing a word fires input on every candidate
    // it tries; the word is not typed until the composition ends, and
    // an ask on the candidates answers for text that will not be there.
    if (e.isComposing) return;
    const value = opts.editor.text();
    const at = opts.editor.caret();

    // Between round trips the answered set is narrowed locally, which is
    // what keeps a phone to one request per dotted segment.
    if (set && stillApplies(set, value, at)) {
      const narrowed = filterSet(set, value, at);
      show(narrowed);
      if (narrowed.length) return;
      set = null;   // narrowed to nothing: ask again below
    }
    else {
      set = null;
      close();
    }

    const why = triggerFor(value, at);
    if (!why) return;
    if (why === 'dot') void askAt(value, at);
    else askTimer = window.setTimeout(() => void askAt(value, at), DEBOUNCE_MS);
  };

  const pick = (i: number) => {
    const item = items()[i];
    if (!set || !item) return;
    const next = splice(set, item, opts.editor.text(), opts.editor.caret());
    close();
    set = null;
    opts.editor.apply(next.text, next.pos);
    opts.onPicked?.(next.text);
  };

  const onKey = (e: KeyboardEvent): boolean => {
    if (e.isComposing || e.keyCode === 229) return false;
    const ctrl = e.ctrlKey || e.metaKey;
    const count = items().length;
    // While a list is up it owns the keys that move and choose within
    // it: Escape dismisses the list rather than the line or the dialog,
    // and Up/Down walk the candidates rather than the history.
    if (count) {
      if (e.key === 'Escape') { e.preventDefault(); close(); return true; }
      const to = moveInList(e, count, active());
      if (to !== null) { e.preventDefault(); setActive(to); return true; }
      if (e.key === 'Enter' || e.key === 'Tab') {
        e.preventDefault();
        pick(active());
        return true;
      }
      return false;
    }
    // No list up: Tab is the explicit ask (the button beside the editor
    // is the same ask, for a keyboard that has no Tab key), and so is
    // Ctrl+Space, the desktop's chord.
    if (e.key === 'Tab' && !e.shiftKey || e.key === ' ' && ctrl) {
      e.preventDefault();
      ask();
      return true;
    }
    return false;
  };

  const rows = (): CompletionRow[] => {
    const s = set;
    return items().map((item) => {
      const at = s?.items.indexOf(item) ?? -1;
      const desc = at >= 0 ? (s?.details[at] ?? '') : '';
      return { key: item, title: item, desc, item };
    });
  };

  onCleanup(() => clearTimeout(askTimer));

  return {
    items, rows, active, setActive,
    open: () => items().length > 0,
    ask, onInput, onKey, pick, close,
  };
}

/// The list, placed.  On a wide viewport it is in the editor's flow, where
/// the caller put this element; on a phone it is pinned above the
/// keyboard, the row the thumb is already at, and sized to what the
/// keyboard leaves -- a `vh` there is the layout viewport, half of which
/// the keyboard covers.
export function CompletionList(props: {
  c: Completion;
  class?: string;
}): JSX.Element {
  const [narrow, setNarrow] = createSignal(window.innerWidth <= NARROW);
  const [inset, setInset] = createSignal(0);
  const onResize = () => setNarrow(window.innerWidth <= NARROW);
  window.addEventListener('resize', onResize);
  const stopInset = onKeyboardInset(setInset);
  onCleanup(() => {
    window.removeEventListener('resize', onResize);
    stopInset();
  });
  const sheetHeight = () => Math.max(120, Math.round((window.innerHeight - inset()) * 0.45));

  const list = (cls: string) => (
    <PickList
      rows={props.c.rows}
      hi={props.c.active}
      onHi={props.c.setActive}
      onPick={(_row, i) => props.c.pick(i)}
      descAsTitle
      class={cls}
    />
  );

  return (
    <Show when={props.c.open()}>
      <Show when={narrow()} fallback={list(`fc-complete ${props.class ?? ''}`)}>
        <div class="fc-complete-sheet"
             style={{ bottom: `${inset()}px`, 'max-height': `${sheetHeight()}px` }}>
          {list('fc-complete-pinned')}
        </div>
      </Show>
    </Show>
  );
}

/// The tap target beside an editor: the explicit ask.  A handset
/// keyboard has no Tab key, so without this the completion is unreachable
/// on the device that needs it most (7.23 ruled it for the expression
/// field, 7.24 for the console; the dialog then shipped without one).
/// pointerdown is refused so the press does not blur the editor (the
/// keyboard would collapse under the caret being completed); the ask is
/// the CLICK.  Asking on the pointerdown itself, as the console's button
/// first did, raised the list under the finger before the tap ended, and
/// the tap's click then picked whichever row had appeared there.
export function CompleteButton(props: {
  c: Completion;
  disabled?: () => boolean;
  class?: string;
}): JSX.Element {
  return (
    <button
      class={`fc-complete-btn ${props.class ?? ''}`}
      title="Complete (Tab)"
      aria-label="Complete"
      disabled={props.disabled?.() ?? false}
      onPointerDown={(e) => e.preventDefault()}
      onClick={() => props.c.ask()}
    >
      &#9662;
    </button>
  );
}
