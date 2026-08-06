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
    /// Switch to another served document (main.cpp fcviewer_switch_doc,
    /// docs/MultiDocServe.md §6).
    fcviewerSwitchDoc?: (name: string) => void;
    /// Name this connection for the host's sharing roster (main.cpp
    /// fcviewer_set_client, docs/MultiDocServe.md §6); persisted in
    /// localStorage by the viewer side.
    fcviewerSetClient?: (name: string) => void;
    fcviewerClientName?: () => string;
    /// Whether the host has made this connection view-only
    /// (docs/MultiDocServe.md §8). Mirrored here by the viewer so a
    /// panel mounting after the push still reads the current mode;
    /// changes arrive as 'fc:viewonly' events.
    fcviewerViewOnly?: boolean;
    /// Set by this layer to claim the HUD feed: the viewer then reports
    /// it as 'fc:hud' events instead of drawing its own overlay box.
    fcviewerHudCard?: boolean;
  }
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

window.addEventListener('fc:control', (e: Event) => {
  const d = (e as CustomEvent).detail;
  if (!d || typeof d.id !== 'number') return;
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
