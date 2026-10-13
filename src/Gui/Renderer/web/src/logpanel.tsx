// The transaction log's panel (docs/TransactionLog.md sec 32.21): the rows
// of the served document's log, the ops of the one chosen, a value of the
// one chosen of those -- and a change shown as the edit it is.
//
// The page's own, and not the desktop's dock mirrored. That dock is one
// widget of the host's window: mirrored, every client and the desktop would
// share one selection and one tab, its menus -- where most of what it does
// is -- are not mirrored, and its graph column is painted. So the page asks
// for what it lists (log.rows, log.ops, log.value; Gui/SceneLog.cpp) and
// draws it as any other card.
//
// Authority: the host's. The list is what the host answered, a `log.changed`
// push is the cue to ask again and carries nothing of the log, and a row's
// change is shown by asking the host for the edit (ViewProvider::ValueDiff),
// whose panel is the host's own, mirrored.
//
// Seeing the log needs the right to edit (ruled 2026-10-13): a viewer is
// answered ViewOnly, which the card says in words.

import { createEffect, createSignal, For, onCleanup, Show } from 'solid-js';
import type { Accessor } from 'solid-js';

import {
  logOps, logRows, logValue, onConnection, onLogChanged, showRowChange,
  type LogOp, type LogOpsReply, type LogRow, type LogValueReply,
} from './control';
import { draggable, fitOnScreen, loadPos, posStyle, type Pos } from './panel';
import { logAuthor, logOpText, logRefusal, logWhen, mergeNewest } from './logmodel.ts';

const POS_KEY = 'fcviewer.log.pos';
const RECORDS_KEY = 'fcviewer.log.records';

export interface LogPanelProps {
  open: Accessor<boolean>;
  onClose: () => void;
  viewOnly: Accessor<boolean>;
  /// The document the viewer is joined to; empty means the backend's own.
  doc: Accessor<string>;
}

export function LogPanel(props: LogPanelProps) {
  const [rows, setRows] = createSignal<LogRow[]>([]);
  const [more, setMore] = createSignal(false);
  const [branch, setBranch] = createSignal('');
  const [error, setError] = createSignal('');
  const [selected, setSelected] = createSignal<number | null>(null);
  const [ops, setOps] = createSignal<LogOpsReply | null>(null);
  const [opIdx, setOpIdx] = createSignal<number | null>(null);
  const [side, setSide] = createSignal<'before' | 'after'>('after');
  const [value, setValue] = createSignal<LogValueReply | null>(null);
  const [valueError, setValueError] = createSignal('');
  const [note, setNote] = createSignal('');
  const [pos, setPos] = createSignal<Pos | null>(loadPos(POS_KEY));
  const [records, setRecords] = createSignal((() => {
    try { return localStorage.getItem(RECORDS_KEY) === '1'; } catch { return false; }
  })());

  let panelRef: HTMLDivElement | undefined;
  let headerRef: HTMLDivElement | undefined;
  /// What the list was last answered for: the document's place in its log.
  let at = { head: 0, branch: 0, last: 0 };
  let asking = false;
  let again = false;

  const shown = () => (records() ? rows() : rows().filter((r) => r.ops > 0));

  /// The newest page, again: on opening, on the host's cue, and when the
  /// socket is back.
  const refresh = async () => {
    if (asking) { again = true; return; }
    asking = true;
    try {
      const r = await logRows(props.doc());
      // The rows listed before stay what they were while the document is
      // on the same branch and none of them went: an undo, a commit and a
      // recompute all write a row after the others. On another branch, or
      // after a trim, which of the old ones are its history is not known.
      const keep = at.last > 0 && r.branch === at.branch && r.last >= at.last;
      const merged = mergeNewest(rows(), r.rows, keep);
      // (older ones than the page are still listed: whether there are
      // more is what was known of those)
      const kept = merged.length > r.rows.length;
      setRows(merged);
      setMore(kept ? more() : r.more);
      at = { head: r.head, branch: r.branch, last: r.last };
      setBranch(r.branchName);
      setError('');
      const sel = selected();
      if (sel !== null && !rows().some((row) => row.seq === sel)) choose(null);
    } catch (e: any) {
      if (e?.code === 'Offline' || e?.code === 'Timeout') {
        setError('waiting for the viewer...');
      } else {
        setError(logRefusal(e));
        setRows([]);
      }
    } finally {
      asking = false;
      if (again) { again = false; void refresh(); }
    }
  };

  const loadOlder = async () => {
    const list = rows();
    if (!list.length) return;
    try {
      const r = await logRows(props.doc(), { before: list[list.length - 1].seq });
      setRows([...rows(), ...r.rows]);
      setMore(r.more);
    } catch (e: any) {
      setError(logRefusal(e));
    }
  };

  const choose = (seq: number | null) => {
    setSelected(seq);
    setOps(null);
    setOpIdx(null);
    setValue(null);
    setValueError('');
    setNote('');
    if (seq === null) return;
    logOps(seq, props.doc())
      .then((r) => { if (selected() === seq) setOps(r); })
      .catch((e) => { if (selected() === seq) setNote(logRefusal(e)); });
  };

  const chosenOp = () => ops()?.ops.find((o) => o.idx === opIdx()) ?? null;

  const readValue = (op: LogOp | null, which: 'before' | 'after') => {
    setValue(null);
    setValueError('');
    const ref = op?.[which];
    if (!op) return;
    if (!ref) {
      setValueError(which === 'before' ? 'nothing before: made by this row'
                                       : 'no value after');
      return;
    }
    logValue(ref, props.doc())
      .then((r) => { if (chosenOp() === op && side() === which) setValue(r); })
      .catch((e) => {
        if (chosenOp() === op && side() === which)
          setValueError(e?.code === 'UnknownValue' ? 'the log does not hold this value any more'
                                                   : logRefusal(e));
      });
  };

  const chooseOp = (op: LogOp) => {
    setOpIdx(op.idx);
    const which = op.after ? 'after' : 'before';
    setSide(which);
    readValue(op, which);
  };

  const show = (op: LogOp) => {
    const seq = selected();
    if (seq === null || !op.obj) return;
    setNote('');
    showRowChange(op.obj, seq, props.doc())
      .catch((e) => setNote(e?.code === 'ViewOnly' ? logRefusal(e)
                                                   : (e?.message ?? e?.code ?? 'not shown')));
  };

  createEffect(() => {
    if (!props.open()) return;
    props.doc();
    at = { head: 0, branch: 0, last: 0 };
    setRows([]);
    choose(null);
    let retry = 0;
    const ask = () => {
      clearTimeout(retry);
      void refresh().then(() => {
        if (error() === 'waiting for the viewer...' && props.open())
          retry = window.setTimeout(ask, 2000);
      });
    };
    onCleanup(() => clearTimeout(retry));
    onCleanup(onConnection((up) => { if (up) ask(); }));
    onCleanup(onLogChanged((msg) => {
      if (!props.doc() || !msg.doc || msg.doc === props.doc()) void refresh();
    }));
    ask();
  });

  createEffect(() => {
    if (props.open() && panelRef && headerRef) {
      draggable(headerRef, () => panelRef!, setPos, POS_KEY);
      fitOnScreen(panelRef, pos, setPos);
    }
  });

  const toggleRecords = () => {
    const on = !records();
    setRecords(on);
    try { localStorage.setItem(RECORDS_KEY, on ? '1' : '0'); } catch { /* this session */ }
  };

  return (
    <Show when={props.open()}>
      <div class="fc-log" ref={panelRef} style={posStyle(pos())}>
        <div class="fc-log-head" ref={headerRef}>
          <span class="fc-log-title">History</span>
          <Show when={branch()}>
            <span class="fc-log-branch" title="the branch the document is on">{branch()}</span>
          </Show>
          <span class="fc-log-spacer" />
          <label class="fc-log-records" title="rows that changed nothing: recomputes, logins">
            <input type="checkbox" checked={records()} onChange={toggleRecords} />
            Records
          </label>
          <button class="fc-log-close" onClick={props.onClose} title="Close">&times;</button>
        </div>

        <Show when={error()}>
          <div class="fc-log-error">{error()}</div>
        </Show>

        <div class="fc-log-rows">
          <table>
            <thead>
              <tr><th class="fc-log-num">#</th><th>What</th><th>Who</th><th>When</th></tr>
            </thead>
            <tbody>
              <For each={shown()}>
                {(row) => (
                  <tr classList={{
                        'fc-log-sel': selected() === row.seq,
                        'fc-log-record': row.ops === 0,
                        'fc-log-away': !row.here,
                      }}
                      data-seq={row.seq}
                      title={row.here ? undefined : `on branch ${row.branchName}`}
                      onClick={() => choose(row.seq)}>
                    <td class="fc-log-num">{row.seq}</td>
                    <td>
                      {row.name || row.kind}
                      <Show when={row.name && row.kind !== 'user'}>
                        <span class="fc-log-kind">{row.kind}</span>
                      </Show>
                      <Show when={!row.here}>
                        <span class="fc-log-kind">{row.branchName}</span>
                      </Show>
                    </td>
                    <td>{logAuthor(row)}</td>
                    <td>{logWhen(row.time)}</td>
                  </tr>
                )}
              </For>
            </tbody>
          </table>
          <Show when={!shown().length && !error()}>
            <div class="fc-log-empty">No rows.</div>
          </Show>
          <Show when={more()}>
            <button class="fc-log-more" onClick={() => void loadOlder()}>Load older</button>
          </Show>
        </div>

        <Show when={selected() !== null}>
          <div class="fc-log-ops">
            <Show when={note()}>
              <div class="fc-log-error">{note()}</div>
            </Show>
            <Show when={ops()} fallback={<div class="fc-log-empty">...</div>}>
              <Show when={ops()!.ops.length}
                    fallback={<pre class="fc-log-value">{ops()!.script || 'changed nothing'}</pre>}>
                <For each={ops()!.ops}>
                  {(op) => (
                    <div class="fc-log-op"
                         classList={{ 'fc-log-sel': opIdx() === op.idx,
                                      'fc-log-away': op.kind !== 'doc' && op.here === false }}
                         data-idx={op.idx}
                         onClick={() => chooseOp(op)}>
                      <span class="fc-log-optext">{logOpText(op)}</span>
                      <Show when={op.derived}><span class="fc-log-kind">derived</span></Show>
                      <Show when={op.kind === 'view'}><span class="fc-log-kind">view</span></Show>
                      <Show when={op.shows}>
                        <button class="fc-log-show" disabled={props.viewOnly()}
                                title="show the shape before and after this row"
                                onClick={(e) => { e.stopPropagation(); show(op); }}>
                          Show
                        </button>
                      </Show>
                    </div>
                  )}
                </For>
                <Show when={ops()!.count > ops()!.ops.length}>
                  <div class="fc-log-empty">
                    {ops()!.count - ops()!.ops.length} more not listed
                  </div>
                </Show>
              </Show>
            </Show>
          </div>
        </Show>

        <Show when={chosenOp()}>
          <div class="fc-log-valuehead">
            <For each={['before', 'after'] as const}>
              {(which) => (
                <button class="fc-log-side" classList={{ 'fc-log-sel': side() === which }}
                        onClick={() => { setSide(which); readValue(chosenOp(), which); }}>
                  {which === 'before' ? 'Before' : 'After'}
                </button>
              )}
            </For>
            <span class="fc-log-spacer" />
            <Show when={value()}>
              <span class="fc-log-kind">
                {value()!.bytes} bytes{value()!.truncated ? ', the start of it' : ''}
                {value()!.attachments.length
                  ? `, ${value()!.attachments.length} attached` : ''}
              </span>
            </Show>
          </div>
          <Show when={valueError()}>
            <div class="fc-log-empty">{valueError()}</div>
          </Show>
          <Show when={value()}>
            <pre class="fc-log-value">{value()!.text}</pre>
          </Show>
        </Show>
      </div>
    </Show>
  );
}
