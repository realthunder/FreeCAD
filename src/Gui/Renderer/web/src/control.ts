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
/// the other two are the containers nothing in the scene stands for,
/// so they are reachable only from the launcher, never from a pick.
export type Subject = 'object' | 'view3d' | 'document';

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
  subject?: Subject;
  label: string;
  type: string;
  props: PropDescriptor[];
}

export function getProperties(
  doc: string,
  obj: string,
  subject: Subject = 'object',
): Promise<PropertiesReply> {
  return sendOp('getProperties', { doc, obj, subject });
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
