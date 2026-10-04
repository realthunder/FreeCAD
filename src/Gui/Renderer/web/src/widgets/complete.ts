// The completion controller's pure half (docs/Sandbox.md 7.23): when to ask
// the host, what to show between asks, how to splice what the user picks,
// and which keyboard a phone should offer.
//
// No DOM and no socket, so the node gate asserts these rules directly --
// which matters more here than anywhere else in the panel, because the ones
// that keep a phone usable (a decimal point is not a trigger; a decimal
// keyboard has no letters) are invisible on a desktop and would otherwise
// only be found by holding a phone.

/// What the host answered, and what it was answered FOR. A reply that
/// arrives after the text moved on is stale, and the caller compares.
export interface CompletionSet {
  /// The text and caret the host computed this for.
  text: string;
  pos: number;
  items: string[];
  /// Per item, the tool tip the desktop's popup would have shown.
  details: string[];
  /// The range in `text` that an item replaces.
  start: number;
  end: number;
}

/// Why the client would ask the host now, or null for "do not ask".
export type Trigger = 'dot' | 'word' | 'ask';

/// What an expression WOULD evaluate to, for the dialog's result line.
/// An expression is invalid for most of the time it is being typed, so
/// this is something to show rather than an error to handle: `severity`
/// says how to show it.
export interface ExpressionPreview {
  /// The evaluated value, in the user's units ('' when there is none).
  result: string;
  severity: 'ok' | 'warning' | 'error';
  /// Why, when there is no result: a parse error, a cyclic reference,
  /// or a note that functions are not evaluated in a preview.
  message: string;
}

/// A run of this many word characters arms a request on its own. The
/// desktop's CommandCompleter refuses under three; two is this side's
/// house rule, because a browser round trip is cheaper than a popup and
/// the first segment is usually short (`Pad`, `Box`).
export const MIN_WORD = 2;

const WORD = /[A-Za-z0-9_]/;
const LETTER = /[A-Za-z_]/;
const DIGIT = /[0-9]/;
/// What a dot may follow and still mean "a name continues": a name, or the
/// close of one -- `<<Long label>>.`, `Sketch.Constraints[0].`.
const AFTER = /[A-Za-z0-9_)\]>]/;

/// The word being typed at `pos`: what a local filter narrows on.
export function wordAt(text: string, pos: number): string {
  let at = Math.max(0, Math.min(pos, text.length));
  while (at > 0 && WORD.test(text[at - 1])) at--;
  return text.slice(at, pos);
}

/// Should what is now typed produce a request to the host?
///
/// The dot is the trigger: it is the one character in an expression that
/// means "a name follows", it is on a phone's primary keyboard, and it
/// bounds the traffic to one round trip per dotted segment rather than one
/// per keystroke. Two qualifications, both from the code:
///
///   - A dot after a DIGIT is a decimal point, not a trigger. `10.` in a
///     quantity field is a number; firing there would throw a menu over
///     the keyboard every time someone types a length.
///   - A first segment has no dot, so a run of word characters arms it
///     too. That is the chatty case, which is why the caller debounces it
///     and why MIN_WORD exists.
export function triggerFor(text: string, pos: number): Trigger | null {
  const at = Math.max(0, Math.min(pos, text.length));
  if (at === 0) return null;
  const last = text[at - 1];
  if (last === '.') {
    const before = at >= 2 ? text[at - 2] : '';
    if (!before || DIGIT.test(before)) return null;   // 10. is a number
    return AFTER.test(before) ? 'dot' : null;
  }
  if (!WORD.test(last)) return null;
  const word = wordAt(text, at);
  // A run that is all digits is a number being typed, not a name.
  if (!LETTER.test(word[0] ?? '')) return null;
  return word.length >= MIN_WORD ? 'word' : null;
}

/// Can an answered set still serve `text`/`pos` without asking again?
///
/// Yes while the caret is still inside the segment the host answered for:
/// it returned every member of that segment, so narrowing is a local
/// string match. Another dot, an edit before the segment, or a caret that
/// moved back into it voids the set -- and so does local filtering that
/// comes up empty, which the caller checks by asking again.
export function stillApplies(set: CompletionSet, text: string, pos: number): boolean {
  if (pos < set.end || pos > text.length) return false;
  if (text.slice(0, set.end) !== set.text.slice(0, set.end)) return false;
  // what has been typed since must be one plain word, no new dot
  const since = text.slice(set.end, pos);
  return since === '' || (WORD.test(since[0]) && !/[^A-Za-z0-9_]/.test(since));
}

/// The items of `set` that match what is typed now, the closest first.
///
/// The match is on an item's LAST segment: the host answers full paths
/// (`Other.Width`), and what the user is typing is the member, not the
/// path. Case-insensitive, prefix before substring -- the desktop's
/// default filter mode is MatchContains, case-insensitive, so this
/// narrows the same set the popup would have shown.
export function filterSet(set: CompletionSet, text: string, pos: number): string[] {
  const needle = wordAt(text, pos).toLowerCase();
  if (!needle) return set.items;
  const prefix: string[] = [];
  const inside: string[] = [];
  for (const item of set.items) {
    const tail = item.slice(item.lastIndexOf('.') + 1).toLowerCase();
    if (tail.startsWith(needle)) prefix.push(item);
    else if (tail.includes(needle)) inside.push(item);
  }
  return [...prefix, ...inside];
}

/// The text after the user picks `item`, and where the caret lands.
///
/// The replaced range runs from the host's `start` to whichever is later,
/// the host's `end` or the caret: the user may have typed further into the
/// segment while the answer was in flight or being filtered locally, and
/// those characters are part of the name being replaced.
export function splice(set: CompletionSet, item: string, text: string, pos: number):
    { text: string; pos: number } {
  const start = Math.max(0, Math.min(set.start, text.length));
  const end = Math.max(set.end, Math.min(pos, text.length));
  const head = text.slice(0, start);
  return { text: head + item + text.slice(end), pos: head.length + item.length };
}

/// A set built from a source that answers `start` but no `end` -- the
/// Python console's guest (sandbox/console.py `complete()`), whose
/// rlcompleter returns the whole dotted path and the index it replaces
/// from, against the source up to the caret.
///
/// `end` is the caret the answer was asked for: everything from `start`
/// to there is the name being completed, which is exactly what the host
/// op states outright. With that filled in, the console runs the same
/// controller as the panel's fields -- one ask per dotted segment, local
/// narrowing in between, and a splice that survives typing done while the
/// answer was in flight.
export function setFromGuest(text: string, pos: number, items: string[],
                             start: number): CompletionSet {
  const at = Math.max(0, Math.min(pos, text.length));
  return {
    text,
    pos: at,
    items,
    details: items.map(() => ''),
    start: Math.max(0, Math.min(start, at)),
    end: at,
  };
}

/// What keyboard a phone should offer for a field holding `text`.
///
/// A quantity field wants `decimal`, and a decimal keyboard has NO
/// letters -- so the moment the value starts with the expression lead
/// char, the field has to become a text keyboard or `Pad.Length` cannot be
/// typed at all. The desktop never sees this: it has one keyboard.
export function inputModeFor(text: string, numeric: boolean): 'decimal' | 'text' {
  if (!numeric) return 'text';
  return text.trimStart().startsWith('=') ? 'text' : 'decimal';
}
