// Split-view chrome (docs/SplitViews.md sec 9.4): a binary splitter
// tree of DOM cells over the single canvas, mirroring the desktop
// ViewArea gestures (ViewArea.cpp ViewAreaZone). The chrome owns the
// gestures -- corner action zones split and join, border handles
// resize -- and pushes the resulting cell rects to the viewer through
// window.fcviewerSetLayout; the WASM side owns cameras, content and
// input routing inside the cells (main.cpp fcviewer_set_layout).
//
// Cells are pointer-events: none so the canvas keeps every scene
// gesture; only the corner zones, the border handles and the per-cell
// content chip are interactive.
import { For, createSignal, onCleanup } from 'solid-js';

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
const ZONE = 14;
const THRESHOLD = 12;
const MIN_RATIO = 0.05;

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
  // The join gesture's armed target, for the dim + arrow overlay.
  const [joinMark, setJoinMark] = createSignal<{
    target: CellNode; dir: 'h' | 'v'; after: boolean } | null>(null);
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

  const layout = () => {
    const { w, h } = size();
    const cells: CellRect[] = [];
    const handles: HandleRect[] = [];
    const walk = (n: Node, x: number, y: number,
                  cw: number, ch: number) => {
      if (n.cell) {
        cells.push({ node: n, x, y, w: cw, h: ch });
        return;
      }
      const r = Math.min(1 - MIN_RATIO, Math.max(MIN_RATIO, n.ratio));
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

  /// Split `cell` along `dir`; the fresh cell goes right/below
  /// (desktop parity: new splits always place the new cell after).
  /// Returns the new split node so the drag can live-adjust its ratio.
  const splitCell = (cell: CellNode, dir: 'h' | 'v'): SplitNode => {
    const fresh: CellNode = { cell: true, id: nextId++, page: cell.page };
    const split: SplitNode = { cell: false, dir, ratio: 0.5,
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

  interface Drag {
    cell: CellNode;
    rect: CellRect;
    startX: number;
    startY: number;
    resize: SplitNode | null;    // set once a split happened
  }
  let drag: Drag | null = null;

  const cellRectOf = (cell: CellNode) =>
    layout().cells.find((c) => c.node === cell) ?? null;

  // The drag listens on the WINDOW, not the pressed element: every
  // tree change re-renders the cell divs, so the corner zone that
  // took the press dies mid-gesture -- element pointer capture then
  // silently drops the live resize and the pointerup, and the stale
  // drag corrupts the next gesture.
  const zoneDown = (cell: CellNode, ev: PointerEvent) => {
    if (ev.button !== 0) return;
    const rect = cellRectOf(cell);
    if (!rect) return;
    drag = { cell, rect, startX: ev.clientX, startY: ev.clientY,
             resize: null };
    const move = (mv: PointerEvent) => zoneMove(mv);
    const up = () => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
      zoneUp();
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
    ev.preventDefault();
  };
  const zoneMove = (ev: PointerEvent) => {
    if (!drag) return;
    // Once a split happened the rest of the drag adjusts the fresh
    // border, wherever the cursor goes.
    if (drag.resize) {
      const s = drag.resize;
      // The split's rect is the union of both sides = the pressed cell.
      const base = drag.rect;
      if (s.dir === 'h')
        s.ratio = (ev.clientX - canvasLeft() - base.x) / base.w;
      else
        s.ratio = (ev.clientY - canvasTop() - base.y) / base.h;
      s.ratio = Math.min(1 - MIN_RATIO, Math.max(MIN_RATIO, s.ratio));
      changed();
      return;
    }
    const px = ev.clientX - canvasLeft();
    const py = ev.clientY - canvasTop();
    const inside = px >= drag.rect.x && px < drag.rect.x + drag.rect.w
        && py >= drag.rect.y && py < drag.rect.y + drag.rect.h;
    const dx = ev.clientX - drag.startX;
    const dy = ev.clientY - drag.startY;
    if (inside) {
      // Back inside always cancels an armed join, even right at the
      // press point where the split threshold below is not met.
      setJoinMark(null);
      if (Math.abs(dx) + Math.abs(dy) < THRESHOLD) return;
      // Inward drag: split along the dominant axis.
      const dir: 'h' | 'v' = Math.abs(dx) >= Math.abs(dy) ? 'h' : 'v';
      drag.resize = splitCell(drag.cell, dir);
    }
    else {
      // Outward drag: arm a join that consumes the neighbor the
      // cursor entered; dragging back disarms.
      let dir: 'h' | 'v';
      let after: boolean;
      if (px >= drag.rect.x + drag.rect.w) { dir = 'h'; after = true; }
      else if (px < drag.rect.x) { dir = 'h'; after = false; }
      else if (py >= drag.rect.y + drag.rect.h) { dir = 'v'; after = true; }
      else { dir = 'v'; after = false; }
      const target = joinTargetFor(drag.cell, dir, after);
      setJoinMark(target ? { target, dir, after } : null);
    }
  };
  const zoneUp = () => {
    if (!drag) return;
    const mark = joinMark();
    setJoinMark(null);
    drag = null;
    if (mark) closeCell(mark.target);
  };

  const canvasLeft = () => {
    const c = document.getElementById('canvas');
    return c ? c.getBoundingClientRect().left : 0;
  };
  const canvasTop = () => {
    const c = document.getElementById('canvas');
    return c ? c.getBoundingClientRect().top : 0;
  };

  const handleDown = (split: SplitNode, ev: PointerEvent) => {
    if (ev.button !== 0) return;
    ev.preventDefault();
    // Window listeners for the same reason as zoneDown: the handle
    // div itself is recreated on every ratio change.
    const move = (mv: PointerEvent) => {
      // Recover the split's base rect from its children each move --
      // an ancestor resize may have moved it.
      const un = unionRect(split);
      if (!un) return;
      if (split.dir === 'h')
        split.ratio = (mv.clientX - canvasLeft() - un.x) / un.w;
      else
        split.ratio = (mv.clientY - canvasTop() - un.y) / un.h;
      split.ratio = Math.min(1 - MIN_RATIO,
                             Math.max(MIN_RATIO, split.ratio));
      changed();
    };
    const up = () => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
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

  const multi = () => !root().cell;

  return (
    <div class="fc-split-root">
      <For each={layout().cells}>
        {(c) => (
          <div class="fc-split-cell" classList={{
                 'fc-split-join': joinMark()?.target === c.node }}
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
                <button class="fc-split-close" title="Close this view"
                        onClick={() => closeCell(c.node)}>x</button>
              </div>
            )}
            {joinMark()?.target === c.node && (
              <svg class="fc-split-arrow" viewBox="0 0 24 24"
                   style={{ transform: `rotate(${
                     joinMark()!.dir === 'h'
                       ? (joinMark()!.after ? 180 : 0)
                       : (joinMark()!.after ? 270 : 90)}deg)` }}>
                <path d="M20 12H6M12 5l-7 7 7 7" fill="none"
                      stroke="currentColor" stroke-width="2.5"
                      stroke-linecap="round" stroke-linejoin="round" />
              </svg>
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
    </div>
  );
}
