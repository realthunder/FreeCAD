// The widget wire, in the page (docs/Sandbox.md 7.22, G7): the frames the
// desktop's widget stream pushes, reduced to the model store a view layer
// renders from.
//
// Pure on purpose -- no DOM, no Solid, no fetch. The gate replays recorded
// frames through this module in node (the ruled pure-plan gate, 7.22
// question 5), and the same reduction serves the task panel, the tool bars
// and whatever consumes the stream later. Everything that needs a document
// or a socket lives above it.
//
// What the host sends is described in 7.22 "What the walker consumes"; the
// three rules that are easy to get wrong, all learned from the recorded
// corpus rather than from the C++:
//
//   - an `open` REPLACES: the same id opens twice at subscribe time (the
//     mirror announces its list live to a client that is already
//     subscribed, then the deferred snapshot push sends it again), so
//     applying an open must be idempotent, as `Store::insert` is on the
//     host;
//   - children arrive BEFORE their container (`snapshotOrder` walks
//     referents first and leaves `parent` out), so a layout's refs always
//     resolve by the time the container opens -- no buffering needed;
//   - an `open` splices the snapshot at the top level of the frame, while
//     every other method nests its payload under `content`.

/// A property whose value is another model crosses as this prefix plus the
/// id, at any depth inside a value (a list, a map).
export const MODEL_REF = 'IPY_MODEL_';
/// Every state key carries it; the bag's own name is what follows.
export const STATE_PREFIX = 'q_';

export type Json = unknown;

/// One item of a layout: exactly one of the shape keys, plus placement.
export interface LayoutItem {
  widget?: string;
  layout?: LayoutSpec;
  action?: string;
  separator?: boolean;
  stretch?: number;
  spacing?: number;
  /// [width, height, hPolicy, vPolicy]
  spacer?: number[];
  /// [row, column, rowSpan, columnSpan] -- four wide, not two.
  pos?: number[];
  align?: number;
}

export interface LayoutSpec {
  class: string;
  name: string;
  items: LayoutItem[];
  margins?: number[];
  spacing?: number;
  [extra: string]: Json;
}

export interface ItemCell {
  text?: string;
  icon?: string;
  toolTip?: string;
  check?: number;
  flags?: number;
  fg?: string;
  bg?: string;
  bold?: boolean;
  align?: number;
}

export interface ItemRow {
  id: number;
  cells: ItemCell[];
  children?: ItemRow[];
  expanded?: boolean;
  hidden?: boolean;
  flags?: number;
}

/// One model as the page holds it. `state` keys have the `q_` stripped:
/// the prefix is the wire's business, not the view's.
export interface WidgetModel {
  id: string;
  model: string;
  qtClass: string;
  state: Record<string, Json>;
  layout?: LayoutSpec;
  items?: ItemRow[];
  parent?: string;
}

export interface Frame {
  op?: string;
  method: string;
  id: string;
  content?: Record<string, Json>;
  // an `open` carries the snapshot spliced at this level
  model?: string;
  qtClass?: string;
  state?: Record<string, Json>;
  layout?: LayoutSpec;
  items?: ItemRow[];
  parent?: string;
}

/// The id an `IPY_MODEL_<id>` reference names, or null for anything else.
export function refId(value: Json): string | null {
  return typeof value === 'string' && value.startsWith(MODEL_REF)
    ? value.slice(MODEL_REF.length)
    : null;
}

/// Every model id referenced anywhere inside a value -- a ref can sit at
/// any depth, because `refsOf` on the host recurses through lists and maps.
export function refsIn(value: Json, out: string[] = []): string[] {
  const direct = refId(value);
  if (direct) {
    out.push(direct);
  }
  else if (Array.isArray(value)) {
    for (const item of value) refsIn(item, out);
  }
  else if (value && typeof value === 'object') {
    for (const item of Object.values(value as Record<string, Json>)) refsIn(item, out);
  }
  return out;
}

/// The widget refs a layout names, its nested layouts included. Actions are
/// included: they are store objects too.
export function layoutRefs(layout: LayoutSpec | undefined, out: string[] = []): string[] {
  for (const item of layout?.items ?? []) {
    const widget = refId(item.widget);
    if (widget) out.push(widget);
    const action = refId(item.action);
    if (action) out.push(action);
    if (item.layout) layoutRefs(item.layout, out);
  }
  return out;
}

/// Drop the `q_` from the keys the wire prefixes; anything else (a
/// `layoutSpec`, a `qtClass`) is the caller's to handle.
export function stripState(state: Record<string, Json> | undefined): Record<string, Json> {
  const out: Record<string, Json> = {};
  for (const [key, value] of Object.entries(state ?? {})) {
    if (key.startsWith(STATE_PREFIX)) out[key.slice(STATE_PREFIX.length)] = value;
  }
  return out;
}

function findRow(rows: ItemRow[], id: number): ItemRow | null {
  for (const row of rows) {
    if (row.id === id) return row;
    const hit = row.children ? findRow(row.children, id) : null;
    if (hit) return hit;
  }
  return null;
}

/// What an item op did, so a view can repaint the rows rather than the tree
/// (8.4: Sketcher refills 162 ops in one solve -- the view coalesces per
/// frame, which it can only do if the store says what moved).
export type ItemChange = 'clear' | 'insert' | 'set' | 'none';

/// The models the page holds, and the reduction of one frame onto them.
export class WidgetStore {
  private models = new Map<string, WidgetModel>();
  /// The last item op's target, for a view that coalesces repaints.
  lastItemChange: ItemChange = 'none';

  get(id: string): WidgetModel | undefined {
    return this.models.get(id);
  }

  has(id: string): boolean {
    return this.models.has(id);
  }

  ids(): string[] {
    return [...this.models.keys()];
  }

  get size(): number {
    return this.models.size;
  }

  /// The models nothing else names through a layout: the panel root, a
  /// dialog root, the mirror's list.
  roots(): string[] {
    const named = new Set<string>();
    for (const model of this.models.values()) {
      for (const id of layoutRefs(model.layout)) named.add(id);
    }
    return this.ids().filter((id) => !named.has(id));
  }

  /// Apply one frame. Returns false for a method this store does not know,
  /// so a caller can log it rather than fail silently.
  apply(frame: Frame): boolean {
    switch (frame.method) {
      case 'open':
        return this.open(frame);
      case 'close':
        this.models.delete(frame.id);
        return true;
      // "state" and "update" both carry state; only update also carries a
      // rebuilt layout.
      case 'state':
      case 'update':
        return this.update(frame);
      case 'custom':
        return this.custom(frame);
      default:
        return false;
    }
  }

  /// An open is a REPLACE, never an insert: see the header. Anything the
  /// page had under this id is dropped with it.
  private open(frame: Frame): boolean {
    this.models.set(frame.id, {
      id: frame.id,
      model: frame.model ?? 'QWidgetModel',
      qtClass: frame.qtClass ?? '',
      state: stripState(frame.state),
      layout: frame.layout,
      items: frame.items,
      parent: refId(frame.parent) ?? undefined,
    });
    return true;
  }

  private update(frame: Frame): boolean {
    const model = this.models.get(frame.id);
    if (!model) return false;
    const content = frame.content ?? {};
    // a rebuilt layout rides an update beside the state keys
    if ('layoutSpec' in content) {
      const spec = content.layoutSpec;
      model.layout = spec && typeof spec === 'object' ? (spec as LayoutSpec) : undefined;
    }
    Object.assign(model.state, stripState(content as Record<string, Json>));
    return true;
  }

  /// A custom is an item op on a view's rows, or an event the widget
  /// emitted. Events change no state, so the store only reports them.
  private custom(frame: Frame): boolean {
    const model = this.models.get(frame.id);
    if (!model) return false;
    const content = frame.content ?? {};
    const op = content.item;
    if (typeof op !== 'string') {
      this.lastItemChange = 'none';
      return true;
    }
    if (op === 'clear') {
      model.items = [];
      this.lastItemChange = 'clear';
      return true;
    }
    if (op === 'insert') {
      const parent = Number(content.parent ?? 0);
      const rows = (content.rows as ItemRow[]) ?? [];
      const into = parent === 0
        ? (model.items ??= [])
        : findRow(model.items ?? [], parent)?.children;
      if (!into) return false;
      const index = Number(content.index ?? into.length);
      into.splice(index < 0 || index > into.length ? into.length : index, 0, ...rows);
      this.lastItemChange = 'insert';
      return true;
    }
    if (op === 'set') {
      const row = findRow(model.items ?? [], Number(content.id));
      if (!row) return false;
      const col = Number(content.col ?? 0);
      while (row.cells.length <= col) row.cells.push({});
      row.cells[col] = { ...row.cells[col], ...(content.cell as ItemCell) };
      this.lastItemChange = 'set';
      return true;
    }
    return false;
  }
}
