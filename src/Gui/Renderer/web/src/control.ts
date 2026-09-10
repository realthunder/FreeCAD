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
): Promise<any> {
  return sendOp('setProperty', { doc, obj, target, name, value });
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
