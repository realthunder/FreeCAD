// Split-view chrome (docs/SplitViews.md sec 9.4): a binary splitter
// tree of DOM cells over the single canvas, mirroring the desktop
// ViewArea gestures (ViewArea.cpp ViewAreaZone, sec 21 and 22): a drag
// is shown as frames and carried out at the release, and can be given
// up. The chrome owns the
// gestures -- corner action zones split and join, border handles
// resize and close -- and pushes the resulting cell rects to the viewer through
// window.fcviewerSetLayout; the WASM side owns cameras, content and
// input routing inside the cells (main.cpp fcviewer_set_layout).
//
// Cells are pointer-events: none so the canvas keeps every scene
// gesture; only the corner zones, the border handles and the per-cell
// content chip are interactive.
import { For, Show, createSignal, onCleanup } from 'solid-js';
import type { CyclesDevice, CyclesState } from './control';

type CellNode = { cell: true; id: number; page: boolean };
type SplitNode = {
  cell: false;
  /// 'h': a | b side by side; 'v': a above b.
  dir: 'h' | 'v';
  /// a's share of the extent, clamped like the desktop's permille
  /// floor so no cell can be squeezed invisible.
  ratio: number;
  a: Node;
  b: Node;
};
type Node = CellNode | SplitNode;

/// Corner-zone size and the drag threshold, in CSS px -- ViewAreaZone
/// parity (Size = 14, manhattan 12).
const THRESHOLD = 12;
/// The floor of a split's ratio whatever the sizes: what a layout from
/// the store, or a window that shrank, is held to.
const MIN_RATIO = 0.05;
/// The least a GESTURE makes a cell, in CSS px each way: the desktop's
/// View/OpenView/MinimumCellSize at its default. `?mincell=N` in the
/// page's address overrides it (0: no minimum) -- a phone held upright
/// is narrower than two cells of 300.
const MIN_CELL = (() => {
  try {
    const v = new URLSearchParams(location.search).get('mincell');
    if (v !== null && v !== '' && Number.isFinite(Number(v)))
      return Math.max(0, Number(v));
  }
  catch {}
  return 300;
})();
/// How far past its limit a border is dragged before the drag means
/// closing the cell it is pushed into (ViewAreaSplitterHandle parity).
const CLOSE_SLACK = 12;

const clampRatio = (r: number) =>
  Math.min(1 - MIN_RATIO, Math.max(MIN_RATIO, r));

/// What a drag under way will do, drawn over the cells it changes
/// (ViewArea::DragFrame): a cell that stays at the size it will have,
/// the cell a split makes, a cell that is closed (red, crossed out). A
/// split that is refused has no frame: the cursor says so.
type FrameKind = 'kept' | 'fresh' | 'going';
interface Frame {
  x: number; y: number; w: number; h: number;
  kind: FrameKind;
}

interface CellRect {
  node: CellNode;
  x: number; y: number; w: number; h: number;
}
interface HandleRect {
  node: SplitNode;
  x: number; y: number; w: number; h: number;
}

let nextId = 2;

// ---- Layout persistence --------------------------------------------
//
// The desktop persists layouts per document in GuiDocument.xml; the
// browser equivalent is localStorage on this device, keyed by the
// served document. A layout is chrome state, not document content, and
// different devices legitimately want different splits of the same
// document -- which is also why there is no server-side store: it
// would be a wire change M4 deliberately avoided (docs/SplitViews.md
// sec 9.1). Saved on every tree change, restored once the viewer is up
// and re-keyed when the served document becomes known or changes.
type SavedNode = { id?: number; page?: boolean; dir?: 'h' | 'v';
                   ratio?: number; a?: SavedNode; b?: SavedNode };
const toSaved = (n: Node): SavedNode => n.cell
  ? { id: n.id, page: n.page }
  : { dir: n.dir, ratio: n.ratio, a: toSaved(n.a), b: toSaved(n.b) };
const fromSaved = (s: SavedNode | undefined): Node | null => {
  if (!s || typeof s !== 'object') return null;
  if (s.dir === 'h' || s.dir === 'v') {
    if (typeof s.ratio !== 'number'
        || !Number.isFinite(s.ratio)) return null;
    const a = fromSaved(s.a);
    const b = fromSaved(s.b);
    return a && b ? { cell: false, dir: s.dir,
                      ratio: Math.min(1 - MIN_RATIO,
                                      Math.max(MIN_RATIO, s.ratio)),
                      a, b } : null;
  }
  // Stored ids are not trusted across sessions: every restored cell is
  // renumbered, so a corrupt store cannot produce duplicate ids.
  return { cell: true, id: 0, page: !!s.page };
};

export function SplitOverlay() {
  const [root, setRoot] = createSignal<Node>(
    { cell: true, id: 1, page: false }, { equals: false });
  // The frames of the drag under way, and what it is ("split", "join",
  // "resize", "close"; '' for none).
  const [frames, setFrames] = createSignal<{ op: string; list: Frame[] }>(
    { op: '', list: [] });
  // The reason of a refused split, shown for a few seconds.
  const [note, setNote] = createSignal('');
  const [size, setSize] = createSignal({ w: 0, h: 0 });

  const canvasSize = () => {
    const c = document.getElementById('canvas');
    const r = c ? c.getBoundingClientRect()
                : { width: window.innerWidth, height: window.innerHeight };
    return { w: r.width, h: r.height };
  };
  const onResize = () => { setSize(canvasSize()); push(); };
  window.addEventListener('resize', onResize);
  setTimeout(onResize);
  onCleanup(() => window.removeEventListener('resize', onResize));

  /// The cells and borders of the tree; with `over`, as they would be
  /// were that split's ratio the one given -- what a border drag shows
  /// before it changes anything.
  const layout = (over?: { split: SplitNode; ratio: number }) => {
    const { w, h } = size();
    const cells: CellRect[] = [];
    const handles: HandleRect[] = [];
    const walk = (n: Node, x: number, y: number,
                  cw: number, ch: number) => {
      if (n.cell) {
        cells.push({ node: n, x, y, w: cw, h: ch });
        return;
      }
      const r = clampRatio(over && over.split === n ? over.ratio : n.ratio);
      if (n.dir === 'h') {
        const aw = cw * r;
        walk(n.a, x, y, aw, ch);
        walk(n.b, x + aw, y, cw - aw, ch);
        handles.push({ node: n, x: x + aw - 3, y, w: 6, h: ch });
      }
      else {
        const ah = ch * r;
        walk(n.a, x, y, cw, ah);
        walk(n.b, x, y + ah, cw, ch - ah);
        handles.push({ node: n, x, y: y + ah - 3, w: cw, h: 6 });
      }
    };
    walk(root(), 0, 0, w, h);
    return { cells, handles };
  };

  /// The rect push: '' while the tree is one cell, so the viewer runs
  /// its ordinary single-view path.
  const push = () => {
    const r = root();
    if (r.cell) {
      window.fcviewerSetLayout?.('');
      return;
    }
    const spec = layout().cells.map((c) =>
      `${c.node.id},${Math.round(c.x)},${Math.round(c.y)},`
      + `${Math.round(c.w)},${Math.round(c.h)},${c.node.page ? 1 : 0}`)
      .join(';');
    window.fcviewerSetLayout?.(spec);
  };
  const changed = () => { setRoot(root()); push(); save(); };

  // Which store the layout lives in: the served document's name once
  // known ('fc:docs' / a switch), else the scene URL -- so a plain
  // single-document serve still remembers its split.
  let docKey = '';
  const sceneKey = (() => {
    try { return new URLSearchParams(location.search).get('scene') ?? ''; }
    catch { return ''; }
  })();
  const storeKey = () => 'fc.split.' + (docKey || '@' + sceneKey);

  let saveTimer: number | undefined;
  const save = () => {
    window.clearTimeout(saveTimer);
    saveTimer = window.setTimeout(() => {
      try {
        const r = root();
        if (r.cell) localStorage.removeItem(storeKey());
        else localStorage.setItem(
          storeKey(), JSON.stringify({ v: 1, tree: toSaved(r) }));
      }
      catch {}   // private mode, quota: no memory, no error
    }, 300);
  };

  /// Adopt the stored layout for the current key: restore it, or
  /// collapse to the single view when the (new) document has none.
  const adopt = () => {
    let r: Node | null = null;
    try {
      const raw = localStorage.getItem(storeKey());
      if (raw) {
        const s = JSON.parse(raw);
        if (s?.v === 1) r = fromSaved(s.tree);
      }
    }
    catch {}
    if (r && !r.cell) {
      const renumber = (n: Node) => {
        if (n.cell) n.id = nextId++;
        else { renumber(n.a); renumber(n.b); }
      };
      renumber(r);
      setRoot(r);
      push();
    }
    else if (!root().cell) {
      setRoot({ cell: true, id: nextId++, page: false });
      push();
    }
  };

  // Restore once the viewer side is up (the chrome usually mounts
  // before the WASM module registers its exports; a push before that
  // would be dropped on the floor), and re-key when the served
  // document becomes known or changes.
  const ready = window.setInterval(() => {
    if (!window.fcviewerSetLayout) return;
    window.clearInterval(ready);
    adopt();
  }, 250);
  const onDocs = (e: Event) => {
    const cur = (e as CustomEvent).detail?.current;
    const key = typeof cur === 'string' ? cur : '';
    if (key === docKey) return;
    docKey = key;
    // Not ready yet: the readiness poll above adopts with this key
    // when it fires.
    if (window.fcviewerSetLayout) adopt();
  };
  window.addEventListener('fc:docs', onDocs);
  window.addEventListener('fc:docswitch', onDocs);
  onCleanup(() => {
    window.clearInterval(ready);
    window.removeEventListener('fc:docs', onDocs);
    window.removeEventListener('fc:docswitch', onDocs);
  });

  // ---- Tree surgery -------------------------------------------------

  const parentOf = (n: Node, of: Node): SplitNode | null => {
    if (n.cell) return null;
    if (n.a === of || n.b === of) return n;
    return parentOf(n.a, of) ?? parentOf(n.b, of);
  };

  /// Split `cell` along `dir`, the cell keeping `ratio` of its extent;
  /// the fresh cell goes right/below (desktop parity: new splits
  /// always place the new cell after).
  const splitCell = (cell: CellNode, dir: 'h' | 'v',
                     ratio = 0.5): SplitNode => {
    const fresh: CellNode = { cell: true, id: nextId++, page: cell.page };
    const split: SplitNode = { cell: false, dir, ratio: clampRatio(ratio),
                               a: cell, b: fresh };
    const p = parentOf(root(), cell);
    if (!p) setRoot(split);
    else if (p.a === cell) p.a = split;
    else p.b = split;
    changed();
    return split;
  };

  /// Remove `cell`: its parent split collapses into the sibling.
  const closeCell = (cell: CellNode) => {
    const p = parentOf(root(), cell);
    if (!p) return;   // the last cell stays
    const sibling = p.a === cell ? p.b : p.a;
    const gp = parentOf(root(), p);
    if (!gp) setRoot(sibling);
    else if (gp.a === p) gp.a = sibling;
    else gp.b = sibling;
    // A tree collapsed to one cell pushes '' and the viewer shows its
    // ordinary wire-driven content -- reflect that here, or a lone
    // Page cell would claim content the C++ side is not showing.
    const r = root();
    if (r.cell) r.page = false;
    changed();
  };

  /// The neighbor cell an outward drag would consume: the sibling
  /// subtree across the exited border, only when it IS a single cell
  /// (the desktop's aligned-edge rule in tree form).
  const joinTargetFor = (cell: CellNode, dir: 'h' | 'v',
                         after: boolean): CellNode | null => {
    let n: Node = cell;
    for (;;) {
      const p = parentOf(root(), n);
      if (!p) return null;
      if (p.dir === dir) {
        const nIsA = p.a === n;
        // Exiting rightward/down needs our subtree on the a side;
        // leftward/up needs it on the b side.
        if (nIsA === after) {
          const sib = nIsA ? p.b : p.a;
          return sib.cell ? sib : null;
        }
      }
      n = p;
    }
  };

  // ---- Gestures -----------------------------------------------------
  //
  // The desktop's rules (ViewArea.cpp, docs/SplitViews.md sec 21 and
  // 22): nothing is split, joined, resized or closed while the button
  // is down. A drag is shown as frames over the cells it will change
  // and is carried out when the primary button is released; Escape,
  // any other button, a second finger, or the page losing the pointer
  // give it up.

  const cellRectOf = (cell: CellNode) =>
    layout().cells.find((c) => c.node === cell) ?? null;

  const showFrames = (op: string, list: Frame[]) => setFrames({ op, list });

  /// A split that cannot be: said the moment the cursor turns to the
  /// forbidden one, at every such turn -- the desktop's wording
  /// (ViewArea::reportRefusedSplit).
  let noteTimer: number | undefined;
  const refuse = (rect: CellRect, dir: 'h' | 'v') => {
    const text = `A view of ${Math.round(rect.w)} x ${Math.round(rect.h)} `
      + `is not split ${dir === 'h' ? 'side by side' : 'top and bottom'}: `
      + `no view cell is made smaller than ${MIN_CELL} x ${MIN_CELL} `
      + '(the minimum view cell size).';
    console.error(text);
    setNote(text);
    window.clearTimeout(noteTimer);
    noteTimer = window.setTimeout(() => setNote(''), 6000);
  };

  /// The cursor of a split that cannot be, wherever the pointer is: the
  /// elements under it have cursors of their own, so it is a class on
  /// the document that overrules them all.
  const forbid = (on: boolean) =>
    document.documentElement.classList.toggle('fc-split-forbidden', on);

  /// One drag: `onMove` works out what the release would do and shows
  /// it, `onCommit` does it. The listeners are on the WINDOW, not the
  /// pressed element: the chrome re-renders under a drag, and an
  /// element's own pointer capture dies with the element.
  const beginDrag = (ev: PointerEvent,
                     onMove: (mv: PointerEvent) => void,
                     onCommit: () => void) => {
    const id = ev.pointerId;
    let done = false;
    const finish = (commit: boolean) => {
      if (done) return;
      done = true;
      window.removeEventListener('pointermove', move, true);
      window.removeEventListener('pointerup', up, true);
      window.removeEventListener('pointercancel', abort, true);
      window.removeEventListener('pointerdown', down, true);
      window.removeEventListener('keydown', key, true);
      window.removeEventListener('blur', abort);
      document.removeEventListener('visibilitychange', abort);
      showFrames('', []);
      forbid(false);
      if (commit) onCommit();
    };
    const abort = () => finish(false);
    const move = (mv: PointerEvent) => {
      if (mv.pointerId !== id) return;
      // Another button of the same mouse going down is not a
      // pointerdown: it arrives as a move with more buttons held.
      if ((mv.buttons & ~1) !== 0) {
        // ... and the right one brings a context menu at its release,
        // which is not what that click was for.
        const eat = (c: Event) => { c.preventDefault(); c.stopPropagation(); };
        window.addEventListener('contextmenu', eat, true);
        window.setTimeout(
          () => window.removeEventListener('contextmenu', eat, true), 800);
        finish(false);
        return;
      }
      onMove(mv);
    };
    const up = (u: PointerEvent) => {
      if (u.pointerId !== id) return;
      finish(u.button === 0);
    };
    // A second finger (or another pointer of any kind) going down.
    const down = (d: PointerEvent) => { if (d.pointerId !== id) finish(false); };
    const key = (k: KeyboardEvent) => {
      if (k.key !== 'Escape') return;
      k.preventDefault();
      k.stopPropagation();
      finish(false);
    };
    window.addEventListener('pointermove', move, true);
    window.addEventListener('pointerup', up, true);
    window.addEventListener('pointercancel', abort, true);
    window.addEventListener('pointerdown', down, true);
    window.addEventListener('keydown', key, true);
    window.addEventListener('blur', abort);
    document.addEventListener('visibilitychange', abort);
    ev.preventDefault();
    ev.stopPropagation();
  };

  /// A corner zone. Dragged INTO its cell it only ever creates: a split
  /// along the dominant axis with the border under the cursor, refused
  /// when a cell would go under the minimum. Dragged OUT into the
  /// neighbor it arms a join, which closes that neighbor.
  const zoneDown = (cell: CellNode, ev: PointerEvent) => {
    if (ev.button !== 0) return;
    const rect = cellRectOf(cell);
    if (!rect) return;
    const startX = ev.clientX;
    const startY = ev.clientY;
    let split: { dir: 'h' | 'v'; at: number } | null = null;
    let join: CellNode | null = null;
    let refused = false;
    /// The cursor follows whether the split under way can be; the turn
    /// TO the forbidden one says why, each time.
    const setRefused = (now: boolean, dir: 'h' | 'v') => {
      if (now && !refused) refuse(rect, dir);
      if (now !== refused) forbid(now);
      refused = now;
    };
    const onMove = (mv: PointerEvent) => {
      split = null;
      join = null;
      const px = mv.clientX - canvasLeft();
      const py = mv.clientY - canvasTop();
      const inside = px >= rect.x && px < rect.x + rect.w
          && py >= rect.y && py < rect.y + rect.h;
      const dx = mv.clientX - startX;
      const dy = mv.clientY - startY;
      if (inside) {
        // Back at the press point: nothing armed.
        if (Math.abs(dx) + Math.abs(dy) < THRESHOLD) {
          setRefused(false, 'h');
          showFrames('', []);
          return;
        }
        const dir: 'h' | 'v' = Math.abs(dx) >= Math.abs(dy) ? 'h' : 'v';
        const along = dir === 'h' ? rect.w : rect.h;
        const across = dir === 'h' ? rect.h : rect.w;
        // Both halves, and the side the new cell inherits.
        if (MIN_CELL > 0 && (along / 2 < MIN_CELL || across < MIN_CELL)) {
          // No frame: the forbidden cursor, and the reason.
          setRefused(true, dir);
          showFrames('', []);
          return;
        }
        setRefused(false, dir);
        const least = MIN_CELL > 0 ? MIN_CELL : along * MIN_RATIO;
        const at = Math.min(along - least, Math.max(
          least, dir === 'h' ? px - rect.x : py - rect.y));
        split = { dir, at };
        showFrames('split', dir === 'h'
          ? [{ x: rect.x, y: rect.y, w: at, h: rect.h, kind: 'kept' },
             { x: rect.x + at, y: rect.y, w: rect.w - at, h: rect.h,
               kind: 'fresh' }]
          : [{ x: rect.x, y: rect.y, w: rect.w, h: at, kind: 'kept' },
             { x: rect.x, y: rect.y + at, w: rect.w, h: rect.h - at,
               kind: 'fresh' }]);
        return;
      }
      // Outward: a join that consumes the neighbor the cursor entered;
      // dragging back disarms.
      setRefused(false, 'h');
      let dir: 'h' | 'v';
      let after: boolean;
      if (px >= rect.x + rect.w) { dir = 'h'; after = true; }
      else if (px < rect.x) { dir = 'h'; after = false; }
      else if (py >= rect.y + rect.h) { dir = 'v'; after = true; }
      else { dir = 'v'; after = false; }
      const target = joinTargetFor(cell, dir, after);
      const going = target ? cellRectOf(target) : null;
      if (!target || !going) {
        showFrames('', []);
        return;
      }
      join = target;
      // The cell that stays, over the room of both, and the one that
      // goes, red and crossed out.
      const x0 = Math.min(rect.x, going.x);
      const y0 = Math.min(rect.y, going.y);
      const x1 = Math.max(rect.x + rect.w, going.x + going.w);
      const y1 = Math.max(rect.y + rect.h, going.y + going.h);
      showFrames('join', [
        { x: x0, y: y0, w: x1 - x0, h: y1 - y0, kind: 'kept' },
        { x: going.x, y: going.y, w: going.w, h: going.h, kind: 'going' }]);
    };
    const onCommit = () => {
      // a refused split has said so already, when the cursor turned
      if (join) closeCell(join);
      else if (split)
        splitCell(cell, split.dir,
                  split.at / (split.dir === 'h' ? rect.w : rect.h));
    };
    beginDrag(ev, onMove, onCommit);
  };

  const canvasLeft = () => {
    const c = document.getElementById('canvas');
    return c ? c.getBoundingClientRect().left : 0;
  };
  const canvasTop = () => {
    const c = document.getElementById('canvas');
    return c ? c.getBoundingClientRect().top : 0;
  };

  /// The least a subtree can be along `dir` with every cell in it at
  /// the minimum cell size or more, its own ratios as they are.
  const minExtent = (n: Node, dir: 'h' | 'v'): number => {
    if (n.cell) return MIN_CELL;
    if (n.dir !== dir)
      return Math.max(minExtent(n.a, dir), minExtent(n.b, dir));
    const r = clampRatio(n.ratio);
    return Math.max(minExtent(n.a, dir) / r, minExtent(n.b, dir) / (1 - r));
  };

  /// A border. Every cell the move resizes gets a frame at the size it
  /// will have; the border stops where a cell would go under the
  /// minimum, and pushed well past that it closes the cell it is pushed
  /// into (when that side is one cell).
  const handleDown = (split: SplitNode, ev: PointerEvent) => {
    if (ev.button !== 0) return;
    let ratio: number | null = null;
    let close: CellNode | null = null;
    const onMove = (mv: PointerEvent) => {
      ratio = null;
      close = null;
      const un = unionRect(split);
      if (!un) return;
      const horiz = split.dir === 'h';
      const total = horiz ? un.w : un.h;
      if (total <= 0) return;
      const pos = horiz ? mv.clientX - canvasLeft() - un.x
                        : mv.clientY - canvasTop() - un.y;
      const now = total * clampRatio(split.ratio);
      let lo = Math.max(total * MIN_RATIO, minExtent(split.a, split.dir));
      let hi = Math.min(total * (1 - MIN_RATIO),
                        total - minExtent(split.b, split.dir));
      // No room for both minimums (a small window): the border can
      // only stay, or give way toward where it already is.
      if (lo > hi) { lo = Math.min(lo, now); hi = Math.max(hi, now); }
      if (lo > hi) lo = hi = now;
      const legal = Math.min(hi, Math.max(lo, pos));
      if (Math.abs(pos - legal) > CLOSE_SLACK) {
        const pushed = pos < legal ? split.a : split.b;
        const going = pushed.cell ? cellRectOf(pushed) : null;
        if (pushed.cell && going) {
          close = pushed;
          showFrames('close', [
            { x: un.x, y: un.y, w: un.w, h: un.h, kind: 'kept' },
            { x: going.x, y: going.y, w: going.w, h: going.h,
              kind: 'going' }]);
          return;
        }
      }
      ratio = legal / total;
      const then = layout({ split, ratio }).cells;
      const list: Frame[] = [];
      for (const c of layout().cells) {
        if (!contains(split, c.node)) continue;
        const t = then.find((k) => k.node === c.node);
        if (!t) continue;
        if (Math.abs(t.x - c.x) > 0.5 || Math.abs(t.y - c.y) > 0.5
            || Math.abs(t.w - c.w) > 0.5 || Math.abs(t.h - c.h) > 0.5)
          list.push({ x: t.x, y: t.y, w: t.w, h: t.h, kind: 'kept' });
      }
      showFrames(list.length ? 'resize' : '', list);
    };
    const onCommit = () => {
      if (close) closeCell(close);
      else if (ratio !== null) {
        split.ratio = ratio;
        changed();
      }
    };
    beginDrag(ev, onMove, onCommit);
  };

  const unionRect = (split: SplitNode) => {
    const cs = layout().cells.filter((c) => contains(split, c.node));
    if (!cs.length) return null;
    const x0 = Math.min(...cs.map((c) => c.x));
    const y0 = Math.min(...cs.map((c) => c.y));
    const x1 = Math.max(...cs.map((c) => c.x + c.w));
    const y1 = Math.max(...cs.map((c) => c.y + c.h));
    return { x: x0, y: y0, w: x1 - x0, h: y1 - y0 };
  };
  const contains = (n: Node, cell: CellNode): boolean =>
    n.cell ? n === cell : contains(n.a, cell) || contains(n.b, cell);

  const setPage = (cell: CellNode, page: boolean) => {
    cell.page = page;
    changed();
  };

  // Per-cell path tracing (docs/CyclesIntegration.md sec 7.1): the
  // backend traces a cell from its own camera and streams the frame
  // into it. The device list comes from the viewer menu's ask (window
  // mirror + event, so a chrome mounting either side of it reads it);
  // the state is the viewer's, per cell, as 'fc:cycles'.
  const [ptDevices, setPtDevices] = createSignal<CyclesDevice[]>(
    window.fcviewerCyclesDevices ?? []);
  const [ptState, setPtState] = createSignal<Record<number, CyclesState>>(
    { ...(window.fcviewerCycles ?? {}) });
  const onPtDevices = (e: Event) => {
    const d = (e as CustomEvent).detail;
    setPtDevices(Array.isArray(d) ? d : []);
  };
  const onPtState = (e: Event) => {
    const d = (e as CustomEvent).detail;
    if (d && typeof d.cell === 'number')
      setPtState((s) => ({ ...s, [d.cell]: d as CyclesState }));
  };
  window.addEventListener('fc:cyclesdevices', onPtDevices);
  window.addEventListener('fc:cycles', onPtState);
  onCleanup(() => {
    window.removeEventListener('fc:cyclesdevices', onPtDevices);
    window.removeEventListener('fc:cycles', onPtState);
  });
  const ptValue = (id: number) => {
    const s = ptState()[id];
    return s && s.on ? s.device : '';
  };
  const ptTitle = (id: number) => {
    const s = ptState()[id];
    if (!s || !s.on) return 'Path trace this view on the backend';
    if (s.error) return `Path tracing: ${s.error}`;
    return `Path tracing on ${s.device}: ${Math.round(s.progress * 100)}%`;
  };

  const multi = () => !root().cell;

  /// A kept frame's face leaves the room of a cell that goes to that
  /// cell's own red frame: two faces one on the other are neither
  /// colour. The going cell lies along one side of the kept frame.
  const clipOf = (f: Frame): string | undefined => {
    if (f.kind !== 'kept') return undefined;
    const g = frames().list.find((o) => o.kind === 'going');
    if (!g) return undefined;
    const top = Math.max(0, g.y + g.h - f.y);
    const left = Math.max(0, g.x + g.w - f.x);
    const bottom = Math.max(0, f.y + f.h - g.y);
    const right = Math.max(0, f.x + f.w - g.x);
    const across = g.w >= f.w - 1;    // one above the other
    if (across)
      return g.y <= f.y + 1 ? `inset(${top}px 0 0 0)` : `inset(0 0 ${bottom}px 0)`;
    return g.x <= f.x + 1 ? `inset(0 0 0 ${left}px)` : `inset(0 ${right}px 0 0)`;
  };

  return (
    <div class="fc-split-root" data-op={frames().op}>
      <For each={layout().cells}>
        {(c) => (
          <div class="fc-split-cell"
               style={{ left: `${c.x}px`, top: `${c.y}px`,
                        width: `${c.w}px`, height: `${c.h}px` }}>
            <div class="fc-split-zone fc-split-zone-tr"
                 title="Drag in to split, out to join"
                 onPointerDown={[zoneDown, c.node]} />
            <div class="fc-split-zone fc-split-zone-bl"
                 title="Drag in to split, out to join"
                 onPointerDown={[zoneDown, c.node]} />
            {multi() && (
              <div class="fc-split-chip">
                <button classList={{ on: !c.node.page }}
                        onClick={() => setPage(c.node, false)}>3D</button>
                <button classList={{ on: c.node.page }}
                        onClick={() => setPage(c.node, true)}>Page</button>
                <Show when={!c.node.page && ptDevices().length > 0}>
                  <select class="fc-split-pt"
                          classList={{ on: ptValue(c.node.id) !== '' }}
                          title={ptTitle(c.node.id)}
                          value={ptValue(c.node.id)}
                          onChange={(e) => window.fcviewerSetCycles?.(
                            e.currentTarget.value, c.node.id)}>
                    <option value="">PT off</option>
                    <For each={ptDevices()}>
                      {(d) => <option value={d.type}>PT {d.type}</option>}
                    </For>
                  </select>
                </Show>
                <button class="fc-split-close" title="Close this view"
                        onClick={() => closeCell(c.node)}>x</button>
              </div>
            )}
          </div>
        )}
      </For>
      <For each={layout().handles}>
        {(h) => (
          <div class="fc-split-handle" classList={{
                 vert: h.node.dir === 'h' }}
               style={{ left: `${h.x}px`, top: `${h.y}px`,
                        width: `${h.w}px`, height: `${h.h}px` }}
               onPointerDown={[handleDown, h.node]} />
        )}
      </For>
      <For each={frames().list}>
        {(f) => (
          <div class={`fc-split-frame fc-split-frame-${f.kind}`}
               data-kind={f.kind}
               style={{ left: `${f.x}px`, top: `${f.y}px`,
                        width: `${f.w}px`, height: `${f.h}px`,
                        'clip-path': clipOf(f) }}>
            {f.kind === 'fresh' && <div class="fc-split-plus" />}
            {f.kind === 'going' && (
              <svg class="fc-split-cross" viewBox="0 0 48 48">
                <path d="M8 8L40 40M40 8L8 40" fill="none"
                      stroke="#c82828" stroke-width="5"
                      stroke-linecap="round" />
              </svg>
            )}
          </div>
        )}
      </For>
      <Show when={note() !== ''}>
        <div class="fc-split-note" role="alert">{note()}</div>
      </Show>
    </div>
  );
}
