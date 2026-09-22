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

/// One cell, as `Fw::ItemCell::toMap` writes it: SPARSE, so a key that is
/// not here is unset rather than empty -- `check` absent means the cell has
/// no check box at all, which is the host's own rule (an invalid QVariant),
/// not a check box that happens to be clear.
export interface ItemCell {
  text?: string;
  icon?: string;
  toolTip?: string;
  statusTip?: string;
  whatsThis?: string;
  /// Qt::CheckState as an int; absent means no check box.
  check?: number;
  flags?: number;
  /// A colour as the host's QVariantList, not a CSS string.
  fg?: number[];
  bg?: number[];
  bold?: boolean;
  align?: number;
}

/// Qt::ItemFlag, the two a row's `flags` is read for here.
export const ITEM_ENABLED = 32;
export const ITEM_CHECKABLE = 16;
/// Qt::CheckState.
export const CHECK_OFF = 0;
export const CHECK_ON = 2;

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

/// The list a row sits in and where: a remove has to splice the PARENT's
/// own array, and the tree is nested here rather than flat with parent ids
/// the way the host holds it.
function locate(rows: ItemRow[], id: number): { list: ItemRow[]; index: number } | null {
  const index = rows.findIndex((row) => row.id === id);
  if (index >= 0) return { list: rows, index };
  for (const row of rows) {
    const hit = row.children ? locate(row.children, id) : null;
    if (hit) return hit;
  }
  return null;
}

/// Qt sorts on the column's text, and `order` is a Qt::SortOrder (0
/// ascending). One level only: the host's op sorts the whole view when its
/// id is 0 and that row's children otherwise (FwQtView `sort`), so this
/// does the same rather than recursing.
function sortRows(rows: ItemRow[], col: number, order: number): void {
  rows.sort((a, b) => {
    const left = a.cells[col]?.text ?? '';
    const right = b.cells[col]?.text ?? '';
    return order === 1 ? right.localeCompare(left) : left.localeCompare(right);
  });
}

/// What an item op did, so a view can repaint the rows rather than the tree
/// (8.4: Sketcher refills 162 ops in one solve -- the view coalesces per
/// frame, which it can only do if the store says what moved).
export type ItemChange = 'clear' | 'insert' | 'set' | 'row' | 'remove' | 'sort' | 'none';

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
    // The row's own state rather than a cell's. The recorded corpus has
    // none of these -- a Sketcher list never nests -- but a tree sends one
    // the moment anything is expanded, collapsed or hidden, and a store
    // that dropped it would fail the gate's "every frame applied" against
    // the first real tree instead of here.
    if (op === 'row') {
      const row = findRow(model.items ?? [], Number(content.id));
      if (!row) return false;
      const patch = (content.row ?? {}) as Partial<ItemRow>;
      if (patch.expanded !== undefined) row.expanded = patch.expanded;
      if (patch.hidden !== undefined) row.hidden = patch.hidden;
      if (patch.flags !== undefined) row.flags = patch.flags;
      this.lastItemChange = 'row';
      return true;
    }
    if (op === 'remove') {
      const at = locate(model.items ?? [], Number(content.id));
      if (!at) return false;
      // The children go with it: the host erases the subtree
      // (`ItemView::eraseRow` recurses), and here they hang off the row
      // being spliced out, so they leave with their parent.
      at.list.splice(at.index, 1);
      this.lastItemChange = 'remove';
      return true;
    }
    if (op === 'sort') {
      const id = Number(content.id ?? 0);
      const list = id === 0
        ? model.items
        : findRow(model.items ?? [], id)?.children;
      if (!list) return false;
      sortRows(list, Number(content.col ?? 0), Number(content.order ?? 0));
      this.lastItemChange = 'sort';
      return true;
    }
    return false;
  }
}

/// An item OP a client sends back -- the same shape the host pushes out.
///
/// The op/event distinction decides whether anything happens on the
/// desktop, so it is worth stating once. `Fw::Store::commCustom` sends a
/// content carrying `item` to `ItemView::applyItemOp`, which changes the
/// rows AND calls `emitItemOp` -> `Backend::itemsChanged` -> the real Qt
/// widget -> the panel's own slot. A content carrying `event` instead
/// reaches `Widget::dispatchEvent`, which for an item view updates the
/// model's private copy and calls no backend at all. So a write sent as an
/// event passes every pure check here and does nothing on screen.
///
/// `itemEdited` travels the OTHER way: the backend emits it (FwQtView)
/// when the desktop user edits a cell. It is not a client's to send.
export interface ItemOp {
  item: string;
  id?: number;
  col?: number;
  cell?: ItemCell;
  row?: Partial<ItemRow>;
  parent?: number;
  index?: number;
  rows?: ItemRow[];
  order?: number;
  /// As `LayoutSpec` carries one: without it an interface is not a
  /// `Record<string, unknown>`, and these go straight to `client.custom`.
  [extra: string]: Json;
}

/// An event a client sends back to a mirrored widget -- a notification,
/// not a change. `commCustom` reads `event` and `args` and hands them to
/// `Widget::dispatchEvent`.
export interface ItemEvent {
  event: string;
  args: Json[];
  [extra: string]: Json;
}

/// A check write, and any other single-cell edit: a `set`, which is what
/// reaches the desktop. The host's own test writes a constraint's check
/// exactly this way and asserts the sketch really moves it to virtual
/// space (Mod/Test/SandboxPanelMirror.py `test_sketcher_constraints`).
export function itemEditOp(id: number, col: number, cell: ItemCell): ItemOp {
  return { item: 'set', id, col, cell };
}

/// Expand or collapse one row. A `row` op, because that is the one the
/// backend turns into `setExpanded` on the real tree; the host echoes it
/// back like any other op.
export function itemExpandOp(id: number, expanded: boolean): ItemOp {
  return { item: 'row', id, row: { expanded } };
}

/// A click, for a panel that acts on one. Genuinely an event -- it
/// notifies, and it does NOT move the selection; see `selectionWrite`.
export function itemClickOp(id: number, col: number, twice = false): ItemEvent {
  return { event: twice ? 'itemDoubleClicked' : 'itemClicked', args: [id, col] };
}

/// Selection is STATE, not an event. The backend drives the desktop's real
/// `selectionModel` from these properties and sends them back when the
/// desktop user selects (FwQtView), so a client's click writes them
/// through `widgets.update`. Sending `itemClicked` instead would fire the
/// panel's click handlers while the selection never moved.
export function selectionWrite(ids: number[], current: number, column = 0): {
  selection: number[];
  currentId: number;
  currentColumn: number;
} {
  return { selection: ids, currentId: current, currentColumn: column };
}

/// The two kinds of root the host mints (Gui/Fw/FwPanelMirror.cpp `owns`):
/// the task panel is `panel:<n>`, a top-level dialog is `dialog:<n>`.
/// Both are `QDialogModel`s and both carry a button box, so the ID is what
/// tells them apart -- and they are ANSWERED differently, which is the
/// whole of W4's write path.
export function isDialogRoot(id: string): boolean {
  return id.startsWith('dialog:');
}

/// A dialog's answer (7.19 M3): the standard button the client chose, sent
/// to the ROOT rather than to the button that carries it.
///
/// The host turns this into the window's `done(button)`, so for a panel
/// slot blocked in `QMessageBox::exec()` this op IS the exec code coming
/// back. Ground truth is the host's own test -- Mod/Test/
/// SandboxPanelMirror.py `test_nested_messagebox` answers Yes exactly this
/// way and asserts the slot returned 0x4000 -- and `onDialogRequest`
/// (Gui/Fw/FwPanelMirror.cpp) is where the mirror accepts it.
export function dialogClickOp(standardButton: number): { event: string; args: number[] } {
  return { event: 'clicked', args: [standardButton] };
}

/// Escape on a dialog, which is `QDialog::reject()` on the bound view --
/// Qt's own answer for a box dismissed rather than answered. A box with an
/// escape button resolves it to that button; one without returns 0, the
/// same as pressing Escape at the desktop.
export function dialogRejectOp(): { event: string } {
  return { event: 'reject' };
}

/// Gui::FileChooser::Mode. A directory chooser cannot be served by an
/// upload -- a browser picks files, not folders -- so the view says so
/// rather than drawing a button that cannot work.
export const CHOOSER_FILE = 0;
export const CHOOSER_DIRECTORY = 1;

/// A path the CLIENT chose, sent to a mirrored `Gui::FileChooser`
/// (docs/Sandbox.md 7.22, W4b).
///
/// NOT a property write, and the difference is the whole of this stage's
/// write path. Writing `fileName` moves the host's line edit and emits
/// `fileNameChanged`, while a panel's slot is connected to
/// `fileNameSelected` -- TechDraw's hatch and welding panels and every
/// FEM settings page take that one, and only SymbolChooser takes the
/// other. So a write alone would leave the panel unfired. The host turns
/// this request into the same ending its own dialog has (`View::
/// onRequest`, Gui/Fw/FwQtView.cpp): the name set, then the widget's own
/// `editingFinished`, which emits `fileNameSelected`.
export function fileSelectedOp(path: string): { event: string; args: string[] } {
  return { event: 'fileSelected', args: [path] };
}

/// A Qt name filter as the `accept` of a file input.
///
/// Qt writes `Fonts (*.ttf *.otf);;All files (*)`; the web wants
/// `.ttf,.otf`. A bare `*` matches nothing here by construction (the
/// pattern needs a dot and a name), so an all-files filter yields the
/// empty string -- which is what offers EVERY file. Writing the `*`
/// through would offer none.
export function acceptFromFilter(filter: string): string {
  const out: string[] = [];
  for (const match of filter.matchAll(/\*(\.[A-Za-z0-9_+-]+)/g)) {
    const ext = match[1].toLowerCase();
    if (!out.includes(ext)) out.push(ext);
  }
  return out.join(',');
}
