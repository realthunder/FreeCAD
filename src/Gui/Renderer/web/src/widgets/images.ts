// Pictures, icons, theme and locale for the mirrored panels
// (docs/Sandbox.md 7.22, G7, W3).
//
// Pure, like protocol.ts and layout.ts, and for the same reason: the node
// gate asserts these rules by replaying the recorded corpus, with no DOM
// and no socket. The fetching itself is injected, so the cache below is
// checkable without a host -- which is how "the image op is asked once per
// distinct id" becomes an assertion rather than a hope.
//
// Two kinds of picture travel on this wire, and telling them apart is the
// whole of the resolution rule:
//
//   - an `img:<sha1>` id, which the host filed in `Fw::ImageStore` because
//     the thing has no name a client could ask for: a custom-painted
//     leaf's grab, a button's QIcon, an item cell's decoration. Fetched
//     with `widgets.image {name}`, answered as a base64 PNG. It is
//     CONTENT-addressed, so a cache entry can never go stale and a changed
//     picture arrives as a different id;
//   - a NAME, which is one of FreeCAD's own icons (`Std_ViewFitAll`) or a
//     path. Fetched with `widgets.icon {name, size}`, answered as SVG text
//     or a base64 PNG at that size. The bytes depend on the host's icon
//     THEME, so the theme is part of the key.
//
// The bag carries both in the same keys (`icon`, `pixmap`, `windowIcon`,
// `itemIcons`, and a cell's `icon`), so nothing but the `img:` prefix says
// which one is in hand.

/// What `Fw::ImageStore` prefixes a content-addressed id with.
export const IMAGE_PREFIX = 'img:';

export function isImageId(value: unknown): boolean {
  return typeof value === 'string' && value.startsWith(IMAGE_PREFIX);
}

/// How a bag value naming a picture must be fetched: by content id, by
/// theme name, or not at all.
export type IconKind = 'image' | 'name' | 'none';

export function iconKind(value: unknown): IconKind {
  if (typeof value !== 'string' || value === '') return 'none';
  return value.startsWith(IMAGE_PREFIX) ? 'image' : 'name';
}

/// What the host answers `widgets.image` / `widgets.icon` with. A PNG comes
/// back base64, an SVG as its text.
export interface IconReply {
  format?: string;
  data?: string;
  width?: number;
  height?: number;
}

/// The reply as something an `<img src>` takes. An SVG is percent-encoded
/// rather than base64'd: it is text on the wire, and encodeURIComponent is
/// what keeps a `#` in a fill colour from cutting the URL short.
export function dataUrl(reply: IconReply | null | undefined): string | null {
  const data = typeof reply?.data === 'string' ? reply.data : '';
  if (data === '') return null;
  return reply?.format === 'svg'
    ? `data:image/svg+xml;charset=utf-8,${encodeURIComponent(data)}`
    : `data:image/png;base64,${data}`;
}

/// A page-lifetime cache of resolved pictures, keyed by whatever makes the
/// bytes different.
///
/// It holds the PROMISE, not the answer, so two widgets that ask for the
/// same icon in the same frame make ONE request -- the ask-once rule the
/// gate checks. A failure is not cached when it was the socket's fault
/// ('Offline', 'Timeout' -- "not yet", not "no"), so the next repaint asks
/// again; a real refusal (an evicted image, an unknown icon) is cached as
/// null, because asking again would get the same answer.
export class IconCache {
  private entries = new Map<string, Promise<string | null>>();
  /// How many fetches were actually started, for the gate.
  asked = 0;

  get size(): number {
    return this.entries.size;
  }

  /// Drop everything. The theme is part of every key, so this is only
  /// needed when a page wants to re-fetch regardless (a reconnect).
  clear(): void {
    this.entries.clear();
  }

  /// `fetch` is given the key's parts and answers a reply or throws.
  resolve(key: string, fetch: () => Promise<IconReply>): Promise<string | null> {
    const had = this.entries.get(key);
    if (had) return had;
    this.asked++;
    const p = fetch()
      .then((reply) => dataUrl(reply))
      .catch((err: unknown) => {
        const code = String((err as { code?: unknown })?.code ?? '');
        if (code === 'Offline' || code === 'Timeout') this.entries.delete(key);
        return null;
      });
    this.entries.set(key, p);
    return p;
  }
}

/// The key an `img:` id is cached under. The id IS the content, so neither
/// the theme nor a size belongs in it.
export function imageKey(name: string): string {
  return `img\n${name}`;
}

/// The key a named icon is cached under: the theme decides the bytes, and a
/// PNG is rasterized at the size it was asked for.
export function iconKey(theme: string, name: string, px: number): string {
  return `icon\n${theme}\n${name}\n${px}`;
}

// ---- Locale -----------------------------------------------------------------

/// The host's `QLocale::name()` as a BCP-47 tag, or null for the C locale.
///
/// Qt spells a locale `en_US`; the web wants `en-US`. `C` (and its POSIX
/// spelling) is not a language at all -- it means unformatted -- so it maps
/// to null rather than to English, which would bring thousands separators
/// to numbers the host prints without them.
export function localeTag(qtName: string | undefined | null): string | null {
  const name = (qtName ?? '').trim();
  if (name === '' || name === 'C' || name === 'POSIX') return null;
  return name.replace(/_/g, '-').replace(/\..*$/, '');
}

/// A number at a fixed number of decimals, in the host's locale.
///
/// Used where the host sends a VALUE and no text: a plain QDoubleSpinBox
/// carries `value` and `decimals`, while a quantity field carries the
/// string Qt already formatted (and that one is shown as it came -- the
/// host's units and decimals are not the client's to second-guess).
export function formatNumber(value: number, decimals: number, locale: string | null): string {
  if (!Number.isFinite(value)) return '';
  const places = Number.isFinite(decimals) && decimals >= 0 ? Math.min(decimals, 20) : 0;
  if (locale === null) return value.toFixed(places);
  try {
    return new Intl.NumberFormat(locale, {
      minimumFractionDigits: places,
      maximumFractionDigits: places,
    }).format(value);
  }
  catch {
    // an unknown tag is not worth failing a panel over
    return value.toFixed(places);
  }
}

/// A number the user typed, read back in the host's locale.
///
/// The display side formats with the host's separators, so the write side
/// has to undo them: a host whose locale groups with a point and decimates
/// with a comma shows 1.234,57, and `Number()` of that is NaN -- which
/// would send the host a text where it wanted a number. The separators are
/// taken from the locale itself rather than guessed, whitespace goes
/// (several locales group with a non-breaking space), and a trailing unit
/// or suffix is ignored so "10.00 mm" reads as 10.
export function parseLocaleNumber(text: string, locale: string | null): number {
  const raw = text.replace(/\s/g, '');
  if (raw === '') return NaN;
  let decimal = '.';
  let group = ',';
  if (locale !== null) {
    try {
      for (const part of new Intl.NumberFormat(locale).formatToParts(12345.6)) {
        if (part.type === 'decimal') decimal = part.value;
        else if (part.type === 'group') group = part.value;
      }
    }
    catch {
      // an unknown tag: the defaults are right often enough
    }
  }
  const normal = raw.split(group).join('').split(decimal).join('.');
  const found = /-?\d*\.?\d+(?:[eE][-+]?\d+)?/.exec(normal);
  return found ? Number(found[0]) : NaN;
}

// ---- The pointer, replayed --------------------------------------------------

/// A custom-painted leaf is a PICTURE in the page: there is no widget to
/// click, so the pointer is sent to the host and replayed into the real
/// widget (`PanelMirror::replayMouse`, 7.19 M3). These build the argument
/// lists that function reads, so their ORDER is not ours to choose.

/// Qt::MouseButton. A DOM `button` is 0 left / 1 middle / 2 right, which is
/// neither Qt's numbering nor its bit values.
export const QT_LEFT = 1;
export const QT_RIGHT = 2;
export const QT_MIDDLE = 4;

/// Qt::KeyboardModifier.
export const QT_SHIFT = 0x02000000;
export const QT_CONTROL = 0x04000000;
export const QT_ALT = 0x08000000;
export const QT_META = 0x10000000;

export function qtButton(domButton: number): number {
  return domButton === 2 ? QT_RIGHT : domButton === 1 ? QT_MIDDLE : QT_LEFT;
}

/// A DOM `buttons` mask happens to agree with Qt's (1 left, 2 right,
/// 4 middle), so this is a pass-through -- named, because the agreement is
/// a coincidence worth stating rather than a rule to rely on silently.
export function qtButtons(domButtons: number): number {
  return domButtons & (QT_LEFT | QT_RIGHT | QT_MIDDLE);
}

export interface ModifierState {
  shiftKey?: boolean;
  ctrlKey?: boolean;
  altKey?: boolean;
  metaKey?: boolean;
}

export function qtModifiers(e: ModifierState): number {
  return (e.shiftKey ? QT_SHIFT : 0) | (e.ctrlKey ? QT_CONTROL : 0)
    | (e.altKey ? QT_ALT : 0) | (e.metaKey ? QT_META : 0);
}

export type MouseKind = 'press' | 'release' | 'move' | 'dblclick' | 'enter' | 'leave';

/// `mouse`: [type, x, y, button, buttons, modifiers], x and y in the
/// PICTURE's pixels -- the host scales them back itself when the grab was
/// capped.
export function mouseArgs(kind: MouseKind, x: number, y: number,
                          button: number, buttons: number,
                          mods: ModifierState): unknown[] {
  return [kind, Math.round(x), Math.round(y), qtButton(button), qtButtons(buttons),
          qtModifiers(mods)];
}

/// `wheel`: [x, y, dx, dy, buttons, modifiers]. A DOM wheel delta is
/// positive DOWNWARD and Qt's is positive upward, so the sign flips; the
/// magnitude is Qt's eighth-of-a-degree notch.
export const WHEEL_NOTCH = 120;

export function wheelArgs(x: number, y: number, deltaX: number, deltaY: number,
                          buttons: number, mods: ModifierState): unknown[] {
  const notches = (d: number) => (d === 0 ? 0 : Math.round(-Math.sign(d) * WHEEL_NOTCH));
  return [Math.round(x), Math.round(y), notches(deltaX), notches(deltaY),
          qtButtons(buttons), qtModifiers(mods)];
}

/// Where a pointer landed inside a picture, in the picture's own pixels:
/// the element is laid out at whatever width the card gives it, while the
/// host knows only the image it sent.
export function pictureAt(rect: { left: number; top: number; width: number; height: number },
                          natural: { width: number; height: number },
                          clientX: number, clientY: number): { x: number; y: number } {
  const sx = rect.width > 0 && natural.width > 0 ? natural.width / rect.width : 1;
  const sy = rect.height > 0 && natural.height > 0 ? natural.height / rect.height : 1;
  return { x: (clientX - rect.left) * sx, y: (clientY - rect.top) * sy };
}
