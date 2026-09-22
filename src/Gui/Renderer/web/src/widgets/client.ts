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
import type { CompletionSet, ExpressionPreview } from './complete.ts';
import { IconCache, iconKey, iconKind, imageKey } from './images.ts';
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

/// Bytes as base64, in chunks.
///
/// `String.fromCharCode(...bytes)` over a whole file throws: the spread
/// becomes one argument per byte, and a few hundred kilobytes overflows
/// the call. 32 KB at a time is well inside every engine's limit.
function base64Of(bytes: ArrayBuffer): string {
  const view = new Uint8Array(bytes);
  const CHUNK = 0x8000;
  let text = '';
  for (let at = 0; at < view.length; at += CHUNK) {
    text += String.fromCharCode(...view.subarray(at, at + CHUNK));
  }
  return btoa(text);
}

export class PanelClient {
  readonly store = new WidgetStore();
  boot: BootState = EMPTY_BOOT;
  /// Raised whenever a frame changed anything, so a view can re-read.
  private notify: () => void;
  private stopPush: (() => void) | null = null;
  /// Both kinds of picture, in one page-lifetime cache (W3): an `img:` id
  /// is content-addressed and a named icon is keyed by the host's theme, so
  /// neither entry can go stale while the page is up.
  readonly pictures = new IconCache();
  private pendingWake = 0;

  constructor(notify: () => void = () => {}) {
    this.notify = notify;
  }

  /// Wake the view ONCE per animation frame, not once per op.
  ///
  /// There is no batching on the host side: `Fw::Store` calls its sink per
  /// item op, so a Sketcher solve that refills its list arrives as 162
  /// separate pushes (8.4), each its own `fc:control` event. Waking the
  /// view on each is 162 reflows for one solve, which is the thing W2 is
  /// not allowed to do. Coalescing here rather than in the card fixes it
  /// for every consumer of the stream at once.
  ///
  /// rAF and not a microtask: the pushes land in separate tasks, so a
  /// microtask drains between them and coalesces nothing. A frame is also
  /// the right unit -- there is no point repainting faster than the page
  /// draws. Where there is no rAF (a test, node) a timeout stands in.
  private wake(): void {
    if (this.pendingWake) return;
    const fire = (): void => {
      this.pendingWake = 0;
      this.notify();
    };
    this.pendingWake = typeof requestAnimationFrame === 'function'
      ? requestAnimationFrame(fire)
      : (setTimeout(fire, 16) as unknown as number);
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
        if (this.store.apply(msg)) this.wake();
      });
    }
    const reply = await sendOp('widgets.subscribe', { panels: true });
    const theme = (reply?.theme as string) ?? '';
    // A named icon's bytes are the theme's. The theme is part of every
    // icon key, so a change cannot serve the old bytes -- but the old
    // entries are dead weight, and a RE-subscribe is exactly where a
    // desktop theme change is noticed (there is no theme event on this
    // wire yet; 7.19's open list says so).
    if (theme !== this.boot.theme) this.pictures.clear();
    this.boot = {
      panel: (reply?.panel as string) ?? null,
      dialogs: (reply?.dialogs as string[]) ?? [],
      theme,
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
    if (this.pendingWake && typeof cancelAnimationFrame === 'function') {
      cancelAnimationFrame(this.pendingWake);
    }
    this.pendingWake = 0;
    try {
      await sendOp('widgets.unsubscribe', { panels: true });
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

  /// Completions for what is typed in a mirrored field (docs/Sandbox.md
  /// 7.23). The host answers from a completer it builds for the request:
  /// the one a desktop widget is using is never touched, and nothing pops
  /// up on the desktop user's screen. A field with nothing to complete
  /// against answers an empty list rather than an error.
  async complete(id: string, text: string, pos: number): Promise<CompletionSet> {
    const reply = await sendOp('widgets.complete', { target: id, text, pos });
    return {
      text,
      pos,
      items: (reply?.items as string[]) ?? [],
      details: (reply?.details as string[]) ?? [],
      start: (reply?.start as number) ?? 0,
      end: (reply?.end as number) ?? 0,
    };
  }

  /// Set -- or clear, with '' -- the expression on a bound field. Its own
  /// op rather than a property write because a parse error has to come
  /// back: it rejects with the lane's error, code 'BadExpression' and the
  /// reason as its message, and the document is left alone.
  async setExpression(id: string, text: string): Promise<void> {
    await sendOp('widgets.expression', { target: id, text });
  }

  /// What the expression WOULD evaluate to, without setting anything --
  /// the live result line the desktop's expression dialog shows while you
  /// type (docs/Sandbox.md 7.23). Never throws for a bad expression: an
  /// expression is wrong for most of the time it is being typed, so the
  /// reason comes back as a severity and a message to show, not as a
  /// rejection to handle.
  async previewExpression(id: string, text: string): Promise<ExpressionPreview> {
    const reply = await sendOp('widgets.expression', { target: id, text, preview: true });
    return {
      result: (reply?.result as string) ?? '',
      severity: (reply?.severity as ExpressionPreview['severity']) ?? 'ok',
      message: (reply?.message as string) ?? '',
    };
  }

  /// A file the viewer's OWN picker chose, sent to the host (W5), which
  /// answers with the path it wrote -- a path in the host's own upload
  /// directory, never one this client named.
  ///
  /// The bytes ride the control lane as base64 rather than a channel of
  /// their own: `sendOp` already carries this connection's token, its
  /// access level and the request correlation, and a chooser's file is a
  /// font or a hatch pattern. The host caps what it will write.
  async upload(name: string, bytes: ArrayBuffer): Promise<string> {
    const reply = await sendOp('widgets.upload', { name, data: base64Of(bytes) });
    return (reply?.path as string) ?? '';
  }

  /// An `img:<sha1>` the bag carries, as a data URL: a picture leaf's grab,
  /// a button's icon, an item cell's decoration. Content-addressed, so one
  /// fetch is enough and a changed picture arrives as a different id.
  async image(name: string): Promise<string | null> {
    return this.pictures.resolve(imageKey(name),
                                 () => sendOp('widgets.image', { name }));
  }

  /// A theme icon by the desktop's own name. SVG comes back as text,
  /// anything else as a PNG at the size asked for.
  async icon(name: string, size = 24): Promise<string | null> {
    return this.pictures.resolve(iconKey(this.boot.theme, name, size),
                                 () => sendOp('widgets.icon', { name, size }));
  }

  /// Either kind, as the bag carries them: nothing but the `img:` prefix
  /// says which of the two ops answers for a given value, so every view
  /// asks through here rather than deciding for itself.
  async picture(value: string, size = 16): Promise<string | null> {
    const kind = iconKind(value);
    if (kind === 'none') return null;
    return kind === 'image' ? this.image(value) : this.icon(value, size);
  }
}
