// The omni search box in the browser (docs/OmniSearch.md sec 6): the
// mirror of the desktop's Std_OmniSearch over the control channel.
// One line at the top centre, '/' opens it, and the same grammar --
// "/ Box.Length", "/cmd draw style", "#.Comment",
// "#.View2.DrawStyle", ".Height" for the selection.
//
// Latency is the design constraint. The box re-filters on every
// keystroke, so nothing per keystroke may cross the wire: the command
// and parameter lists are held locally by version (omnicatalog.ts),
// the document's objects are fetched once per opening, and an
// object's property descriptors once per object and cached for the
// box's life. What does go out is small and never per key: the
// volatile detail of the rows on screen (a parameter's value, a
// command's active state) after the list settles, and the resolve of
// the typed text on Enter or Tab. Every reply is id-correlated, and a
// counter drops the ones a later keystroke made stale.
import { For, Show, createEffect, createMemo, createSignal, on, onCleanup, untrack } from 'solid-js';
import type { JSX } from 'solid-js';
import { PickList, moveInList } from './widgets/picker.tsx';
import type { PickRow } from './widgets/picker.tsx';
import {
  commandChildren,
  getContainerProperties,
  getProperties,
  isBrowserSafeCommand,
  omniResolve,
  omniRows,
  paramReset,
  paramSet,
  runCommand,
  setProperty,
} from './control';
import type {
  CommandChild,
  PropDescriptor,
  PropScope,
  PropertiesReply,
  ResolveReply,
  SelectionItem,
} from './control';
import { commands, params } from './omnicatalog';
import { createPathCompleter } from './pathcomplete';
import type { PropTarget } from './pathcomplete';
import type { Indexed, ParamRow } from './omnicatalog';
import { editValue, fmtValue } from './inspector';

export type Mode = 'chooser' | 'object' | 'command' | 'param';

export interface Input {
  mode: Mode;
  /// The text after the prefix
  query: string;
  /// Where the query starts in the full text
  offset: number;
}

/// No '/param ' here: the host's preferences are not a browser's to change
/// (docs/ShareAccess.md sec 2.2), so the mirror does not offer the mode.
const PREFIXES: [string, Mode][] = [
  ['/cmd ', 'command'],
  ['/ ', 'object'],
];

/// The desktop grammar (OmniSearch::parseInput): a full prefix picks
/// the mode, a lone '/' or a partial prefix is the chooser, and text
/// without a slash is an object query as typed.
export function parseInput(text: string): Input {
  if (!text.startsWith('/')) return { mode: 'object', query: text, offset: 0 };
  for (const [prefix, mode] of PREFIXES) {
    if (text.startsWith(prefix))
      return { mode, query: text.slice(prefix.length), offset: prefix.length };
  }
  return { mode: 'chooser', query: text.slice(1), offset: 1 };
}

/// Rows on screen at most; the rest is a count
const LIMIT = 60;
/// How long the list must stand still before its rows' detail is asked for
const SETTLE_MS = 120;

interface Row extends PickRow {
  /// What the text becomes when the row is picked
  complete?: string;
  kind: 'mode' | 'command' | 'param' | 'object' | 'property' | 'member';
  group?: boolean;
  param?: Indexed<ParamRow>;
  prop?: { target: PropTarget; prop: PropDescriptor };
}

type Panel =
  | { kind: 'param'; row: Indexed<ParamRow>; value: string; set: boolean }
  | { kind: 'property'; target: PropTarget; prop: PropDescriptor }
  | { kind: 'object'; doc: string; obj: string; label: string; type: string; path: string;
      props: PropDescriptor[] | null }
  | { kind: 'children'; name: string; title: string; exclusive: boolean; items: CommandChild[] };

const MODE_ROWS: Row[] = [
  { key: '/ ', kind: 'mode', title: '/ ', desc: 'Objects, properties, documents and views',
    complete: '/ ' },
  { key: '/cmd ', kind: 'mode', title: '/cmd ', desc: 'Commands, by keyword', complete: '/cmd ' },
];

// ---- parameter editors ------------------------------------------------

/// A packed 0xRRGGBBAA as the colour input's "#rrggbb", and back
function hexToCss(value: string): { css: string; alpha: string } {
  const n = parseInt(value, 16);
  const s = (Number.isFinite(n) ? n >>> 0 : 0).toString(16).padStart(8, '0');
  return { css: '#' + s.slice(0, 6), alpha: s.slice(6) };
}

function paramEditor(
  row: Indexed<ParamRow>,
  value: () => string,
  readonly: boolean,
  commit: (text: string) => void,
): JSX.Element {
  if (readonly) return <span class="fc-val">{value()}</span>;
  const proxy = row.proxy ?? '';
  if (proxy === 'ComboBox' && row.items) {
    // The stored value is the item's index, or its data string
    return (
      <select class="fc-edit fc-enum" value={value()}
              onChange={(e) => commit(e.currentTarget.value)}>
        <For each={row.items}>
          {(item, i) => (
            <option value={row.dataIsString ? (item.data ?? '') : String(i())}
                    title={item.tooltip ?? ''}>{item.text}</option>
          )}
        </For>
      </select>
    );
  }
  if (row.type === 'Bool') {
    return (
      <input type="checkbox" class="fc-edit fc-check" checked={value() === 'true'}
             onChange={(e) => commit(e.currentTarget.checked ? 'true' : 'false')} />
    );
  }
  if (proxy === 'Color' || row.type === 'Hex') {
    // Every Hex parameter in the tree is a packed colour; the alpha
    // byte is kept as it is (the picker has none)
    return (
      <input type="color" class="fc-edit fc-color" value={hexToCss(value()).css}
             onChange={(e) => {
               const rgb = e.currentTarget.value.slice(1);
               commit('0x' + rgb + hexToCss(value()).alpha);
             }} />
    );
  }
  if (row.type === 'Int' || row.type === 'UInt' || row.type === 'Float') {
    const integer = row.type !== 'Float';
    return (
      <input type="number" class="fc-edit fc-num" inputmode="decimal"
             value={value()}
             min={row.min ?? (row.type === 'UInt' ? 0 : undefined)}
             max={row.max}
             step={row.step || (integer ? 1 : 'any')}
             onChange={(e) => {
               const v = parseFloat(e.currentTarget.value);
               if (!Number.isFinite(v)) return;
               commit(integer ? String(Math.round(v)) : String(v));
             }}
             onKeyDown={(e) => { if (e.key === 'Enter') e.currentTarget.blur(); }} />
    );
  }
  // String: a line edit, a file name, a shortcut, a line pattern
  return (
    <input type="text" class="fc-edit fc-text" value={value()}
           onChange={(e) => commit(e.currentTarget.value)}
           onKeyDown={(e) => { if (e.key === 'Enter') e.currentTarget.blur(); }} />
  );
}

// ---- the box ------------------------------------------------------------

export function OmniBox(props: {
  open: () => boolean;
  onClose: () => void;
  selection: () => SelectionItem[];
  viewOnly?: () => boolean;
  /// A host connection: not held to the browser allowlist
  host?: () => boolean;
}) {
  const [text, setText] = createSignal('/');
  const input = createMemo(() => parseInput(text()));
  const [hi, setHi] = createSignal(-1);
  const [panel, setPanel] = createSignal<Panel | null>(null);
  const [status, setStatus] = createSignal<{ text: string; error?: boolean } | null>(null);
  /// The volatile detail of rows seen so far (omni.rows), by key
  const [detail, setDetail] = createSignal<Record<string, any>>({});
  let inputEl: HTMLInputElement | undefined;
  let rootEl: HTMLDivElement | undefined;

  const fail = (e: any, what: string) =>
    setStatus({ text: `${what}: ${e?.code ?? e?.message ?? e}`, error: true });
  /// The objects and their properties, for "Box.Length" (pathcomplete.ts)
  const paths = createPathCompleter(fail);

  const close = () => {
    setPanel(null);
    props.onClose();
  };

  // Opening resets everything but the catalogs, which are asked for
  // their delta (a hundred bytes each when nothing changed) -- the
  // rows are already there from the last time.
  createEffect(on(props.open, (open) => {
    if (!open) return;
    setText('/');
    setHi(-1);
    setPanel(null);
    setStatus(null);
    paths.reset();
    void commands.sync();
    void params.sync();
    requestAnimationFrame(() => inputEl?.focus());
  }));

  // A press outside closes the box, as the desktop's outside click
  // does (with a popup, the desktop closes the popup first; here the
  // list is part of the box).
  const onDown = (e: PointerEvent) => {
    if (props.open() && rootEl && !rootEl.contains(e.target as Node)) close();
  };
  document.addEventListener('pointerdown', onDown, true);
  onCleanup(() => document.removeEventListener('pointerdown', onDown, true));

  // Object mode needs the document's objects; once per opening
  createEffect(() => {
    if (props.open() && input().mode === 'object') paths.ensureObjects();
  });

  const objectRows = (query: string) => paths.rows(query, props.selection(), LIMIT);

  const listed = createMemo<{ rows: Row[]; total: number }>(() => {
    const inp = input();
    switch (inp.mode) {
      case 'chooser': {
        const rows = MODE_ROWS.filter((r) => r.title.startsWith('/' + inp.query));
        return { rows, total: rows.length };
      }
      case 'command': {
        commands.generation();
        const r = commands.search(inp.query, LIMIT);
        return {
          total: r.total,
          rows: r.rows.map((c) => ({
            key: c.name, kind: 'command' as const, title: c.title,
            desc: `${c.shortcut ? c.shortcut + '   ' : ''}${c.desc || c.name}`,
            group: !!c.group,
          })),
        };
      }
      case 'param': {
        params.generation();
        const r = params.search(inp.query, LIMIT);
        return {
          total: r.total,
          rows: r.rows.map((p) => ({
            key: p.key, kind: 'param' as const, title: p.path,
            desc: p.title + (p.doc && p.doc !== p.title ? ' -- ' + p.doc : ''),
            param: p,
          })),
        };
      }
      case 'object':
        return objectRows(inp.query);
    }
  });

  // The highlight follows the list: a row that vanished is not current
  createEffect(() => {
    const n = listed().rows.length;
    if (hi() >= n) setHi(n ? n - 1 : -1);
  });

  // The rows' volatile detail, once the list has stood still. Keys
  // already answered are asked again -- a value may have changed --
  // but the ask is one small op per settle, never one per key.
  let settleTimer = 0;
  let detailSeq = 0;
  createEffect(() => {
    const inp = input();
    const rows = listed().rows;
    clearTimeout(settleTimer);
    if ((inp.mode !== 'command' && inp.mode !== 'param') || rows.length === 0) return;
    const list = inp.mode === 'command' ? 'commands' : 'params';
    const keys = rows.map((r) => r.key);
    const seq = ++detailSeq;
    settleTimer = window.setTimeout(() => {
      omniRows(list, keys)
        .then((r) => {
          if (seq !== detailSeq) return;   // a later list superseded this one
          setDetail((prev) => ({ ...prev, ...r.rows }));
        })
        .catch(() => { /* offline: the rows show without detail */ });
    }, SETTLE_MS);
  });
  onCleanup(() => clearTimeout(settleTimer));

  const inactive = (row: Row) =>
    row.kind === 'command' && detail()[row.key]?.active === false;
  /// A command the server will not run for this connection: anything off
  /// the browser allowlist unless it is a host (docs/ShareAccess.md sec
  /// 2.2). A group row is not judged here: what it runs is only known once
  /// its children are fetched, and each child is judged by its `command`.
  const refused = (row: Row) =>
    row.kind === 'command' && !row.group && !props.host?.() && !isBrowserSafeCommand(row.key);
  /// The same for one row of a group's menu, by the member it runs. An
  /// unnamed row (a recent file, say) is refused too, as the server does.
  const refusedChild = (item: CommandChild) =>
    !props.host?.() && !isBrowserSafeCommand(item.command ?? '');

  // ---- actions

  const openParam = (row: Indexed<ParamRow>) => {
    const d = detail()[row.key];
    setPanel({ kind: 'param', row, value: d?.value ?? '', set: !!d?.set });
    setStatus(null);
    if (!d) {
      omniRows('params', [row.key])
        .then((r) => {
          const v = r.rows[row.key];
          if (!v) return;
          setDetail((prev) => ({ ...prev, ...r.rows }));
          setPanel((p) => p?.kind === 'param' && p.row.key === row.key
            ? { ...p, value: v.value ?? '', set: !!v.set } : p);
        })
        .catch((e) => fail(e, 'parameter'));
    }
  };

  const openProperty = (target: PropTarget, prop: PropDescriptor) => {
    setPanel({ kind: 'property', target, prop });
    setStatus(null);
  };

  const run = (name: string, child?: number) => {
    setStatus({ text: `Running ${name}...` });
    runCommand(name, child)
      .then(() => close())
      .catch((e) => fail(e, name));
  };

  const openChildren = (row: Row) => {
    commandChildren(row.key)
      .then((r) => setPanel({ kind: 'children', name: row.key, title: row.title,
                              exclusive: r.exclusive, items: r.items }))
      .catch((e) => fail(e, row.key));
  };

  /// Show what a resolve answered: a property's editor, or an object card
  const showResolved = (r: ResolveReply, objs?: string[]) => {
    if (r.kind === 'property' && r.prop && r.scope) {
      const title = r.scope === 'document' ? `${r.doc}#.${r.prop.name}`
        : r.scope === 'view3d' ? `#.${r.view ?? 'ActiveView'}.${r.prop.name}`
        : `${r.obj}.${r.scope === 'view' ? 'ViewObject.' : ''}${r.prop.name}`;
      openProperty({ doc: r.doc, obj: r.obj, scope: r.scope, view: r.view, objs,
                     title: objs && objs.length > 1
                       ? `${objs.length} objects . ${r.prop.name}` : title }, r.prop);
      return;
    }
    const path = r.top && r.sub ? `${r.top}.${r.sub}` : r.obj;
    setPanel({ kind: 'object', doc: r.doc, obj: r.obj, label: r.label ?? r.obj,
               type: r.type ?? '', path, props: null });
    getProperties(r.doc, r.obj, 'object')
      .then((pr) => setPanel((p) => p?.kind === 'object' && p.obj === r.obj
        ? { ...p, props: pr.props } : p))
      .catch((e) => fail(e, r.obj));
  };

  /// Enter in object mode: the text as typed, in the box's grammar
  const resolveTyped = () => {
    let q = input().query.trim();
    if (!q) return;
    let objs: string[] | undefined;
    if (q.startsWith('.')) {
      // The selection: resolve on the first, edit them all
      const sel = props.selection().filter((s) => s.obj);
      if (!sel.length) { setStatus({ text: 'Nothing selected', error: true }); return; }
      objs = sel.map((s) => s.obj!);
      q = sel[0].obj! + q;
    }
    setStatus({ text: 'Resolving...' });
    omniResolve(q)
      .then((r) => { setStatus(null); showResolved(r, objs); })
      .catch((e) => setStatus({ text: e?.code === 'NoMatch' ? `Nothing named ${q}`
                                : `${e?.code ?? 'error'}`, error: true }));
  };

  const complete = (row: Row) => {
    if (row.complete === undefined) return;
    const inp = input();
    const next = row.kind === 'mode' ? row.complete : text().slice(0, inp.offset) + row.complete;
    setText(next);
    setHi(-1);
    requestAnimationFrame(() => {
      inputEl?.focus();
      inputEl?.setSelectionRange(next.length, next.length);
    });
  };

  /// Tab or a click: the row decides
  const pick = (row: Row, click: boolean) => {
    switch (row.kind) {
      case 'mode':
        complete(row);
        break;
      case 'command':
        if (refused(row)) {
          setStatus({ text: `${row.title} runs only for the desktop's owner`, error: true });
          break;
        }
        if (inactive(row)) { setStatus({ text: `${row.title} is not active`, error: true }); break; }
        run(row.key);
        break;
      case 'param':
        openParam(row.param!);
        break;
      case 'property':
        complete(row);
        openProperty(row.prop!.target, row.prop!.prop);
        break;
      case 'object':
        complete(row);
        if (click) resolveTyped();
        break;
      case 'member':
        complete(row);
        break;
    }
  };

  const current = (): Row | null => {
    const rows = listed().rows;
    if (!rows.length) return null;
    return rows[hi() >= 0 ? hi() : 0];
  };

  const onKey = (e: KeyboardEvent) => {
    const rows = listed().rows;
    const to = moveInList(e, rows.length, hi());
    if (to !== null) { e.preventDefault(); setHi(to); return; }
    switch (e.key) {
      case 'Escape':
        e.preventDefault();
        if (panel()) setPanel(null);
        else close();
        break;
      case 'Tab': {
        e.preventDefault();
        if (e.shiftKey) { if (rows.length) setHi(Math.max(hi() - 1, 0)); break; }
        const row = current();
        if (row) pick(row, false);
        break;
      }
      case 'ArrowRight': {
        const row = current();
        if (row?.kind === 'command' && row.group && hi() >= 0) {
          e.preventDefault();
          openChildren(row);
        }
        break;
      }
      case 'Enter': {
        e.preventDefault();
        const inp = input();
        if (inp.mode === 'object') { resolveTyped(); break; }
        const row = current();
        if (row) pick(row, false);
        break;
      }
    }
  };

  // ---- panels

  const commitProperty = (target: PropTarget, p: PropDescriptor, value: unknown) => {
    if (props.viewOnly?.()) { setStatus({ text: 'View only', error: true }); return; }
    const objs = target.objs ?? [target.obj];
    setStatus({ text: 'Applying...' });
    Promise.all(objs.map((obj) =>
      setProperty(target.doc, obj, target.scope, p.name, value, target.view)
        .then(() => null).catch((e) => e?.code ?? 'error')))
      .then((results) => {
        const failed = results.filter((r) => r);
        setStatus(failed.length ? { text: `Failed: ${failed[0]}`, error: true } : null);
        // Re-read: an edit can cascade, and only fresh descriptors show the truth
        const fresh = target.scope === 'object' || target.scope === 'view'
          ? getProperties(target.doc, target.obj, 'object')
          : getContainerProperties(target.scope, target.doc, target.view);
        return fresh.then((r) => {
          const np = r.props.find((d) => d.name === p.name && d.scope === p.scope);
          if (np) setPanel((cur) => cur?.kind === 'property' ? { ...cur, prop: np } : cur);
        });
      })
      .catch((e) => fail(e, p.name));
  };

  const commitParam = (row: Indexed<ParamRow>, value: string) => {
    if (props.viewOnly?.()) { setStatus({ text: 'View only', error: true }); return; }
    paramSet(row.key, value)
      .then((s) => {
        setDetail((prev) => ({ ...prev, [row.key]: { value: s.value, set: s.set } }));
        setPanel((p) => p?.kind === 'param' && p.row.key === row.key
          ? { ...p, value: s.value, set: s.set } : p);
        setStatus(null);
      })
      .catch((e) => fail(e, row.entry));
  };

  const resetParam = (row: Indexed<ParamRow>) => {
    if (props.viewOnly?.()) { setStatus({ text: 'View only', error: true }); return; }
    paramReset(row.key)
      .then((s) => {
        setDetail((prev) => ({ ...prev, [row.key]: { value: s.value, set: s.set } }));
        setPanel((p) => p?.kind === 'param' && p.row.key === row.key
          ? { ...p, value: s.value, set: s.set } : p);
        setStatus(null);
      })
      .catch((e) => fail(e, row.entry));
  };

  const propertyRow = (target: PropTarget, p: PropDescriptor) => (
    <div class="fc-omni-panel-row">
      <span class="fc-name" classList={{ 'fc-ro': p.readonly }}>{p.name}</span>
      {p.readonly || props.viewOnly?.() ? fmtValue(p)
        : editValue(p, (d, v) => commitProperty(target, d, v))}
    </div>
  );

  const renderPanel = (pn: Panel): JSX.Element => {
    switch (pn.kind) {
      case 'param': {
        const value = () => (panel() as any)?.value ?? pn.value;
        return (
          <>
            <div class="fc-omni-panel-head">
              <span class="fc-omni-panel-title" title={pn.row.key}>{pn.row.path}</span>
              <span class="fc-omni-panel-sub">default {pn.row.default}</span>
            </div>
            <Show when={pn.row.title || pn.row.doc}>
              <div class="fc-omni-panel-doc">
                {pn.row.title}{pn.row.doc && pn.row.doc !== pn.row.title ? '\n' + pn.row.doc : ''}
              </div>
            </Show>
            <div class="fc-omni-panel-row">
              <span class="fc-name">{pn.row.entry}</span>
              {paramEditor(pn.row, value, !!props.viewOnly?.(), (t) => commitParam(pn.row, t))}
              <button class="fc-omni-btn" disabled={!(panel() as any)?.set || !!props.viewOnly?.()}
                      title="Remove the stored value so the default applies"
                      onClick={() => resetParam(pn.row)}>Reset</button>
            </div>
          </>
        );
      }
      case 'property':
        return (
          <>
            <div class="fc-omni-panel-head">
              <span class="fc-omni-panel-title">{pn.target.title}</span>
              <span class="fc-omni-panel-sub">{pn.prop.group}</span>
            </div>
            <Show when={pn.prop.doc}>
              <div class="fc-omni-panel-doc">{pn.prop.doc}</div>
            </Show>
            {propertyRow(pn.target, (panel() as any)?.prop ?? pn.prop)}
          </>
        );
      case 'object':
        return (
          <>
            <div class="fc-omni-panel-head">
              <span class="fc-omni-panel-title">{pn.label}</span>
              <span class="fc-omni-panel-sub">{pn.path}  {pn.type}</span>
            </div>
            <Show when={!pn.props}><div class="fc-note">Loading...</div></Show>
            <For each={(pn.props ?? []).filter((p) => !p.hidden)}>
              {(p) => propertyRow({ doc: pn.doc, obj: pn.obj, scope: p.scope, title: pn.path }, p)}
            </For>
          </>
        );
      case 'children':
        return (
          <>
            <div class="fc-omni-panel-head">
              <span class="fc-omni-panel-title">{pn.title}</span>
            </div>
            <For each={pn.items.filter((i) => i.visible !== false)}>
              {(item) => item.separator ? <div class="fc-omni-sep" /> : (
                <button class="fc-omni-menu-item"
                        disabled={item.enabled === false || refusedChild(item)}
                        title={refusedChild(item) ? 'Runs only for the desktop\'s owner'
                                                  : (item.tooltip ?? '')}
                        onClick={() => run(pn.name, item.index)}>
                  <span class="fc-menu-tick">
                    {item.checkable ? (item.checked ? (pn.exclusive ? '\u25CF' : '\u2713') : '') : ''}
                  </span>
                  {item.text}
                </button>
              )}
            </For>
          </>
        );
    }
  };

  const note = (): string | null => {
    const inp = input();
    const l = listed();
    if (inp.mode === 'object') {
      if (!paths.objects()) return 'Loading objects...';
      if (inp.query.startsWith('.') && !props.selection().some((s) => s.obj))
        return 'Nothing selected';
      if (!l.rows.length && inp.query) return 'No match';
      return null;
    }
    if ((inp.mode === 'command' && commands.size === 0)
        || (inp.mode === 'param' && params.size === 0))
      return 'Loading...';
    if (!l.rows.length && inp.query) return 'No match';
    if (l.total > l.rows.length) return `${l.total - l.rows.length} more; keep typing`;
    return null;
  };

  return (
    <Show when={props.open()}>
      <div class="fc-omni" role="dialog" aria-label="Search" ref={rootEl}>
        <div class="fc-omni-line">
          <input
            ref={inputEl}
            class="fc-omni-input"
            type="text"
            autocomplete="off"
            autocapitalize="off"
            spellcheck={false}
            value={text()}
            placeholder="/ object, /cmd command"
            onInput={(e) => { setText(e.currentTarget.value); setHi(-1); setStatus(null); }}
            onKeyDown={onKey}
          />
          <button class="fc-close" onClick={close} aria-label="Close">x</button>
        </div>
        <PickList
          rows={() => listed().rows.map((r) => ({ ...r, inactive: inactive(r) || refused(r) }))}
          hi={hi}
          onHi={setHi}
          onPick={(row) => pick(row, true)}
          limit={LIMIT}
          note={note}
          class="fc-omni-list"
          right={(row) => (
            <>
              <Show when={row.kind === 'param'}>
                <span class="fc-omni-right">{detail()[row.key]?.value ?? ''}</span>
              </Show>
              <Show when={row.kind === 'command' && row.group}>
                <button class="fc-omni-arrow" title="Show the group's menu"
                        onClick={(e) => { e.stopPropagation(); openChildren(row); }}>
                  {'\u25B8'}
                </button>
              </Show>
            </>
          )}
        />
        <Show when={panel()}>
          {(pn) => <div class="fc-omni-panel">{renderPanel(pn())}</div>}
        </Show>
        <Show when={status()}>
          <div class="fc-omni-status" classList={{ 'fc-err': !!status()!.error }}>
            {status()!.text}
          </div>
        </Show>
      </div>
    </Show>
  );
}
