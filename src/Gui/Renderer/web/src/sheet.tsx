// The spreadsheet panel (docs/SpreadsheetRemote.md sec 3): a DOM grid over
// the viewer canvas, fed by sheet.get / sheet.set on the control channel.
//
// DOM rather than the vg page tier, and the reason is worth keeping next to
// the code: a sheet is a text-editing UI, not geometry. Cell editing with a
// soft keyboard and IME, selection, clipboard, sticky headers and
// accessibility are the hard parts, and the browser gives every one of them
// away. Drawing a few hundred visible cells is the cheap part.
//
// Authority: the host is the source of truth. An edit is sent, acknowledged,
// and the values come back through the sheet.changed push -- the ack is never
// treated as the new state, exactly as the inspector never treats a frame as
// its setProperty ack.

import { createEffect, createMemo, createSignal, For, onCleanup, Show } from 'solid-js';
import type { Accessor } from 'solid-js';

import {
  onSheetChanged, sheetGet, sheetList, sheetSet,
  type SheetCell, type SheetData, type SheetEntry,
} from './control';
import { draggable, fitOnScreen, loadPos, posStyle, type Pos } from './panel';

const POS_KEY = 'fcviewer.sheet.pos';

/// Column label the way a spreadsheet numbers them: A..Z, AA..AZ, ...
function columnLabel(index: number): string {
  let label = '';
  let n = index;
  do {
    label = String.fromCharCode(65 + (n % 26)) + label;
    n = Math.floor(n / 26) - 1;
  } while (n >= 0);
  return label;
}

const DEFAULT_COL_W = 100;
const DEFAULT_ROW_H = 22;
/// Always show a margin of empty cells past the used range, so a sheet can
/// be extended without a "make it bigger" affordance -- the same reason a
/// desktop sheet is not exactly its used range.
const SPARE_ROWS = 8;
const SPARE_COLS = 4;

export interface SheetPanelProps {
  open: Accessor<boolean>;
  onClose: () => void;
  viewOnly: Accessor<boolean>;
  /// The document the viewer is joined to; empty means the backend's own.
  doc: Accessor<string>;
}

export function SheetPanel(props: SheetPanelProps) {
  const [sheets, setSheets] = createSignal<SheetEntry[]>([]);
  const [current, setCurrent] = createSignal<string>('');
  const [data, setData] = createSignal<SheetData | null>(null);
  const [error, setError] = createSignal<string>('');
  const [selected, setSelected] = createSignal<string>('A1');
  const [editing, setEditing] = createSignal<string | null>(null);
  const [editText, setEditText] = createSignal('');
  const [pos, setPos] = createSignal<Pos | null>(loadPos(POS_KEY));

  let panelRef: HTMLDivElement | undefined;
  let headerRef: HTMLDivElement | undefined;
  let inputRef: HTMLInputElement | undefined;

  // Address -> cell, so the grid can ask for any coordinate cheaply. Only
  // cells that exist are sent; the rest of the grid is genuinely empty.
  const byAddress = createMemo(() => {
    const map = new Map<string, SheetCell>();
    for (const cell of data()?.cells ?? []) map.set(cell.a, cell);
    return map;
  });

  const rows = createMemo(() => (data()?.rows ?? 0) + SPARE_ROWS);
  const cols = createMemo(() => (data()?.cols ?? 0) + SPARE_COLS);

  const refresh = async (obj: string) => {
    try {
      const d = await sheetGet(obj, props.doc());
      setData(d);
      setError('');
    } catch (e: any) {
      setError(e?.message ?? e?.code ?? 'failed to read the sheet');
    }
  };

  // Discover the sheets once the panel opens. A sheet has no geometry, so
  // it can never be picked in the 3D view -- the list op is the only way in.
  //
  // The panel can open BEFORE the socket exists: ?sheet opens it during page
  // load, and every other card in this chrome is opened by a selection, which
  // by definition cannot arrive until the viewer is up. So an 'Offline'
  // refusal here is "not yet", not "no": keep asking while the panel is open,
  // rather than showing a dead card that a reload is the only way out of.
  createEffect(() => {
    if (!props.open()) return;
    let cancelled = false;
    onCleanup(() => { cancelled = true; });

    const ask = () => {
      if (cancelled || !props.open()) return;
      sheetList(props.doc())
        .then((r) => {
          if (cancelled) return;
          setError('');
          setSheets(r.sheets);
          if (!r.sheets.length) {
            setError('this document has no spreadsheet');
            return;
          }
          if (!current()) {
            setCurrent(r.sheets[0].obj);
            void refresh(r.sheets[0].obj);
          }
        })
        .catch((e) => {
          if (cancelled) return;
          if (e?.code === 'Offline' || e?.code === 'Timeout') {
            setError('waiting for the viewer...');
            setTimeout(ask, 500);
            return;
          }
          setError(e?.message ?? e?.code ?? 'no spreadsheet here');
        });
    };
    ask();
  });

  // The host says a sheet moved on; re-get if it is the one on screen.
  const stop = onSheetChanged((msg) => {
    const d = data();
    if (!d || msg.obj !== d.obj) return;
    if (msg.version <= d.version) return;
    void refresh(d.obj);
  });
  onCleanup(stop);

  createEffect(() => {
    if (props.open() && panelRef && headerRef) {
      draggable(headerRef, () => panelRef!, setPos, POS_KEY);
      fitOnScreen(panelRef, pos, setPos);
    }
  });

  const beginEdit = (address: string) => {
    if (props.viewOnly()) return;
    const cell = byAddress().get(address);
    setEditText(cell?.f ?? '');
    setEditing(address);
    queueMicrotask(() => inputRef?.focus());
  };

  const commit = async () => {
    const address = editing();
    const obj = data()?.obj;
    if (!address || !obj) return;
    const text = editText();
    setEditing(null);
    try {
      await sheetSet(obj, address, text, props.doc());
      // No optimistic write: the values arrive with the push that follows.
      // A cell can change the value of a dozen others, and only the host
      // knows which.
    } catch (e: any) {
      setError(e?.code === 'ViewOnly'
        ? 'the host has disabled editing'
        : (e?.message ?? 'the edit was refused'));
    }
  };

  const onGridKey = (e: KeyboardEvent) => {
    if (editing()) return;
    const d = data();
    if (!d) return;
    const address = selected();
    const cell = byAddress().get(address);
    let r = cell?.r ?? 0;
    let c = cell?.c ?? 0;
    if (!cell) {
      const m = /^([A-Z]+)(\d+)$/.exec(address);
      if (m) {
        c = 0;
        for (const ch of m[1]) c = c * 26 + (ch.charCodeAt(0) - 64);
        c -= 1;
        r = parseInt(m[2], 10) - 1;
      }
    }
    const move = (dr: number, dc: number) => {
      e.preventDefault();
      const nr = Math.max(0, Math.min(rows() - 1, r + dr));
      const nc = Math.max(0, Math.min(cols() - 1, c + dc));
      setSelected(columnLabel(nc) + (nr + 1));
    };
    switch (e.key) {
      case 'ArrowUp': return move(-1, 0);
      case 'ArrowDown': return move(1, 0);
      case 'ArrowLeft': return move(0, -1);
      case 'ArrowRight': return move(0, 1);
      case 'Enter': case 'F2':
        e.preventDefault();
        return beginEdit(address);
      case 'Delete': case 'Backspace': {
        e.preventDefault();
        const obj = d.obj;
        if (!props.viewOnly()) void sheetSet(obj, address, '', props.doc());
        return;
      }
      default:
        // A printable key starts an edit with that character, the way a
        // spreadsheet has always behaved.
        if (e.key.length === 1 && !e.ctrlKey && !e.metaKey && !e.altKey) {
          beginEdit(address);
          setEditText(e.key);
          e.preventDefault();
        }
    }
  };

  const colWidth = (c: number) => data()?.colW?.[String(c)] ?? DEFAULT_COL_W;
  const rowHeight = (r: number) => data()?.rowH?.[String(r)] ?? DEFAULT_ROW_H;

  const cellStyle = (cell: SheetCell | undefined) => {
    const style: Record<string, string> = {};
    if (cell?.fg) style.color = cell.fg;
    if (cell?.bg) style['background-color'] = cell.bg;
    if (cell?.ha) style['text-align'] = cell.ha;
    if (cell?.st?.includes('bold')) style['font-weight'] = '600';
    if (cell?.st?.includes('italic')) style['font-style'] = 'italic';
    if (cell?.st?.includes('underline')) style['text-decoration'] = 'underline';
    return style;
  };

  return (
    <Show when={props.open()}>
      <div class="fc-sheet" ref={panelRef} style={posStyle(pos())}>
        <div class="fc-sheet-head" ref={headerRef}>
          <select
            class="fc-sheet-pick"
            value={current()}
            onChange={(e) => {
              setCurrent(e.currentTarget.value);
              void refresh(e.currentTarget.value);
            }}
          >
            <For each={sheets()}>
              {(s) => <option value={s.obj}>{s.label}</option>}
            </For>
          </select>
          <Show when={props.viewOnly()}>
            <span class="fc-sheet-ro" title="the host has disabled editing">
              view only
            </span>
          </Show>
          <span class="fc-sheet-spacer" />
          <button class="fc-sheet-close" onClick={props.onClose} title="Close">
            &times;
          </button>
        </div>

        <div class="fc-sheet-formula">
          <span class="fc-sheet-addr">{selected()}</span>
          <input
            class="fc-sheet-input"
            readOnly={props.viewOnly()}
            value={editing() ? editText() : (byAddress().get(selected())?.f ?? '')}
            onFocus={() => beginEdit(selected())}
            onInput={(e) => setEditText(e.currentTarget.value)}
            onKeyDown={(e) => {
              if (e.key === 'Enter') { e.preventDefault(); void commit(); }
              else if (e.key === 'Escape') { e.preventDefault(); setEditing(null); }
            }}
          />
        </div>

        <Show when={error()}>
          <div class="fc-sheet-error">{error()}</div>
        </Show>

        <div class="fc-sheet-grid" tabIndex={0} onKeyDown={onGridKey}>
          <table>
            <thead>
              <tr>
                <th class="fc-sheet-corner" />
                <For each={Array.from({ length: cols() }, (_, i) => i)}>
                  {(c) => (
                    <th style={{ width: `${colWidth(c)}px` }}>{columnLabel(c)}</th>
                  )}
                </For>
              </tr>
            </thead>
            <tbody>
              <For each={Array.from({ length: rows() }, (_, i) => i)}>
                {(r) => (
                  <tr style={{ height: `${rowHeight(r)}px` }}>
                    <th class="fc-sheet-rowhead">{r + 1}</th>
                    <For each={Array.from({ length: cols() }, (_, i) => i)}>
                      {(c) => {
                        const address = columnLabel(c) + (r + 1);
                        const cell = () => byAddress().get(address);
                        return (
                          <td
                            classList={{
                              'fc-sheet-sel': selected() === address,
                              'fc-sheet-err': !!cell()?.err,
                            }}
                            style={cellStyle(cell())}
                            title={cell()?.err ?? cell()?.alias ?? undefined}
                            onClick={() => setSelected(address)}
                            onDblClick={() => beginEdit(address)}
                          >
                            <Show
                              when={editing() === address}
                              fallback={cell()?.t ?? ''}
                            >
                              <input
                                ref={inputRef}
                                class="fc-sheet-cellinput"
                                value={editText()}
                                onInput={(e) => setEditText(e.currentTarget.value)}
                                onBlur={() => void commit()}
                                onKeyDown={(e) => {
                                  if (e.key === 'Enter') { e.preventDefault(); void commit(); }
                                  else if (e.key === 'Escape') { e.preventDefault(); setEditing(null); }
                                }}
                              />
                            </Show>
                          </td>
                        );
                      }}
                    </For>
                  </tr>
                )}
              </For>
            </tbody>
          </table>
        </div>
      </div>
    </Show>
  );
}
