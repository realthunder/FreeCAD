// The property inspector (docs/ThinClient.md §4.3): a floating card
// (bottom sheet on narrow screens) that appears on selection.
// Navigation is a group drop-down plus a keyword box with live
// filtering — a non-empty keyword searches every group with the match
// highlighted; clearing it restores the selected group.
//
// The same card also serves what a pick cannot reach: the 3D view
// (every Render_*/Shadow_* knob, the ones worth turning while looking
// at the result) and the document, read together as one 'View &
// document' subject — from this side they are the settings of what you
// are looking at, and which container a knob lives in is the backend's
// business. Nothing in the scene stands for them, so they get one
// launcher action and a switcher in the header — which is also how you
// get back to the picked object without re-picking it.
import {
  For,
  Show,
  createEffect,
  createMemo,
  createResource,
  createSignal,
  onCleanup,
  untrack,
} from 'solid-js';
import type { JSX } from 'solid-js';
import {
  PropDescriptor,
  PropertiesReply,
  SelectionItem,
  Subject,
  getProperties,
  setProperty,
} from './control';
import { NARROW, Pos, draggable, fitOnScreen, loadPos, posStyle }
  from './panel';

const ALL = '\u001Fall';  // sentinel that can't collide with a group name
// U+001F (unit separator) rather than a NUL: equally impossible in a
// property group name, but a NUL anywhere in the first 8 kB makes git
// treat this whole file as binary — no diffs, no blame, and grep skips
// it unless asked for -a.

function fmtValue(p: PropDescriptor): JSX.Element {
  const v = p.value;
  switch (p.type) {
    case 'Bool':
      return <span class="fc-val">{v ? 'true' : 'false'}</span>;
    case 'Quantity':
      return (
        <span class="fc-val">
          {typeof v === 'number' ? +v.toFixed(4) : String(v)}
          <span class="fc-unit"> {p.unit}</span>
        </span>
      );
    case 'Int':
    case 'Float':
      return (
        <span class="fc-val">
          {typeof v === 'number' ? +v.toFixed(4) : String(v)}
        </span>
      );
    case 'Enum':
      return (
        <span class="fc-val">
          {p.enums && typeof v === 'number' ? p.enums[v] : String(v)}
        </span>
      );
    case 'Color':
      return (
        <span class="fc-val">
          <span class="fc-swatch" style={{ background: String(v) }} />
          {String(v)}
        </span>
      );
    case 'Vector': {
      const o = v as any;
      return (
        <span class="fc-val fc-vec">
          {o ? `${+o.x.toFixed(3)}, ${+o.y.toFixed(3)}, ${+o.z.toFixed(3)}` : ''}
        </span>
      );
    }
    case 'Placement': {
      const o = v as any;
      if (!o) return <span class="fc-val" />;
      const p0 = o.position, r = o.rotation;
      return (
        <span class="fc-val fc-vec">
          {`(${+p0.x.toFixed(2)}, ${+p0.y.toFixed(2)}, ${+p0.z.toFixed(2)}) ` +
           `∠${+r.angle.toFixed(1)}°`}
        </span>
      );
    }
    case 'String':
      return <span class="fc-val">{String(v ?? '')}</span>;
    default:
      return <span class="fc-val fc-raw">{p.type}</span>;
  }
}

/// Editors for the editable slice (docs/ThinClient.md §4.B): native
/// inputs, commit on change (toggles/selects/color) or Enter/blur
/// (number and text fields). Everything else falls back to the static
/// rendering.
function editValue(
  p: PropDescriptor,
  commit: (p: PropDescriptor, value: unknown) => void,
): JSX.Element {
  switch (p.type) {
    case 'Bool':
      return (
        <input
          type="checkbox"
          class="fc-edit fc-check"
          checked={!!p.value}
          onChange={(e) => commit(p, e.currentTarget.checked)}
        />
      );
    case 'Int':
    case 'Float':
    case 'Quantity': {
      const num = (el: HTMLInputElement) => {
        const v = parseFloat(el.value);
        if (!Number.isFinite(v) || v === p.value) {
          el.value = String(p.value);
          return;
        }
        commit(p, p.type === 'Int' ? Math.round(v) : v);
      };
      return (
        <span class="fc-val">
          <input
            type="number"
            class="fc-edit fc-num"
            inputmode="decimal"
            value={typeof p.value === 'number' ? +p.value.toFixed(4) : ''}
            min={p.constraints?.min}
            max={p.constraints?.max}
            step={p.constraints?.step || (p.type === 'Int' ? 1 : 'any')}
            onChange={(e) => num(e.currentTarget)}
            onKeyDown={(e) => {
              if (e.key === 'Enter') (e.currentTarget as HTMLInputElement).blur();
            }}
          />
          <Show when={p.type === 'Quantity'}>
            <span class="fc-unit"> {p.unit}</span>
          </Show>
        </span>
      );
    }
    case 'Enum':
      return (
        <select
          class="fc-edit fc-enum"
          value={String(p.value)}
          onChange={(e) => commit(p, parseInt(e.currentTarget.value, 10))}
        >
          <For each={p.enums ?? []}>
            {(name, i) => <option value={String(i())}>{name}</option>}
          </For>
        </select>
      );
    case 'String':
      return (
        <input
          type="text"
          class="fc-edit fc-text"
          value={String(p.value ?? '')}
          onChange={(e) => {
            if (e.currentTarget.value !== p.value)
              commit(p, e.currentTarget.value);
          }}
          onKeyDown={(e) => {
            if (e.key === 'Enter') (e.currentTarget as HTMLInputElement).blur();
          }}
        />
      );
    case 'Color':
      return (
        <input
          type="color"
          class="fc-edit fc-color"
          value={String(p.value)}
          onChange={(e) => commit(p, e.currentTarget.value)}
        />
      );
    default:
      return fmtValue(p);
  }
}

/// The QCompleter bit: the matched substring rendered highlighted.
function highlightName(name: string, needle: string): JSX.Element {
  if (!needle) return name;
  const at = name.toLowerCase().indexOf(needle.toLowerCase());
  if (at < 0) return name;
  return (
    <>
      {name.slice(0, at)}
      <mark>{name.slice(at, at + needle.length)}</mark>
      {name.slice(at + needle.length)}
    </>
  );
}

export function Inspector(props: {
  selection: () => SelectionItem[];
  /// Bumped by the menu to open the card on a subject. A counter rather
  /// than a boolean: asking for the same subject twice in a row must
  /// re-open a card the user closed in between.
  request: () => { subject: Subject; n: number } | null;
  /// Reported back so the menu button can stand aside on a narrow
  /// screen, where the card is a bottom sheet over that corner.
  onCardOpen?: (open: boolean) => void;
  /// The host made this connection view-only (docs/MultiDocServe.md
  /// §8). Every editor becomes a reading, because offering a field
  /// whose commit the backend will refuse is worse than not offering
  /// it — and a press on one says why.
  viewOnly?: () => boolean;
}) {
  const first = createMemo(() => {
    const sel = props.selection();
    return sel.find((s) => s.obj) ?? null;
  });

  // Selecting shows only the one-line pill (user spec); the card — and
  // its first getProperties round trip — waits for the expand button.
  const [expanded, setExpanded] = createSignal(false);
  const [subject, setSubject] = createSignal<Subject>('object');

  const [reply, { refetch }] = createResource(
    () => {
      if (!expanded()) return null;
      const s = subject();
      if (s !== 'object') return { subject: s, doc: '', obj: '' };
      const f = first();
      return f ? { subject: s, doc: f.doc ?? '', obj: f.obj! } : null;
    },
    (k): Promise<PropertiesReply> => getProperties(k.doc, k.obj, k.subject),
  );

  const path = () => {
    const f = first();
    if (!f) return '';
    // Doc-qualify only objects from outside the scene's home document.
    const prefix = f.home === false && f.doc ? `${f.doc}#` : '';
    return `${prefix}${f.obj}${f.sub ? '.' + f.sub : ''}`;
  };

  // Header line for the card: an object is identified by its path, the
  // other two subjects by what they are (a client sees one 3D view and
  // one document, so there is nothing to disambiguate).
  const title = () => {
    if (subject() === 'viewdoc')
      return reply()?.label ?? reply()?.doc ?? 'Properties';
    return reply()?.label ?? first()?.label ?? first()?.obj ?? '';
  };
  const subtitle = () => {
    if (subject() === 'viewdoc') return 'View and document';
    return path();
  };

  // One in-flight edit at a time; its row dims until the ack. The ack
  // triggers a refetch — an edit can cascade into other properties, so
  // the whole card re-reads rather than trusting its local copy.
  const [pendingRow, setPendingRow] = createSignal('');
  const [rowError, setRowError] = createSignal<{ row: string; code: string }
      | null>(null);

  // The "you cannot edit this" flash, raised by a press on a locked
  // row. Times out on its own: it answers a gesture, it is not state.
  const [locked, setLocked] = createSignal(false);
  let lockTimer = 0;
  const flashLocked = () => {
    setLocked(true);
    clearTimeout(lockTimer);
    lockTimer = window.setTimeout(() => setLocked(false), 2600);
  };
  onCleanup(() => clearTimeout(lockTimer));

  const commit = (p: PropDescriptor, value: unknown) => {
    const r = reply();
    if (props.viewOnly?.()) {
      // Belt and braces: the rows render read-only in this mode, so
      // this only catches an in-flight editor when the host flips the
      // switch mid-edit. The backend would refuse it anyway.
      flashLocked();
      return;
    }
    if (!r || pendingRow()) return;
    setPendingRow(p.name);
    setRowError(null);
    setProperty(r.doc, r.obj, p.scope, p.name, value)
      .catch((e) => setRowError({ row: p.name, code: e?.code ?? 'error' }))
      // Refetch on success AND failure: a rejected edit leaves the
      // typed value in the field, and only fresh descriptors (same
      // values, new objects) make the rows re-render the truth.
      .then(() => refetch())
      .finally(() => setPendingRow(''));
  };

  const [group, setGroup] = createSignal<string>(ALL);
  const [keyword, setKeyword] = createSignal('');
  const [closed, setClosed] = createSignal(false);

  // Where the user last dragged each panel. Kept apart: the card and
  // the pill are different sizes and are never on screen together, so
  // one shared position would move each of them somewhere the other
  // was put. Restored from the last session — a panel that returns to
  // the corner you moved it out of has not really moved.
  const [cardPos, setCardPos] = createSignal<Pos | null>(
    loadPos('fc.inspector.card'));
  const [pillPos, setPillPos] = createSignal<Pos | null>(
    loadPos('fc.inspector.pill'));

  // The pill and the card are one thing that is small or large, not two
  // things in two places: expanding hands the pill's corner to the card,
  // so the panel grows where it stands instead of jumping to wherever
  // its own CSS corner is. Only that direction exists — the card has no
  // collapse, it closes.
  let pillEl: HTMLDivElement | undefined;

  const handOver = (from: HTMLElement | undefined,
                    to: (p: Pos) => void) => {
    if (!from || window.innerWidth <= NARROW) return;
    const r = from.getBoundingClientRect();
    to({ x: Math.round(r.left), y: Math.round(r.top) });
  };

  // A window that shrank can leave a panel half outside it; nudge it
  // back rather than making the user find it.
  const reclamp = () => {
    for (const [get, set] of [[cardPos, setCardPos],
                              [pillPos, setPillPos]] as const) {
      const p = get();
      if (!p) continue;
      set({ x: Math.min(p.x, Math.max(0, window.innerWidth - 60)),
            y: Math.min(p.y, Math.max(0, window.innerHeight - 40)) });
    }
  };
  window.addEventListener('resize', reclamp);
  onCleanup(() => window.removeEventListener('resize', reclamp));

  /// Open the card on a subject — the launcher's whole behaviour, and
  /// the switcher's. Navigation resets because the groups of one
  /// subject mean nothing to another (an object's `Base` is not the
  /// view's `Render`).
  const openOn = (s: Subject) => {
    setSubject(s);
    setKeyword('');
    setGroup(ALL);
    setClosed(false);
    setExpanded(true);
  };

  // A new selection resets the navigation and returns the card to the
  // object: a pick is a statement about what the user is now
  // interested in — including while the card is showing the view or
  // the document, where the picked object is what they just asked
  // about. An open card therefore follows the selection; only a
  // collapsed one stays collapsed, because expanding is a request the
  // user has not made yet.
  let lastKey = '';
  createEffect(() => {
    const f = first();
    if (f) {
      // Track identity, not object presence: re-picking another
      // element of the same object leaves the card exactly as it is.
      const key = `${f.objectKey}`;
      if (key !== lastKey) {
        lastKey = key;
        // untrack: whether the card is open decides what a *pick*
        // does, it must not itself re-run this. Tracking it would
        // make opening the card on the view snap straight back to
        // the object.
        if (!untrack(card)) setExpanded(false);
      }
      setSubject('object');
      setClosed(false);
      setKeyword('');
      setGroup(ALL);
    }
    else {
      lastKey = '';
      // Nothing picked any more. An open card must not sit there
      // showing an object that is no longer selected, and closing it
      // would throw away a panel the user opened: it falls back to the
      // container it was last on (the view, unless they had asked for
      // the document).
      if (untrack(card) && untrack(subject) === 'object') {
        setSubject('viewdoc');
        setKeyword('');
        setGroup(ALL);
      }
    }
  });

  const groups = createMemo(() => {
    const seen: string[] = [];
    for (const p of reply()?.props ?? []) {
      if (!p.hidden && !seen.includes(p.group)) seen.push(p.group);
    }
    return seen.sort((a, b) => a.localeCompare(b));
  });

  // Selected group vanished with a new object → back to All.
  createEffect(() => {
    if (group() !== ALL && !groups().includes(group())) setGroup(ALL);
  });

  const rows = createMemo(() => {
    const all = (reply()?.props ?? []).filter((p) => !p.hidden);
    const kw = keyword().trim();
    if (kw) {
      // Keyword searches across every group, QCompleter-style.
      return all.filter((p) =>
        p.name.toLowerCase().includes(kw.toLowerCase()),
      );
    }
    if (group() === ALL) return all;
    return all.filter((p) => p.group === group());
  });

  const showGroupTag = createMemo(
    () => keyword().trim() !== '' || group() === ALL,
  );

  const card = () => expanded() && !closed();

  // The menu asks for a subject; the card opens on it. The pill's corner
  // is handed over first when there is a pill to hand it over from, so
  // the card still grows where the user last put things.
  createEffect((prev: number | undefined) => {
    const r = props.request();
    if (r && r.n !== prev) {
      handOver(pillEl, setCardPos);
      openOn(r.subject);
    }
    return r?.n;
  });

  createEffect(() => props.onCardOpen?.(card()));

  return (
    <>
      <Show when={first() && !closed() && !card()}>
          <div
            class="fc-pill"
            role="status"
            aria-label="Selection"
            style={posStyle(pillPos())}
            ref={(el) => {
              pillEl = el;
              draggable(el, () => el, setPillPos, 'fc.inspector.pill');
              fitOnScreen(el, pillPos, setPillPos);
            }}
          >
            <span class="fc-pill-label">
              {first()!.label ?? first()!.obj}
              <Show when={props.selection().length > 1}>
                <span class="fc-count"> +{props.selection().length - 1}</span>
              </Show>
            </span>
            <span class="fc-pill-path">{path()}</span>
            <button
              class="fc-expand"
              onClick={() => {
                handOver(pillEl, setCardPos);
                setExpanded(true);
              }}
              aria-label="Show properties"
              title="Show properties"
            >⌄</button>
            <button class="fc-close" onClick={() => setClosed(true)}
                    aria-label="Close">×</button>
          </div>
      </Show>

      <Show when={card()}>
      {(() => {
      let panel!: HTMLDivElement;
      return (
      <div
        class="fc-inspector"
        role="dialog"
        aria-label="Properties"
        style={posStyle(cardPos())}
        ref={(el) => {
          panel = el;
          fitOnScreen(el, cardPos, setCardPos);
        }}
      >
        <div
          class="fc-head fc-drag"
          title="Drag to move"
          ref={(el) => draggable(el, () => panel, setCardPos,
                                 'fc.inspector.card')}
        >
          {/* Laid out like the pill's one line — label then path — so
              the two read as one panel at two sizes rather than as a
              card that replaced a pill. */}
          <div class="fc-title">
            <span class="fc-label">
              {title()}
              <Show when={subject() === 'object' && props.selection().length > 1}>
                <span class="fc-count"> +{props.selection().length - 1}</span>
              </Show>
            </span>
            <span class="fc-subtitle">{subtitle()}</span>
          </div>
          <button class="fc-close" onClick={() => setClosed(true)}
                  aria-label="Close">×</button>
        </div>

        {/* Subject switcher. Object is offered only when something is
            picked — an empty card would be a dead end, and the pill is
            what says whether there is anything to inspect. */}
        <div class="fc-subjects" role="tablist" aria-label="Inspect">
          <For each={[
            ['object', 'Object'],
            ['viewdoc', 'View & document'],
          ] as [Subject, string][]}>
            {([s, label]) => (
              <button
                class="fc-subj"
                role="tab"
                classList={{ 'fc-subj-on': subject() === s }}
                aria-selected={subject() === s}
                disabled={s === 'object' && !first()}
                onClick={() => openOn(s)}
              >{label}</button>
            )}
          </For>
        </div>

        <div class="fc-nav">
          <select
            class="fc-group"
            value={keyword().trim() ? ALL : group()}
            disabled={!!keyword().trim()}
            onChange={(e) => setGroup(e.currentTarget.value)}
          >
            <option value={ALL}>All</option>
            <For each={groups()}>{(g) => <option value={g}>{g}</option>}</For>
          </select>
          <input
            class="fc-filter"
            type="search"
            placeholder="Filter properties"
            value={keyword()}
            onInput={(e) => setKeyword(e.currentTarget.value)}
          />
        </div>

        <div class="fc-body">
          {/* The mode, stated once at the top rather than repeated on
              every row — and louder for a moment when a press on a
              locked row asks why nothing happened. */}
          <Show when={props.viewOnly?.()}>
            <div class="fc-note fc-viewonly"
                 classList={{ 'fc-viewonly-flash': locked() }}>
              View only — the host has disabled editing
            </div>
          </Show>
          <Show when={reply.loading}>
            <div class="fc-note">Loading…</div>
          </Show>
          <Show when={reply.error}>
            <div class="fc-note fc-err">
              {(reply.error as any)?.code === 'Offline'
                ? 'Backend offline'
                : `Failed: ${(reply.error as any)?.code ?? 'error'}`}
            </div>
          </Show>
          <Show when={!reply.loading && !reply.error && rows().length === 0}>
            <div class="fc-note">No matching properties</div>
          </Show>
          <For each={rows()}>
            {(p) => (
              <div
                class="fc-row"
                classList={{ 'fc-pending': pendingRow() === p.name,
                             'fc-locked': !!props.viewOnly?.() && !p.readonly }}
                title={props.viewOnly?.() && !p.readonly
                  ? 'View only — the host has disabled editing'
                  : (p.doc ?? '')}
                onPointerDown={() => {
                  // A read-only property was never editable, so a press
                  // on it says nothing about the mode; only a field
                  // this connection would otherwise own gets the flash.
                  if (props.viewOnly?.() && !p.readonly) flashLocked();
                }}
              >
                <span class="fc-name" classList={{ 'fc-ro': p.readonly }}>
                  {highlightName(p.name, keyword().trim())}
                  <Show when={showGroupTag()}>
                    <span class="fc-grouptag">{p.group}</span>
                  </Show>
                  <Show when={rowError()?.row === p.name}>
                    <span class="fc-rowerr">{rowError()!.code}</span>
                  </Show>
                </span>
                {p.readonly || props.viewOnly?.()
                  ? fmtValue(p) : editValue(p, commit)}
              </div>
            )}
          </For>
        </div>
      </div>
      );
      })()}
      </Show>
    </>
  );
}
