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

/// The backend's "this sheet moved on" cue: identity and version only,
/// so a viewer showing another sheet ignores it without a round trip.
export function onSheetChanged(
  fn: (msg: { doc: string; obj: string; version: number }) => void,
): () => void {
  return onPush('sheet.changed', fn);
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

/// The `command` op's allowlist as the server has it (SceneControl.cpp
/// isBrowserSafeCommand), for drawing a button disabled rather than
/// letting it be refused. The server's copy is the one that decides.
export function isBrowserSafeCommand(name: string): boolean {
  return name.startsWith('Sketcher_Create')
    || name === 'Sketcher_External'
    || name === 'Sketcher_CarbonCopy';
}
