// The control client (docs/ThinClient.md §3): id-correlated JSON
// operations over the scene socket's text lane. The WASM viewer owns
// the socket; it exposes window.fcviewerControlSend and re-dispatches
// answers as window 'fc:control' events — this module turns that pair
// into promises.

declare global {
  interface Window {
    fcviewerControlSend?: (json: string) => boolean;
    /// Turn the renderer HUD on or off (main.cpp fcviewer_set_hud).
    fcviewerSetHud?: (on: boolean) => void;
    /// The NaviCube view menu's actions (main.cpp fcviewer_navi_action):
    /// 0 isometric, 1 dimetric, 2 trimetric, 3 fit all, 4 reset the
    /// cube to the served view's place for it.
    fcviewerNaviAction?: (action: number) => void;
    /// Paint {obj, sub} in the hover colour, '' to drop it (main.cpp
    /// fcviewer_hover_named): the context menu's "Pick geometry" entries.
    fcviewerHoverNamed?: (obj: string, sub: string) => void;
    /// A pick the server made for this client, into its own selection
    /// (main.cpp fcviewer_select_named).
    fcviewerSelectNamed?: (obj: string, sub: string, extend: boolean) => void;
    /// Hold the scene's own preselection while a menu has the pointer
    /// (main.cpp fcviewer_hold_hover).
    fcviewerHoldHover?: (on: boolean) => void;
    /// Selection menu (main.cpp, docs/ThinClientUI.md): mode 0 single /
    /// 1 multi; filter 0 elements / 1 object / 2 face / 3 edge /
    /// 4 vertex.
    fcviewerSetSelMode?: (mode: number) => void;
    fcviewerSetPickFilter?: (filter: number) => void;
    /// Split-view layout push (main.cpp fcviewer_set_layout,
    /// docs/SplitViews.md sec 9.4): "id,x,y,w,h,p;..." in CSS px on
    /// the canvas, empty string = single full-canvas view.
    fcviewerSetLayout?: (spec: string) => void;
    /// Switch to another served document (main.cpp fcviewer_switch_doc,
    /// docs/MultiDocServe.md §6).
    fcviewerSwitchDoc?: (name: string) => void;
    /// Forward one keystroke to the edit session on the same wire the
    /// canvas uses (main.cpp fcviewer_send_key, docs/ThinClient.md sec
    /// 8.7). An on-view entry box holds the DOM focus while it is being
    /// typed into, so the canvas sees none of its keys; what each key
    /// MEANS is decided on the server, which is why this is a pipe and
    /// not a handler. Returns false when there is no session to send to.
    fcviewerSendKey?: (down: boolean, key: string, text: string,
                       mods: number) => boolean;
    /// The document's undo and redo (main.cpp fcviewer_undo_redo,
    /// docs/ThinClient.md 8.11 item 2): one step each, everyone's stack.
    /// The canvas answers Ctrl+Z / Ctrl+Y itself; these are for chrome.
    fcviewerUndo?: () => boolean;
    fcviewerRedo?: () => boolean;
    /// The on-view parameters the server last stated, mirrored for a
    /// panel that mounts after the push ('fc:onview' carries the same).
    fcviewerOnView?: unknown[];
    /// Name this connection for the host's sharing roster (main.cpp
    /// fcviewer_set_client, docs/MultiDocServe.md §6); persisted in
    /// localStorage by the viewer side.
    fcviewerSetClient?: (name: string) => void;
    fcviewerClientName?: () => string;
    /// Mirror of the current name, set by the viewer alongside the
    /// 'fc:client' event so a late-mounting panel still reads it.
    fcviewerClient?: string;
    /// Whether the host has made this connection view-only
    /// (docs/MultiDocServe.md §8). Mirrored here by the viewer so a
    /// panel mounting after the push still reads the current mode;
    /// changes arrive as 'fc:viewonly' events.
    fcviewerViewOnly?: boolean;
    /// This connection's access level (docs/ShareAccess.md sec 2.2), as
    /// the backend last said; changes arrive as 'fc:access' events. Absent
    /// from a backend that predates the levels.
    fcviewerAccess?: 'view' | 'edit' | 'host';
    /// Set by this layer to claim the HUD feed: the viewer then reports
    /// it as 'fc:hud' events instead of drawing its own overlay box.
    fcviewerHudCard?: boolean;
    /// The served viewport (main.cpp fcviewer_set_cycles,
    /// docs/CyclesIntegration.md sec 7.1): a device type starts the
    /// backend path tracing this view, '' stops it. State arrives as
    /// 'fc:cycles' events and is mirrored here for a late mount.
    fcviewerSetCycles?: (device: string, cell: number) => void;
    fcviewerCycles?: Record<number, CyclesState>;
    /// The backend's path-tracing devices, published by main.tsx once
    /// asked (also an 'fc:cyclesdevices' event) for the split chrome's
    /// per-cell control.
    fcviewerCyclesDevices?: CyclesDevice[];
    /// Whether the scene socket is up; changes arrive as 'fc:connection'
    /// events (main.cpp fcviewer_connection_event). A new connection has
    /// none of the old one's subscriptions, which is what the event is for.
    fcviewerConnected?: boolean;
    /// State this client's camera to the server now, moved or not
    /// (main.cpp fcviewer_state_camera): an op that runs in the client's
    /// view is refused for a client that has never stated one.
    fcviewerStateCamera?: () => boolean;
    /// The object this client's edit session is on, or null (main.cpp
    /// fcviewer_edit_event); changes arrive as 'fc:edit' events.
    fcviewerEditing?: string | null;
  }
}

export interface CyclesDevice { type: string; description: string }

/// What the viewer says about one served viewport ('fc:cycles'): the
/// sub-view it is for (0 = the full canvas, else a split cell's id)
/// and its state.
export interface CyclesState {
  cell: number;
  on: boolean;
  device: string;
  progress: number;
  status: string;
  error: string;
  width: number;
  height: number;
  frames: number;
}


export interface ControlError {
  code: string;
  message?: string;
}

let nextId = 1;
const pending = new Map<
  number,
  { resolve: (v: any) => void; reject: (e: ControlError) => void; timer: number }
>();

/// Subscribers to unsolicited pushes, by op name. The control lane
/// carries both answers (correlated by id) and pushes (no id) — a push
/// is the backend saying something changed, never a reply.
const pushHandlers = new Map<string, Set<(msg: any) => void>>();

/// Listen for a pushed op. Returns the unsubscribe.
export function onPush(op: string, fn: (msg: any) => void): () => void {
  let set = pushHandlers.get(op);
  if (!set) { set = new Set(); pushHandlers.set(op, set); }
  set.add(fn);
  return () => { set!.delete(fn); };
}

window.addEventListener('fc:control', (e: Event) => {
  const d = (e as CustomEvent).detail;
  if (!d) return;
  if (typeof d.id !== 'number') {
    // No id: a push. Delivered to whoever asked for that op.
    if (typeof d.op === 'string') {
      for (const fn of pushHandlers.get(d.op) ?? []) {
        try { fn(d); } catch (err) { console.error('push handler failed', err); }
      }
    }
    return;
  }
  const p = pending.get(d.id);
  if (!p) return;
  pending.delete(d.id);
  clearTimeout(p.timer);
  if (d.ok === false) p.reject({ code: d.code ?? 'Error', message: d.message });
  else p.resolve(d);
});

/// Send one operation; resolves with the correlated answer. Rejects
/// immediately with code 'Offline' while the socket is down (no
/// queueing — the caller shows its offline state), and with 'Timeout'
/// when the backend never answers (a lost reply is indistinguishable
/// from a dead backend; the id correlation makes it harmless).
export function sendOp(
  op: string,
  fields: Record<string, unknown> = {},
  timeoutMs = 10000,
): Promise<any> {
  return new Promise((resolve, reject) => {
    const send = window.fcviewerControlSend;
    if (!send) return reject({ code: 'Offline', message: 'viewer not ready' });
    const id = nextId++;
    const timer = window.setTimeout(() => {
      pending.delete(id);
      reject({ code: 'Timeout' });
    }, timeoutMs);
    pending.set(id, { resolve, reject, timer });
    if (!send(JSON.stringify({ id, op, ...fields }))) {
      pending.delete(id);
      clearTimeout(timer);
      reject({ code: 'Offline', message: 'scene socket down' });
    }
  });
}

/// One selected item as the viewer reports it (fc:selection detail).
export interface SelectionItem {
  objectKey: string;
  sub: string;
  doc?: string;
  obj?: string;
  label?: string;
  type?: string;
  /// False when the object lives outside the scene's home document
  /// (an external link) — only then is the path doc-qualified.
  home?: boolean;
}

/// What a card is inspecting. 'object' is a picked document object;
/// 'viewdoc' is the two containers nothing in the scene stands for —
/// the 3D view and the document — read together, because from a
/// viewer's side they are one thing ("the settings of what I am
/// looking at") and telling them apart is the backend's business, not
/// the user's. It is a client-side composite: the wire still speaks
/// the two subjects, and each descriptor carries the scope its edits
/// go back to.
export type Subject = 'object' | 'viewdoc';

/// The subjects the wire knows (docs/ThinClient.md §4.2).
export type WireSubject = 'object' | 'view3d' | 'document';

/// Which container a descriptor came out of — and, handed back
/// verbatim as setProperty's target, how to reach it again.
export type PropScope = 'object' | 'view' | 'view3d' | 'document';

export interface PropDescriptor {
  name: string;
  scope: PropScope;
  group: string;
  type: string;
  value: unknown;
  unit?: string;
  enums?: string[];
  constraints?: { min?: number; max?: number; step?: number };
  /// What the property holds under its name, as an expression spells it:
  /// "Width" of a sketch's Constraints, "Base.x" of a Placement,
  /// "[<<a name>>]" for a name that is no identifier
  members?: string[];
  doc?: string;
  readonly: boolean;
  hidden: boolean;
}

export interface PropertiesReply {
  doc: string;
  obj: string;
  subject?: WireSubject;
  label: string;
  type: string;
  props: PropDescriptor[];
}

export function getProperties(
  doc: string,
  obj: string,
  subject: Subject = 'object',
): Promise<PropertiesReply> {
  if (subject !== 'viewdoc')
    return sendOp('getProperties', { doc, obj, subject });
  // The composite: one card, two containers. Groups are namespaced so
  // the drop-down still navigates them separately and a row says which
  // it came from; scope rides each descriptor, so an edit goes back to
  // the right container without the card tracking which half it is in.
  return Promise.all([
    sendOp('getProperties', { doc, obj, subject: 'view3d' }),
    sendOp('getProperties', { doc, obj, subject: 'document' }),
  ]).then(([view, document_]) => {
    const tag = (r: PropertiesReply, prefix: string) =>
      (r?.props ?? []).map((p) => ({ ...p, group: `${prefix} · ${p.group}` }));
    return {
      doc: document_?.doc ?? view?.doc ?? '',
      obj: '',
      label: document_?.label ?? document_?.doc ?? 'Properties',
      type: '',
      props: [...tag(view, 'View'), ...tag(document_, 'Document')],
    } as PropertiesReply;
  });
}

/// Commit one property edit. The backend wraps it in a transaction and
/// recomputes; the updated scene arrives through the normal snapshot
/// stream (never treat a frame as the ack — this resolve is the ack).
export function setProperty(
  doc: string,
  obj: string,
  target: PropScope,
  name: string,
  value: unknown,
  /// For target 'view3d': a view of the document by persistent name
  /// ("View2"); absent = the served view (docs/OmniSearch.md sec 6).
  view?: string,
): Promise<any> {
  const fields: Record<string, unknown> = { doc, obj, target, name, value };
  if (view) fields.view = view;
  return sendOp('setProperty', fields);
}

/// The two containers nothing in the scene stands for, one at a time:
/// the document's properties, or a view's -- the served view, or a
/// named one of the document.
export function getContainerProperties(
  subject: 'document' | 'view3d',
  doc = '',
  view?: string,
): Promise<PropertiesReply> {
  const fields: Record<string, unknown> = { doc, obj: '', subject };
  if (view) fields.view = view;
  return sendOp('getProperties', fields);
}

// ---- The omni search box (docs/OmniSearch.md sec 6) -------------------

/// A catalog answer: the rows to add or replace and the keys to drop
/// since the version the request named, or the whole list (`full`).
export interface CatalogReply {
  list: string;
  session: string;
  version: number;
  full: boolean;
  add: any[];
  remove: string[];
}

export function omniCatalog(list: string, session: string, version: number):
    Promise<CatalogReply> {
  return sendOp('omni.catalog', { list, session, version });
}

/// The volatile detail of the rows on screen: a parameter's value and
/// whether it is stored, a command's active state.
export function omniRows(list: string, keys: string[]):
    Promise<{ rows: Record<string, { value?: string; set?: boolean; active?: boolean }> }> {
  return sendOp('omni.rows', { list, keys });
}

export interface ObjectEntry {
  name: string;
  /// Absent when it equals the name
  label?: string;
  type: string;
  /// Sub-object names, for "Part.Box"
  children?: string[];
}

export interface ViewEntry {
  /// The persistent name, "View2"
  name: string;
  title: string;
  /// The view this connection is served from
  served?: boolean;
}

export interface ObjectsReply {
  doc: string;
  label: string;
  objects: ObjectEntry[];
  views: ViewEntry[];
}

export function omniObjects(doc = ''): Promise<ObjectsReply> {
  return sendOp('omni.objects', { doc });
}

/// What the typed text names, in the box's grammar: an object, or a
/// property with the descriptor and the addressing setProperty needs.
export interface ResolveReply {
  kind: 'object' | 'property';
  doc: string;
  /// The object the property belongs to, or the resolved sub-object
  obj: string;
  /// For an object: the top-level object and the sub-object path
  top?: string;
  sub?: string;
  label?: string;
  type?: string;
  scope?: PropScope;
  /// A named view, when scope is 'view3d' and it is not the served one
  view?: string;
  prop?: PropDescriptor;
}

export function omniResolve(query: string, doc = ''): Promise<ResolveReply> {
  return sendOp('omni.resolve', { query, doc });
}

/// Run a command, or row `child` of a group command's menu
export function runCommand(name: string, child?: number): Promise<any> {
  const fields: Record<string, unknown> = { name };
  if (typeof child === 'number') fields.child = child;
  return sendOp('command.run', fields, 60000);
}

export interface CommandChild {
  index: number;
  text?: string;
  /// The command a click on the row runs; absent for an unnamed row
  command?: string;
  tooltip?: string;
  checkable?: boolean;
  checked?: boolean;
  enabled?: boolean;
  visible?: boolean;
  separator?: boolean;
}

export function commandChildren(name: string):
    Promise<{ exclusive: boolean; items: CommandChild[] }> {
  return sendOp('command.children', { name });
}

export interface ParamState { key: string; value: string; set: boolean }

export function paramGet(key: string): Promise<ParamState> {
  return sendOp('param.get', { key });
}

/// `value` in the registry's text form: true/false, a decimal, 0x-hex
/// for a Hex, or the string
export function paramSet(key: string, value: string): Promise<ParamState> {
  return sendOp('param.set', { key, value });
}

export function paramReset(key: string): Promise<ParamState> {
  return sendOp('param.reset', { key });
}

// ---------------------------------------------------------------------------
// The spreadsheet tier (docs/SpreadsheetRemote.md sec 3).

/// One cell as the host serializes it. Keys are short because the used
/// range travels whole: `t` is what the desktop displays, `f` what an
/// editor opens with, and they differ for anything computed.
export interface SheetCell {
  a: string;                 // address, "B4"
  r: number;
  c: number;
  t?: string;                // display text
  f?: string;                // content / formula
  ha?: 'left' | 'center' | 'right';
  va?: 'top' | 'middle' | 'bottom';
  fg?: string;
  bg?: string;
  st?: string[];             // bold / italic / underline
  sr?: number;               // span rows
  sc?: number;               // span columns
  alias?: string;
  err?: string;
  /// The MACHINE value, in the sandbox wire encoding (a number, a string,
  /// or {t:'quantity',v,u}). Present only for the by-value set; it is what
  /// makes a local preview possible, and its absence means no preview.
  wv?: any;
}

export interface SheetData {
  doc: string;
  obj: string;
  label: string;
  version: number;
  rows: number;
  cols: number;
  cells: SheetCell[];
  colW: Record<string, number>;
  rowH: Record<string, number>;
}

export interface SheetEntry { obj: string; label: string }

export function sheetList(doc?: string): Promise<{ doc: string; sheets: SheetEntry[] }> {
  return sendOp('sheet.list', doc ? { doc } : {});
}

export function sheetGet(obj: string, doc?: string): Promise<SheetData> {
  return sendOp('sheet.get', doc ? { doc, obj } : { obj });
}

/// Commit one cell. The reply is the ack; the refreshed values arrive
/// through the sheet.changed push that follows, exactly as setProperty's
/// scene update does — never treat the ack as the new state.
export function sheetSet(
  obj: string,
  cell: string,
  content: string,
  doc?: string,
): Promise<{ version: number }> {
  return sendOp('sheet.set', doc ? { doc, obj, cell, content } : { obj, cell, content });
}

/// Completions for a cell's content (docs/Sandbox.md 7.25): the same
/// shape as PanelClient.complete, answered by a completer the host builds
/// for the request with the sheet as owner.  Non-mutating: a view-only
/// client may ask.
export function sheetComplete(
  obj: string,
  text: string,
  pos: number,
  doc?: string,
): Promise<{ items: string[]; details: string[]; start: number; end: number }> {
  return sendOp('sheet.complete', doc ? { doc, obj, text, pos } : { obj, text, pos });
}

/// The backend's "this sheet moved on" cue: identity and version only,
/// so a viewer showing another sheet ignores it without a round trip.
export function onSheetChanged(
  fn: (msg: { doc: string; obj: string; version: number }) => void,
): () => void {
  return onPush('sheet.changed', fn);
}

// ---- The transaction log (docs/TransactionLog.md sec 32.21) ----

/// One row of the log as the host lists it (Gui/SceneLog.cpp).
export interface LogRow {
  seq: number;
  parent: number;
  kind: string;              // user, implicit, recompute, undo, redo, merge, ...
  origin: string;
  name: string;
  time: number;              // seconds since the epoch
  author: string;
  authorKind: string;        // local, verified, invited, declared, enrolled
  branch: number;
  branchName: string;
  inverts?: number;          // the row an undo or a redo is the inverse of
  merged?: number;           // a merge's second parent
  ops: number;               // 0: a record, which changed nothing
  here: boolean;             // of the history the document is at the head of
  mine: boolean;             // made by the one who asks
  within: boolean;           // a document may be moved from it by the one who asks
}

/// What the one who asks may (docs/TransactionLog.md sec 32.25): the host
/// all of it; anyone else by branch, and `write` says the branch the
/// document is on is one it writes -- which is what lets it move the
/// document: restore, switch, branch.
export interface LogReach {
  host: boolean;
  write: boolean;
}

export interface LogRowsReply extends LogReach {
  doc: string;
  floor?: number;            // the row the branch the document is on is written from
  oldest?: number;           // the oldest row the page was read to: rows of branches
                             // that are not read are left out of it
  rows: LogRow[];            // newest first
  more: boolean;             // older ones than these exist
  reset?: boolean;           // more came than a page: start over from these
  last: number;
  head: number;
  branch: number;
  branchName: string;
}

/// One op of a row.
export interface LogOp {
  idx: number;
  op: string;                // create, remove, set, addprop, delprop
  kind: string;              // doc, obj, view
  prop: string;
  type: string;
  before?: string;           // the refs of the value before and after
  after?: string;
  derived?: boolean;
  followed?: boolean;
  cid?: number;
  obj?: string;
  label?: string;
  here?: boolean;            // the object is in the document now
  shows?: boolean;           // its view can show the two states
  keptValue?: boolean;       // the value after is kept in the file by itself
  keptObject?: boolean;      // every value of the object is kept in the file
}

export interface LogOpsReply {
  doc: string;
  row: number;
  name: string;
  script?: string;
  ops: LogOp[];
  count: number;             // how many the row has; `ops` may be fewer
  keep?: boolean;            // the asker may keep a value at the row
  keepWhy?: string;          // and why not
  keepObject?: boolean;      // the asker may keep every value of an object: the host
}

export interface LogValueReply {
  ref: string;
  text: string;
  bytes: number;
  truncated: boolean;
  attachments: Array<{ name: string; bytes: number }>;
  /// What a value whose text says nothing to a reader is, in a line -- a
  /// shape's kind, its counts, volume and size -- and the same as numbers
  /// (sec 32.35); where the host knows it
  summary?: string;
  facts?: Record<string, number>;
}

const withDoc = (doc: string | undefined, fields: Record<string, unknown>) =>
  (doc ? { doc, ...fields } : fields);

/// A page of rows, newest first: the newest, those before `before`, or
/// what came after `after`. Asking is also what subscribes this connection
/// to `log.changed` for the document. For those who may edit: a viewer is
/// answered ViewOnly.
export function logRows(
  doc?: string,
  range: { before?: number; after?: number; limit?: number } = {},
): Promise<LogRowsReply> {
  return sendOp('log.rows', withDoc(doc, range));
}

export function logOps(row: number, doc?: string): Promise<LogOpsReply> {
  return sendOp('log.ops', withDoc(doc, { row }));
}

export function logValue(ref: string, doc?: string): Promise<LogValueReply> {
  return sendOp('log.value', withDoc(doc, { ref }));
}

/// Where the document is in its log: what every act on it answers with.
export interface LogPlace {
  doc: string;
  last: number;
  head: number;
  branch: number;
  branchName: string;
}

export interface LogVersion {
  num: number;
  named: boolean;            // a named one is never evicted
  name: string;
  branch: number;
  branchName: string;
  branchRead?: boolean;      // false: of a branch the one who asks does not read
  seq: number;               // the row it was taken at; 0: at none
  time: number;
  here: boolean;             // of the history the document is at the head of
  head: boolean;             // the document as it stands
  within: boolean;           // a document may be moved to it by the one who asks
}

/// A branch of the log. One the asker does not read is listed by its name
/// alone, `read` false, to be shown greyed (docs/TransactionLog.md sec 32.30).
export interface LogBranch {
  id: number;
  name: string;
  read: boolean;
  head: number;
  fromVersion: number;
  fromSeq: number;
  time: number;
  closed: boolean;           // not switched to
  current: boolean;          // the one the document is on
  write: boolean;            // the one who asks writes it
  floor?: number;            // and from which row
  made: boolean;             // made by the asker's own doing: its to delete
}

/// A request to merge one branch into another.
export interface LogRequest {
  id: number;
  source: string;            // the branch to merge
  target: string;            // the branch to merge it into
  by: string;
  byKind: string;
  note: string;
  time: number;
  state: string;             // open, merged, rejected, withdrawn
  closed?: number;
  closedBy?: string;
  result?: number;           // the row the merge wrote
  mine: boolean;             // asked by the one who asks: its to withdraw
  handles: boolean;          // the asker writes the target: its to merge or reject
  here: boolean;             // the document is on the target, where it is merged
}

/// Rows taken back as one step though none is the last: all of them or
/// none, and a refusal (`Refused`) says which is in the way. Below the host
/// a connection takes back its own rows (`NotYours`).
export function logUndoRows(rows: number[], doc?: string): Promise<LogPlace> {
  return sendOp('log.undo', withDoc(doc, { rows }));
}

export function logVersions(
  doc?: string,
): Promise<LogPlace & LogReach & { versions: LogVersion[] }> {
  return sendOp('log.versions', withDoc(doc, {}));
}

export function logBranches(
  doc?: string,
): Promise<LogPlace & LogReach & { branches: LogBranch[] }> {
  return sendOp('log.branches', withDoc(doc, {}));
}

/// A version of the document as it stands, named where a name is given.
export function logSnapshot(name?: string, doc?: string): Promise<LogPlace & { version: number }> {
  return sendOp('log.snapshot', withDoc(doc, name ? { name } : {}));
}

/// Name a version; with no name, make it unnamed again.
export function logNameVersion(version: number, name: string, doc?: string): Promise<LogPlace> {
  return sendOp('log.nameVersion', withDoc(doc, { version, name }));
}

/// The document made what a version was, as one step that undo takes back.
export function logRestore(version: number, doc?: string): Promise<LogPlace> {
  return sendOp('log.restore', withDoc(doc, { version }));
}

/// A new branch, switched to: from a version, or a row, or where the
/// document is.
export function logNewBranch(
  name: string, from: { version?: number; row?: number } = {}, doc?: string,
): Promise<LogPlace & { made: number }> {
  return sendOp('log.branch', withDoc(doc, { name, ...from }));
}

export function logSwitchBranch(name: string, doc?: string): Promise<LogPlace> {
  return sendOp('log.switch', withDoc(doc, { name }));
}

export function logRenameBranch(name: string, to: string, doc?: string): Promise<LogPlace> {
  return sendOp('log.renameBranch', withDoc(doc, { name, to }));
}

export function logDeleteBranch(name: string, doc?: string): Promise<LogPlace & { rows: number }> {
  return sendOp('log.deleteBranch', withDoc(doc, { name }));
}

/// Rows of a branch that is read, applied to the one the document is on as
/// a step of the asker's own. Where they meet what was set here since, the
/// host answers `Conflicts` with how many, and `fallback` says a side for
/// them all.
export function logApply(
  rows: number[], sides?: LogSides, doc?: string,
): Promise<LogPlace & { row: number; failed: number }> {
  return sendOp('log.apply', withDoc(doc, { rows, ...sidesOf(sides) }));
}

/// A side for each conflict, by its key (LogChange.key), and one for the
/// rest: what an apply or a merge that meets conflicts goes on with
/// (docs/TransactionLog.md sec 32.26)
export interface LogSides {
  fallback?: 'ours' | 'theirs';
  picks?: Record<string, 'ours' | 'theirs'>;
}

function sidesOf(sides?: LogSides): Record<string, unknown> {
  const out: Record<string, unknown> = {};
  if (sides?.fallback) out.fallback = sides.fallback;
  if (sides?.picks && Object.keys(sides.picks).length) out.picks = sides.picks;
  return out;
}

/// One change a merge or an apply would make (Document::MergeChange):
/// `conflict` asks a side, `view` keeps ours unless picked; the values are
/// text, at the base, ours and theirs.
export interface LogChange {
  key: string;               // what a pick names it by
  kind: string;              // take, same, conflict, view, derived, unit, merge, name
  op: string;                // set, addprop, delprop, create, remove, revive, unit
  ckind: string;             // doc, obj, view
  object: string;
  label?: string;
  prop: string;
  derived: boolean;
  base?: string;
  ours?: string;
  theirs?: string;
  note?: string;
}

/// A file a client sent to be merged, not read yet (sec 30.20): the host's
/// all are listed to it, a client's own to the client.
export interface LogSentFile {
  id: number;
  name: string;
  size: number;
  by: string;
  byKind: string;
  time: number;
  mine: boolean;             // sent by the one who asks: its to withdraw
}

/// A branch an import made that the branch the document is on has not
/// taken (sec 30.20 H1).
export interface LogImport {
  branch: string;
  file: string;              // the file it came from
  authors: string[];         // who made its rows
  by?: string;               // who sent the file, when someone did
  time: number;
  rows: number;              // the operations it has to give
  independent: boolean;      // it shares no history with this file
  stopped: boolean;          // the import stopped at a row
}

export interface LogPreviewReply extends LogPlace {
  what: 'rows' | 'merge';
  request?: number;
  source?: string;           // the branch a request merges
  base: number;              // the newest row both histories hold
  forward: number;           // rows taken as they are, where nothing here moved
  fastForward: boolean;
  conflicts: number;
  changes: LogChange[];
  count: number;             // how many there are; `changes` may be fewer
}

/// What an apply of `rows`, or the merge `request` asks, would do: nothing
/// changes. Refused as the act would be.
export function logPreview(
  of: { rows: number[] } | { request: number } | { import: string }, doc?: string,
): Promise<LogPreviewReply> {
  return sendOp('log.preview', withDoc(doc, of));
}

/// The merge undone at the head taken out of the log; with `dry`, only
/// what it would take out and whether the asker may.
export function logRollBack(
  dry: boolean, doc?: string,
): Promise<LogPlace & { merge: number; source: string; rows: number; may?: boolean;
                       why?: string }> {
  return sendOp('log.rollBack', withDoc(doc, dry ? { dry: true } : {}));
}

export function logRequests(
  doc?: string,
): Promise<LogPlace & LogReach & { requests: LogRequest[]; files?: LogSentFile[];
                                   imports?: LogImport[] }> {
  return sendOp('log.requests', withDoc(doc, {}));
}

/// A file sent, read into a branch of its own: the host's.
export function logBring(
  file: number, doc?: string,
): Promise<LogPlace & { name: string; into: string; rows: number; stopped?: boolean;
                       why?: string }> {
  return sendOp('log.bring', withDoc(doc, { file }));
}

/// A file sent, let go unread: the host's, or withdrawn by its sender.
export function logDropFile(file: number, doc?: string): Promise<LogPlace & { name: string }> {
  return sendOp('log.dropFile', withDoc(doc, { file }));
}

/// A branch an import made, merged into the one the document is on: the
/// host's. `Conflicts` and the sides as for logApply.
export function logMergeImport(
  branch: string, sides?: LogSides, doc?: string,
): Promise<LogPlace & { row: number; forwarded: number; failed: number }> {
  return sendOp('log.mergeImport', withDoc(doc, { branch, ...sidesOf(sides) }));
}

/// A branch's history before one of its versions taken out, but what
/// another branch holds and the named versions: the host's, for good.
export function logTrim(
  branch: string, version: number, doc?: string,
): Promise<LogPlace & { rows: number }> {
  return sendOp('log.trim', withDoc(doc, { branch, version }));
}

/// A version an older one may be squashed to it from, newest first, with
/// the rows it would fold.
export interface LogBehind {
  num: number;
  name: string;
  seq: number;
  rows: number;
}

/// The rows between version `from` and `version` folded into one row of
/// their net change: the host's, for good. With no `from`, the versions it
/// may start from.
export function logSquash(
  version: number, from?: number, doc?: string,
): Promise<LogPlace & { version: number; behind?: LogBehind[]; rows?: number; row?: number }> {
  return sendOp('log.squash', withDoc(doc, from ? { version, from } : { version }));
}

/// Ask for `source` to be merged into `target`: for one who reads both.
export function logRequest(
  source: string, target: string, note: string, doc?: string,
): Promise<LogPlace & { request: number }> {
  return sendOp('log.request', withDoc(doc, { source, target, note }));
}

/// End a request without merging: `withdrawn` by who asked, `rejected` by
/// who writes its target.
export function logCloseRequest(
  request: number, state: 'withdrawn' | 'rejected', doc?: string,
): Promise<LogPlace> {
  return sendOp('log.closeRequest', withDoc(doc, { request, state }));
}

/// Do what a request asks, in a document that is on its target. `Conflicts`
/// and the sides as for logApply.
export function logMergeRequest(
  request: number, sides?: LogSides, doc?: string,
): Promise<LogPlace & { row: number; forwarded: number; failed: number }> {
  return sendOp('log.mergeRequest', withDoc(doc, { request, ...sidesOf(sides) }));
}

// What a save writes into the file though no kept version reaches it: the
// values a recompute wrote -- shapes -- kept (docs/TransactionLog.md sec
// 32.11, 32.27).

/// A value kept by itself: it stays when its row goes.
export interface LogKeptValue {
  ref: string;
  cid: number;
  obj?: string;              // where the document has the object
  label?: string;
  here: boolean;             // the object is in the document
  prop: string;
  seq: number;               // the row it was kept at
  row: boolean;              // that row is still in the log
  release: boolean;          // the asker may let it go
  why?: string;              // and why not
  keeper?: string;           // who kept it, where not the host
  keeperKind?: string;
  mine?: boolean;            // the asker kept it
}

/// An object every value of which is kept, and those the log has of it.
export interface LogKeptObject {
  cid: number;
  obj?: string;
  label?: string;
  here: boolean;
  /// `release`, `why` and the keeper of one kept by itself too, as for
  /// LogKeptValue; `keep`: whether the asker may keep it by itself
  values: Array<{ ref: string; prop: string; seq: number; alone: boolean; release?: boolean;
                  why?: string; keeper?: string; mine?: boolean; keep?: boolean;
                  keepWhy?: string }>;
  release: boolean;          // the asker may stop keeping it
  why?: string;
}

export function logKept(
  doc?: string,
): Promise<LogPlace & LogReach & { values: LogKeptValue[]; objects: LogKeptObject[];
                                   count: number; keepObject?: boolean }> {
  return sendOp('log.kept', withDoc(doc, {}));
}

/// Keep, or let go of, the value of an object at a row (0: where the
/// document is); with no `prop` every value of it a recompute wrote. One
/// object by name or id, or several by name. `values` is how many moved.
/// Keeping is the host's, and of one who writes the row's branch since the
/// point; letting go is also the keeper's.
export function logKeep(
  what: { obj?: string; cid?: number; objects?: string[]; prop?: string; row?: number },
  keep: boolean, doc?: string,
): Promise<LogPlace & { row: number; values: number }> {
  return sendOp('log.keep', withDoc(doc, { ...what, keep }));
}

/// Keep every value of an object the log has and will have, or stop: the
/// host's alone, both.
export function logKeepObject(
  what: { obj?: string; cid?: number }, keep: boolean, doc?: string,
): Promise<LogPlace & { cid: number; changed: boolean }> {
  return sendOp('log.keepObject', withDoc(doc, { ...what, keep }));
}

/// Let go of a value kept by itself, by its ref: one whose row is gone too.
export function logRelease(ref: string, doc?: string): Promise<LogPlace & { ref: string }> {
  return sendOp('log.release', withDoc(doc, { ref }));
}

/// The host's "this document's log has other rows now": the document's
/// name and nothing of the log.
export function onLogChanged(fn: (msg: { doc: string }) => void): () => void {
  return onPush('log.changed', fn);
}

/// Gui::ViewProvider::ValueDiff: the edit that shows a change of an
/// object's value as its two states.
export const EDIT_VALUE_DIFF = 6;

/// Show what row `row` made of object `obj`, as the edit it is: the
/// host's panel of it is mirrored, and comes up by itself (main.tsx).
///
/// An edit runs in the view this connection stated, and a page nobody has
/// clicked in has stated none (docs/ThinClient.md sec 8.10b): the camera
/// goes up first, as the viewer's own ask for an edit sends it.
export function showRowChange(obj: string, row: number, doc?: string): Promise<any> {
  window.fcviewerStateCamera?.();
  return sendOp('edit', withDoc(doc, { obj, mode: EDIT_VALUE_DIFF, row }));
}

/// The same edit, of a value the log keeps in the file (its ref) against
/// what the object has now (docs/TransactionLog.md sec 32.27).
export function showKeptValue(obj: string, kept: string, doc?: string): Promise<any> {
  window.fcviewerStateCamera?.();
  return sendOp('edit', withDoc(doc, { obj, mode: EDIT_VALUE_DIFF, kept }));
}

// ---- The streamed desktop tool bars (docs/ThinClient.md 8.11 item 4) ----

/// Listen for the scene socket going up or down. Returns the unsubscribe.
export function onConnection(fn: (up: boolean) => void): () => void {
  const listener = (e: Event) => fn(!!(e as CustomEvent).detail);
  window.addEventListener('fc:connection', listener);
  return () => window.removeEventListener('fc:connection', listener);
}

/// Force a camera frame up, for an op that runs in this client's view.
export function stateCamera(): boolean {
  return !!window.fcviewerStateCamera?.();
}

/// Run a tool in this client's view: the `command` op, which the server
/// allowlists (docs/ThinClient.md 8.7). `index` is a group member, 1-based
/// as the widget models count it; the group's default moves with it.
export function runTool(name: string, index?: number): Promise<any> {
  const fields: Record<string, unknown> = { name };
  if (typeof index === 'number' && index > 0) fields.index = index;
  return sendOp('command', fields, 60000);
}

/// The constraint commands that open no dialog on the serving machine. A
/// dimensional one asks for its value in an on-view box at the label.
const DIALOG_FREE_CONSTRAINTS = new Set([
  'Sketcher_ConstrainHorVer',
  'Sketcher_ConstrainHorizontal',
  'Sketcher_ConstrainVertical',
  'Sketcher_ConstrainLock',
  'Sketcher_ConstrainBlock',
  'Sketcher_ConstrainCoincident',
  'Sketcher_ConstrainCoincidentUnified',
  'Sketcher_ConstrainPointOnObject',
  'Sketcher_ConstrainParallel',
  'Sketcher_ConstrainPerpendicular',
  'Sketcher_ConstrainTangent',
  'Sketcher_ConstrainEqual',
  'Sketcher_ConstrainSymmetric',
  'Sketcher_ConstrainGroup',
  'Sketcher_ToggleDrivingConstraint',
  'Sketcher_ToggleActiveConstraint',
  'Sketcher_Dimension',
  'Sketcher_ConstrainDistance',
  'Sketcher_ConstrainDistanceX',
  'Sketcher_ConstrainDistanceY',
  'Sketcher_ConstrainRadius',
  'Sketcher_ConstrainDiameter',
  'Sketcher_ConstrainRadiam',
  'Sketcher_ConstrainAngle',
  'Sketcher_ChangeDimensionConstraint',
  'Sketcher_ConstrainSnellsLaw',
]);

/// The `command` op's allowlist as the server has it (SceneControl.cpp
/// isBrowserSafeCommand), for drawing a button disabled rather than
/// letting it be refused. The server's copy is the one that decides.
export function isBrowserSafeCommand(name: string): boolean {
  return name.startsWith('Sketcher_Create')
    || name === 'Sketcher_External'
    || name === 'Sketcher_Defining'
    || name === 'Sketcher_Intersection'
    || name === 'Sketcher_IntersectionDefining'
    || name === 'Sketcher_CarbonCopy'
    || DIALOG_FREE_CONSTRAINTS.has(name);
}

/// Bytes as base64, in chunks: `String.fromCharCode(...bytes)` over a whole
/// file is one argument per byte, and a few hundred kilobytes overflows the
/// call.
function base64Of(bytes: ArrayBuffer): string {
  const view = new Uint8Array(bytes);
  const CHUNK = 0x8000;
  let text = '';
  for (let at = 0; at < view.length; at += CHUNK) {
    text += String.fromCharCode(...view.subarray(at, at + CHUNK));
  }
  return btoa(text);
}

/// What the host says of a file it took as a request.
export interface SentRequest {
  request: number;
  name: string;
  size: number;
}

/// Send a copy of the document to whoever is sharing it, to be merged
/// (docs/TransactionLog.md sec 30.23). The host keeps the file and reads
/// nothing of it until its owner asks; the answer says only that it was
/// taken. Needs a connection that may edit: a request becomes rows of the
/// host's log. Rejects with `TooLarge` past the host's upload limit, and
/// with `TooMany` when what already waits there leaves no room (sec 30.29).
export function sendRequestFile(name: string, bytes: ArrayBuffer): Promise<SentRequest> {
  return sendOp('requests.send', { name, data: base64Of(bytes) }, 120000);
}
