// What every floating panel in the viewer chrome shares: where it sits,
// how it is dragged, and how it is kept on screen. Split out of the
// inspector when the HUD became a card of its own — the panels differ
// in what they show, not in how they behave.
import type { JSX } from 'solid-js';

/// A dragged panel's top-left, in viewport pixels.
export interface Pos { x: number; y: number }

/// Below this the layout is a bottom sheet pinned to the edge, and
/// there is nowhere to drag it to.
export const NARROW = 640;

export function loadPos(key: string): Pos | null {
  try {
    const raw = localStorage.getItem(key);
    if (!raw) return null;
    const p = JSON.parse(raw);
    return typeof p?.x === 'number' && typeof p?.y === 'number' ? p : null;
  }
  catch {
    return null;   // private mode, quota, corrupt value: no memory, no error
  }
}

/// Anchoring for a dragged panel: the CSS corner still applies until it
/// has been moved, and the narrow layout's bottom sheet always wins — a
/// phone rotated into a wide viewport picks the drag back up, which is
/// what a remembered position should do.
export const posStyle = (p: Pos | null): JSX.CSSProperties | undefined =>
  p && window.innerWidth > NARROW
    ? { left: `${p.x}px`, top: `${p.y}px`, right: 'auto', bottom: 'auto' }
    : undefined;

/// Pull a panel back inside the viewport once it is mounted and its real
/// size is known — a position handed over from a smaller panel, or one
/// remembered from a larger window, can put half of it outside. Runs
/// after layout, so the size is the laid-out one rather than a guess.
export function fitOnScreen(el: HTMLElement, p: () => Pos | null,
                            set: (v: Pos) => void) {
  requestAnimationFrame(() => {
    const cur = p();
    if (!cur || window.innerWidth <= NARROW) return;
    const r = el.getBoundingClientRect();
    const x = Math.min(Math.max(0, cur.x),
                       Math.max(0, window.innerWidth - r.width));
    const y = Math.min(Math.max(0, cur.y),
                       Math.max(0, window.innerHeight - r.height));
    if (x !== cur.x || y !== cur.y) set({ x, y });
  });
}

/// Make `panel` draggable by `handle`, remembering where it was left.
///
/// Any fixed corner collides with something eventually — the card is
/// tall enough to cover the NaviCube whatever side it is anchored to,
/// and what is behind it is exactly what the user is editing. So the
/// panel moves instead: grab the header, put it where the model is not.
///
/// Pointer events rather than mouse events, so a stylus and a tablet's
/// touch work the same way; capture on the handle so a fast drag that
/// outruns the pointer keeps the panel rather than dropping it.
/// `panel` is a getter, not an element: a header's ref may run before
/// the card's own has assigned it, and a drag that reads the panel
/// only when the pointer goes down never sees that window.
export function draggable(
  handle: HTMLElement,
  panel: () => HTMLElement,
  setPos: (p: Pos) => void,
  storeKey: string,
) {
  const clamp = (x: number, y: number): Pos => {
    const r = panel().getBoundingClientRect();
    // Bound by the panel's own size so it can never be dragged out of
    // reach — a panel with no grabbable header on screen is lost.
    return {
      x: Math.min(Math.max(0, x), Math.max(0, window.innerWidth - r.width)),
      y: Math.min(Math.max(0, y), Math.max(0, window.innerHeight - r.height)),
    };
  };

  handle.addEventListener('pointerdown', (e: PointerEvent) => {
    if (window.innerWidth <= NARROW) return;
    // The header carries the close button, and the pill its own; a
    // press on a control is that control's, not a drag.
    if ((e.target as HTMLElement).closest('button, input, select, a')) return;
    const r = panel().getBoundingClientRect();
    const ox = e.clientX - r.left;
    const oy = e.clientY - r.top;
    let last = { x: r.left, y: r.top };
    const move = (ev: PointerEvent) => {
      last = clamp(ev.clientX - ox, ev.clientY - oy);
      setPos(last);
    };
    const up = () => {
      handle.removeEventListener('pointermove', move);
      handle.removeEventListener('pointerup', up);
      handle.removeEventListener('pointercancel', up);
      try {
        localStorage.setItem(storeKey, JSON.stringify(last));
      }
      catch { /* no memory of it next time; the drag still worked */ }
    };
    handle.setPointerCapture(e.pointerId);
    handle.addEventListener('pointermove', move);
    handle.addEventListener('pointerup', up);
    handle.addEventListener('pointercancel', up);
    // Stop the press from reaching the canvas underneath, which would
    // read it as the start of an orbit.
    e.preventDefault();
    e.stopPropagation();
  });
}
