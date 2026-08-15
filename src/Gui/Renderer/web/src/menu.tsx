// The viewer's round corner buttons, bottom-left — the only corner
// nothing else claims (card and pill top-left, NaviCube top-right, axis
// cross bottom-right). Two of them: the viewer menu (everything that is
// not a direct manipulation of the model) and, beside it, the selection
// menu (mode and pick filter) — that one is a distinct glyph, not a
// third item in the first menu, because it is a mode the user flips
// mid-work and should not have to hunt for.
import { For, Show, createSignal, onCleanup } from 'solid-js';
import type { JSX } from 'solid-js';

export interface MenuItem {
  label: string;
  onSelect?: () => void;
  /// Present for items that are a switch rather than an action; the tick
  /// shows the current state, so the menu also reports it.
  checked?: () => boolean;
  /// For checked items that behave as a radio group (the document
  /// section): picking one is a navigation, not a toggle to watch, so
  /// the menu closes like it does for an action.
  closeOnSelect?: boolean;
  /// A non-interactive section label — the selection menu has two
  /// groups and headers are what keep six short words readable.
  header?: boolean;
}

export function LauncherMenu(props: {
  items: MenuItem[];
  /// True while something else owns the corner — the bottom sheet on a
  /// narrow screen covers it, and a button under a panel is a trap.
  hidden?: () => boolean;
  /// Second and later buttons distinguish themselves: a glyph — inline
  /// SVG preferred, a text glyph renders differently per device font —
  /// a label, and an extra class that offsets them along the corner.
  glyph?: JSX.Element;
  title?: string;
  class?: string;
}) {
  const [open, setOpen] = createSignal(false);
  let root!: HTMLDivElement;

  // Close on anything that means "not this menu": a press elsewhere
  // (captured, so it closes even on a press the canvas will consume for
  // an orbit) and Escape.
  const onDown = (e: PointerEvent) => {
    if (!root.contains(e.target as Node)) setOpen(false);
  };
  const onKey = (e: KeyboardEvent) => {
    if (e.key === 'Escape') setOpen(false);
  };
  document.addEventListener('pointerdown', onDown, true);
  window.addEventListener('keydown', onKey);
  onCleanup(() => {
    document.removeEventListener('pointerdown', onDown, true);
    window.removeEventListener('keydown', onKey);
  });

  const title = () => props.title ?? 'Viewer menu';

  return (
    <Show when={!props.hidden?.()}>
      <div class={`fc-launcher ${props.class ?? ''}`} ref={root!}>
        {/* The popup sits above the button, so it opens into the empty
            middle of the viewport rather than off the bottom edge. */}
        <Show when={open()}>
          <div class="fc-menu" role="menu" aria-label={title()}>
            <For each={props.items}>
              {(item) => item.header ? (
                <div class="fc-menu-header">{item.label}</div>
              ) : (
                <button
                  class="fc-menu-item"
                  role={item.checked ? 'menuitemcheckbox' : 'menuitem'}
                  aria-checked={item.checked ? item.checked() : undefined}
                  onClick={() => {
                    item.onSelect?.();
                    // A switch keeps the menu open — flipping it is
                    // something you watch happen, and may want to undo.
                    if (!item.checked || item.closeOnSelect) setOpen(false);
                  }}
                >
                  <span class="fc-menu-tick">
                    {item.checked?.() ? '✓' : ''}
                  </span>
                  {item.label}
                </button>
              )}
            </For>
          </div>
        </Show>
        <button
          class="fc-launch"
          aria-haspopup="menu"
          aria-expanded={open()}
          onClick={() => setOpen(!open())}
          aria-label={title()}
          title={title()}
        >{props.glyph ?? '☰'}</button>
      </div>
    </Show>
  );
}
