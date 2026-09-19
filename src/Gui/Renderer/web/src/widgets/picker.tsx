// One list to pick from (docs/Sandbox.md 7.26): the omni search's rows,
// the console's and the expression dialog's completions, the sheet's.
//
// Before this each had its own -- the omni box a vertical list, the two
// completers a 12-row list on a wide viewport and a horizontal strip of
// chips on a phone, capped at 20 with the keyboard highlight cycling over
// items the strip did not show.  The user ruled for the list: a button at
// the right of the editor, a vertical list that scrolls, every item in
// it.  This is that list, and nothing else: rows in, a highlight, the
// keys that move it, a pick.  What the rows ARE is the caller's.

import { For, Show, createEffect, on } from 'solid-js';
import type { JSX } from 'solid-js';

/// What a row needs to be drawn.  Callers extend it with what a pick
/// needs to act on.
export interface PickRow {
  key: string;
  title: string;
  desc?: string;
  /// Drawn greyed: a command that is not active, a refused one.
  inactive?: boolean;
}

/// Rows past this are not drawn; the note says how many.  Sixty is the
/// omni box's number, and a completion answer is rarely longer than a
/// document's object count -- a list that long is scrolled, not read,
/// and typing one more letter is the faster way through it.
export const PICK_LIMIT = 60;

/// Which row a key moves the highlight to, or null when the key is not
/// a move.  Clamped, not wrapped: a wrap on a list that scrolls loses
/// the reader.  Tab moves down and Shift+Tab up only when `tab` is
/// set -- a completer takes Tab as "pick", the omni box as "next".
export function moveInList(e: KeyboardEvent, count: number, hi: number,
                           tab = false): number | null {
  if (!count) return null;
  const down = () => Math.min(hi + 1, count - 1);
  const up = () => Math.max(hi - 1, 0);
  switch (e.key) {
    case 'ArrowDown': return down();
    case 'ArrowUp': return up();
    case 'PageDown': return Math.min(hi + 8, count - 1);
    case 'PageUp': return Math.max(hi - 8, 0);
    case 'Home': return 0;
    case 'End': return count - 1;
    case 'Tab': return tab ? (e.shiftKey ? up() : down()) : null;
  }
  return null;
}

export function PickList<T extends PickRow>(props: {
  rows: () => T[];
  /// The highlighted index, -1 for none.
  hi: () => number;
  onHi: (i: number) => void;
  onPick: (row: T, i: number) => void;
  /// Extra content at the row's right: a parameter's value, a group's
  /// arrow.  Its own buttons stop pointerdown so a press there is not a
  /// pick.
  right?: (row: T) => JSX.Element;
  /// Rows beyond `limit` are counted, not drawn.
  limit?: number;
  /// A line under the rows when there is something to say, beside the
  /// overflow count this draws itself.
  note?: () => string | null;
  class?: string;
  /// The rows are what `title` shows on hover; a completion carries its
  /// tool tip in `desc` and wants it there instead of inline.
  descAsTitle?: boolean;
  ref?: (el: HTMLDivElement) => void;
}): JSX.Element {
  let listEl: HTMLDivElement | undefined;
  const limit = () => props.limit ?? PICK_LIMIT;
  const shown = () => props.rows().slice(0, limit());
  const over = () => Math.max(0, props.rows().length - limit());

  // The highlight follows the keys into view.  `nearest`, so a list
  // taller than its box scrolls one row at a time and a pointer hover
  // that set the highlight does not jump the list.
  createEffect(on(props.hi, (i) => {
    if (i < 0 || !listEl) return;
    const el = listEl.querySelector<HTMLElement>(`[data-row="${i}"]`);
    el?.scrollIntoView({ block: 'nearest' });
  }));

  const note = () => {
    const n = over();
    const own = props.note?.() ?? null;
    if (own) return own;
    return n > 0 ? `${n} more; keep typing` : null;
  };

  return (
    <div
      class={props.class ? `fc-pick ${props.class}` : 'fc-pick'}
      role="listbox"
      ref={(el) => { listEl = el; props.ref?.(el); }}
      // pointerdown, not the click, is what would move focus off the
      // editor -- and on a phone that collapses the keyboard under the
      // caret being completed.  Refusing the default here keeps the
      // focus and STILL lets a finger scroll the list, which a pick on
      // pointerdown (the chips' way) never could.  The pick is the click.
      onPointerDown={(e) => e.preventDefault()}
    >
      <For each={shown()}>
        {(row, i) => (
          <div
            class="fc-pick-row"
            role="option"
            data-row={i()}
            aria-selected={props.hi() === i()}
            classList={{ 'fc-pick-hi': props.hi() === i(),
                         'fc-pick-inactive': !!row.inactive }}
            title={props.descAsTitle ? (row.desc ?? '') : ''}
            // A MOVING mouse takes the highlight; a list that appears
            // under a resting one must not, or the row under the cursor
            // is what Enter picks instead of the first match -- Chrome
            // delivers pointerenter on layout alone.
            onPointerMove={(e) => {
              if (e.pointerType === 'mouse' && (e.movementX || e.movementY) && props.hi() !== i())
                props.onHi(i());
            }}
            onClick={() => props.onPick(row, i())}
          >
            <div class="fc-pick-main">
              <span class="fc-pick-title">{row.title}</span>
              <Show when={row.desc && !props.descAsTitle}>
                <span class="fc-pick-desc">{row.desc}</span>
              </Show>
            </div>
            <Show when={props.right}>
              <span class="fc-pick-right" onPointerDown={(e) => e.stopPropagation()}>
                {props.right!(row)}
              </span>
            </Show>
          </div>
        )}
      </For>
      <Show when={note()}><div class="fc-note">{note()}</div></Show>
    </div>
  );
}
