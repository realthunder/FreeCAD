// The widget stream's client half (docs/Sandbox.md 7.22, G7, W1): subscribe
// over the scene socket's control lane, feed what arrives into the store,
// and carry a write back.
//
// Framework-free on purpose. The reduction in protocol.ts is pure and this
// layer only adds the socket, so the Solid signals live in the view above
// it: that keeps both halves typecheckable without a DOM and leaves the
// client reusable by anything else that wants the desktop's widgets.
//
// The transport is the one the chrome already has (control.ts): `sendOp`
// correlates a request with its answer, `onPush` delivers the frames that
// belong to nobody's request. A `widgets` push IS the stream, so no second
// socket, no new port, nothing in main.cpp to change.
//
// Two things about the order, both from 7.22:
//
//   - the snapshot does NOT ride the subscribe reply -- it follows on the
//     host's zero timer -- so the push handler is registered BEFORE the
//     subscribe goes out, or the first opens land on the floor;
//   - the host answers a client's own write only when it CORRECTED it (the
//     origin echo). That arrives as an ordinary `widgets` update aimed at
//     this connection, so there is nothing to handle specially here: the
//     store applies it like any other, and the field shows what the
//     document really holds.

import { onPush, sendOp } from '../control.ts';
import { WidgetStore } from './protocol.ts';
import type { Frame } from './protocol.ts';

/// What the subscribe reply tells a client before any frame arrives.
export interface BootState {
  /// The task panel up right now, or null -- which is how a client tells
  /// "no panel" from "not told yet".
  panel: string | null;
  /// Top-level dialogs already up, in show order.
  dialogs: string[];
  /// The host's icon override ('' for the stock theme).
  theme: string;
  /// The host's QLocale name, for numbers and dates.
  locale: string;
}

const EMPTY_BOOT: BootState = { panel: null, dialogs: [], theme: '', locale: 'C' };

export class PanelClient {
  readonly store = new WidgetStore();
  boot: BootState = EMPTY_BOOT;
  /// Raised whenever a frame changed anything, so a view can re-read.
  private notify: () => void;
  private stopPush: (() => void) | null = null;
  private images = new Map<string, string>();
  private icons = new Map<string, string>();

  constructor(notify: () => void = () => {}) {
    this.notify = notify;
  }

  /// The panel root the mirror has up, if any. A root is `panel:<n>`; the
  /// mirror's list container (`panel`) is not one, which is why this asks
  /// for the colon.
  get panelId(): string | null {
    for (const id of this.store.ids()) {
      if (id.startsWith('panel:')) return id;
    }
    return null;
  }

  /// Top-level dialogs, M3's `dialog:<n>` roots, in the order they opened.
  get dialogIds(): string[] {
    return this.store.ids().filter((id) => id.startsWith('dialog:')).sort();
  }

  /// Listen first, then ask: the host pushes the snapshot after the reply.
  async subscribe(): Promise<BootState> {
    if (!this.stopPush) {
      this.stopPush = onPush('widgets', (msg: Frame) => {
        if (this.store.apply(msg)) this.notify();
      });
    }
    const reply = await sendOp('widgets.subscribe', { panels: true });
    this.boot = {
      panel: (reply?.panel as string) ?? null,
      dialogs: (reply?.dialogs as string[]) ?? [],
      theme: (reply?.theme as string) ?? '',
      locale: (reply?.locale as string) ?? 'C',
    };
    this.notify();
    return this.boot;
  }

  /// Leave, and stop listening. The host stops the mirror with its last
  /// subscriber, so this is what keeps a closed panel from costing the
  /// desktop anything.
  async dispose(): Promise<void> {
    this.stopPush?.();
    this.stopPush = null;
    try {
      await sendOp('widgets.unsubscribe', {});
    }
    catch {
      // the socket is already gone: nothing to leave
    }
  }

  /// Write bag values to a mirrored widget. The keys are the bag's own
  /// names; the `q_` the wire wants is added here, so no caller carries the
  /// prefix around.
  async write(id: string, values: Record<string, unknown>): Promise<void> {
    const state: Record<string, unknown> = {};
    for (const [key, value] of Object.entries(values)) state[`q_${key}`] = value;
    await sendOp('widgets.update', { target: id, state });
  }

  /// A request to the widget rather than a value: the panel's accept and
  /// reject, a button's click, a focus.
  async custom(id: string, content: Record<string, unknown>): Promise<void> {
    await sendOp('widgets.custom', { target: id, content });
  }

  /// An `img:<sha1>` the bag carries, as a data URL. Content-addressed, so
  /// a page-lifetime cache can never go stale and one fetch is enough.
  async image(name: string): Promise<string> {
    const had = this.images.get(name);
    if (had !== undefined) return had;
    const reply = await sendOp('widgets.image', { name });
    const url = `data:image/png;base64,${String(reply?.data ?? '')}`;
    this.images.set(name, url);
    return url;
  }

  /// A theme icon by name. SVG comes back as text, anything else base64.
  async icon(name: string, size = 24): Promise<string> {
    const key = `${name}@${size}`;
    const had = this.icons.get(key);
    if (had !== undefined) return had;
    const reply = await sendOp('widgets.icon', { name, size });
    const data = String(reply?.data ?? '');
    const url = reply?.format === 'svg'
      ? `data:image/svg+xml;charset=utf-8,${encodeURIComponent(data)}`
      : `data:image/png;base64,${data}`;
    this.icons.set(key, url);
    return url;
  }
}
