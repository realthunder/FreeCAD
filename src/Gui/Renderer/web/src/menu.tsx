// The viewer menu: one small round button in the bottom-left corner —
// the only corner nothing else claims (card and pill top-left, NaviCube
// top-right, axis cross bottom-right) — with everything that is not a
// direct manipulation of the model hanging off it.
//
// It was the property launcher, a button with one action. It is a menu
// now because there is a second thing to reach (the HUD) and there will
// be more; a second round button in the same corner would be two
// unlabelled circles competing for the same thumb.
import { For, Show, createSignal, onCleanup } from 'solid-js';

export interface MenuItem {
  label: string;
  onSelect: () => void;
  /// Present for items that are a switch rather than an action; the tick
  /// shows the current state, so the menu also reports it.
  checked?: () => boolean;
}

export function LauncherMenu(props: {
  items: MenuItem[];
  /// True while something else owns the corner — the bottom sheet on a
  /// narrow screen covers it, and a button under a panel is a trap.
  hidden?: () => boolean;
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

  return (
    <Show when={!props.hidden?.()}>
      <div class="fc-launcher" ref={root!}>
        {/* The popup sits above the button, so it opens into the empty
            middle of the viewport rather than off the bottom edge. */}
        <Show when={open()}>
          <div class="fc-menu" role="menu" aria-label="Viewer menu">
            <For each={props.items}>
              {(item) => (
                <button
                  class="fc-menu-item"
                  role={item.checked ? 'menuitemcheckbox' : 'menuitem'}
                  aria-checked={item.checked ? item.checked() : undefined}
                  onClick={() => {
                    item.onSelect();
                    // A switch keeps the menu open — flipping it is
                    // something you watch happen, and may want to undo.
                    if (!item.checked) setOpen(false);
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
          aria-label="Viewer menu"
          title="Viewer menu"
        >☰</button>
      </div>
    </Show>
  );
}
