// The transaction log's panel (docs/TransactionLog.md sec 32.21, 32.22):
// the rows of the served document's log, the ops of the one chosen, a value
// of the one chosen of those -- a change shown as the edit it is -- and
// what is done with them: rows taken back, versions, branches.
//
// The page's own, and not the desktop's dock mirrored. That dock is one
// widget of the host's window: mirrored, every client and the desktop would
// share one selection and one tab, its menus -- where most of what it does
// is -- are not mirrored, and its graph column is painted. So the page asks
// for what it lists (log.rows, log.ops, log.value, log.versions,
// log.branches; Gui/SceneLog.cpp) and draws it as any other card.
//
// Authority: the host's. The lists are what the host answered, a
// `log.changed` push is the cue to ask again and carries nothing of the
// log, a row's change is shown by asking the host for the edit
// (ViewProvider::ValueDiff), whose panel is the host's own, mirrored -- and
// every act is asked of the host, which does it or says why not. Nothing is
// worked out here of what an act would leave.
//
// Seeing the log needs the right to edit (ruled 2026-10-13): a viewer is
// answered ViewOnly, which the card says in words.

import { createEffect, createSignal, For, onCleanup, Show } from 'solid-js';
import type { Accessor } from 'solid-js';

import {
  logBranches, logDeleteBranch, logNameVersion, logNewBranch, logOps, logRenameBranch,
  logRestore, logRows, logSnapshot, logSwitchBranch, logUndoRows, logValue, logVersions,
  onConnection, onLogChanged, showRowChange,
  type LogBranch, type LogOp, type LogOpsReply, type LogRow, type LogValueReply,
  type LogVersion,
} from './control';
import { draggable, fitOnScreen, loadPos, posStyle, type Pos } from './panel';
import {
  actRefusal, branchNameProblem, logAuthor, logOpText, logRefusal, logWhen, mergeNewest,
  pickRows, rowUndoable, undoLabel, versionText,
} from './logmodel.ts';

const POS_KEY = 'fcviewer.log.pos';
const RECORDS_KEY = 'fcviewer.log.records';

export interface LogPanelProps {
  open: Accessor<boolean>;
  onClose: () => void;
  viewOnly: Accessor<boolean>;
  /// The document the viewer is joined to; empty means the backend's own.
  doc: Accessor<string>;
}

/// A question the card asks before an act: a name to give, or a yes to
/// what nothing takes back. One at a time, in the card itself -- a page on
/// a phone has no dialog of its own worth the name.
interface Ask {
  label: string;
  /// The text to start from; absent, no text is asked for
  text?: string;
  yes: string;
  /// Why the text is not one to go on with; empty when it is
  problem?: (text: string) => string;
  /// The act: what it says when done
  run: (text: string) => Promise<string>;
}

export function LogPanel(props: LogPanelProps) {
  const [rows, setRows] = createSignal<LogRow[]>([]);
  const [more, setMore] = createSignal(false);
  const [error, setError] = createSignal('');
  const [picked, setPicked] = createSignal<number[]>([]);
  const [several, setSeveral] = createSignal(false);
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
  const [tab, setTab] = createSignal<'rows' | 'versions'>('rows');
  const [branches, setBranches] = createSignal<LogBranch[]>([]);
  const [mayDelete, setMayDelete] = createSignal(false);
  const [versions, setVersions] = createSignal<LogVersion[]>([]);
  const [version, setVersion] = createSignal<number | null>(null);
  const [ask, setAsk] = createSignal<Ask | null>(null);
  const [askText, setAskText] = createSignal('');
  const [status, setStatus] = createSignal<{ text: string; bad: boolean } | null>(null);
  const [busy, setBusy] = createSignal(false);

  let panelRef: HTMLDivElement | undefined;
  let headerRef: HTMLDivElement | undefined;
  /// What the list was last answered for: the document's place in its log.
  let at = { head: 0, branch: 0, last: 0 };
  let asking = false;
  let again = false;
  /// The row the rows between are counted from, with shift held
  let anchor: number | null = null;

  const shown = () => (records() ? rows() : rows().filter((r) => r.ops > 0));
  /// The one row chosen, whose ops are listed; none where several are
  const selected = () => (picked().length === 1 ? picked()[0] : null);
  const current = () => branches().find((b) => b.current) ?? null;
  const pickedRows = () => rows().filter((r) => picked().includes(r.seq));
  const chosenVersion = () => versions().find((v) => v.num === version()) ?? null;

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
      setError('');
      const left = picked().filter((seq) => merged.some((row) => row.seq === seq));
      if (left.length !== picked().length) choose(left);
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

  /// The branches, and the versions where they are looked at. What is not
  /// answered leaves what was listed: the rows' own refusal is what says
  /// that the log cannot be read.
  const refreshLists = async () => {
    try {
      const b = await logBranches(props.doc());
      setBranches(b.branches);
      setMayDelete(b.mayDelete);
    } catch { /* said by the rows */ }
    if (tab() !== 'versions') return;
    try {
      const v = await logVersions(props.doc());
      // Newest first, as the rows are
      setVersions([...v.versions].reverse());
      if (version() !== null && !v.versions.some((x) => x.num === version())) setVersion(null);
    } catch { /* said by the rows */ }
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

  /// The rows chosen are these now: the ops of one alone are listed.
  const choose = (seqs: number[]) => {
    setPicked(seqs);
    setOps(null);
    setOpIdx(null);
    setValue(null);
    setValueError('');
    setNote('');
    if (seqs.length !== 1) return;
    const seq = seqs[0];
    logOps(seq, props.doc())
      .then((r) => { if (selected() === seq) setOps(r); })
      .catch((e) => { if (selected() === seq) setNote(logRefusal(e)); });
  };

  const clickRow = (seq: number, e: MouseEvent) => {
    const next = pickRows(shown().map((r) => r.seq), picked(), anchor, seq,
                          { toggle: several() || e.ctrlKey || e.metaKey, range: e.shiftKey });
    anchor = next.anchor;
    choose(next.picked);
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

  /// An act asked of the host: what it says when done, or why it was not.
  /// The lists are asked again either way -- a refusal is often that the
  /// card showed what is no longer so.
  const act = async (run: () => Promise<string>) => {
    if (busy()) return;
    setBusy(true);
    setStatus(null);
    try {
      setStatus({ text: await run(), bad: false });
    } catch (e: any) {
      setStatus({ text: actRefusal(e), bad: true });
    } finally {
      setBusy(false);
      void refresh();
      void refreshLists();
    }
  };

  const question = (q: Ask) => {
    setAskText(q.text ?? '');
    setAsk(q);
  };
  const askProblem = () => {
    const q = ask();
    return q?.problem && q.text !== undefined ? q.problem(askText()) : '';
  };
  const answer = () => {
    const q = ask();
    if (!q || askProblem()) return;
    const text = askText().trim();
    setAsk(null);
    void act(() => q.run(text));
  };

  const undoPicked = () => {
    const seqs = [...picked()].sort((a, b) => a - b);
    if (!seqs.length) return;
    void act(async () => {
      await logUndoRows(seqs, props.doc());
      choose([]);
      return seqs.length === 1 ? `Row ${seqs[0]} undone` : `${seqs.length} rows undone`;
    });
  };

  const branchNames = () => branches().map((b) => b.name);
  const newBranch = (from: { version?: number; row?: number }, where: string) => question({
    label: `Name of the branch from ${where}:`,
    text: '',
    yes: 'Branch',
    problem: (text) => branchNameProblem(text, branchNames()),
    run: async (text) => {
      await logNewBranch(text, from, props.doc());
      return `On the new branch ${text}`;
    },
  });
  const renameBranch = () => {
    const b = current();
    if (!b) return;
    question({
      label: `New name of ${b.name}:`,
      text: b.name,
      yes: 'Rename',
      problem: (text) => (text.trim() === b.name
        ? 'that is its name' : branchNameProblem(text, branchNames())),
      run: async (text) => {
        await logRenameBranch(b.name, text, props.doc());
        return `${b.name} is ${text} now`;
      },
    });
  };
  const deleteBranch = (name: string) => question({
    label: `Delete branch ${name} and the history only it holds? This cannot be undone.`,
    yes: 'Delete',
    run: async () => {
      const r = await logDeleteBranch(name, props.doc());
      return `Branch ${name} deleted: ${r.rows} rows gone`;
    },
  });
  const switchTo = (name: string) => void act(async () => {
    await logSwitchBranch(name, props.doc());
    return `On branch ${name}`;
  });
  const snapshot = () => question({
    label: 'A version of the document as it is. A name keeps it:',
    text: '',
    yes: 'Snapshot',
    run: async (text) => {
      const r = await logSnapshot(text, props.doc());
      return text ? `Version ${r.version}: ${text}` : `Version ${r.version}`;
    },
  });
  const restore = (v: LogVersion) => void act(async () => {
    await logRestore(v.num, props.doc());
    return `Restored to ${versionText(v)}`;
  });
  const nameVersion = (v: LogVersion) => question({
    label: `Name of version ${v.num}:`,
    text: v.name,
    yes: 'Name',
    problem: (text) => (text.trim() ? '' : 'a name, or leave it'),
    run: async (text) => {
      await logNameVersion(v.num, text, props.doc());
      return `Version ${v.num}: ${text}`;
    },
  });
  const unnameVersion = (v: LogVersion) => void act(async () => {
    await logNameVersion(v.num, '', props.doc());
    return `Version ${v.num} has no name now`;
  });

  createEffect(() => {
    if (!props.open()) return;
    props.doc();
    at = { head: 0, branch: 0, last: 0 };
    anchor = null;
    setRows([]);
    setBranches([]);
    setVersions([]);
    setVersion(null);
    setAsk(null);
    setStatus(null);
    choose([]);
    let retry = 0;
    const askAll = () => {
      clearTimeout(retry);
      void refreshLists();
      void refresh().then(() => {
        if (error() === 'waiting for the viewer...' && props.open())
          retry = window.setTimeout(askAll, 2000);
      });
    };
    onCleanup(() => clearTimeout(retry));
    onCleanup(onConnection((up) => { if (up) askAll(); }));
    onCleanup(onLogChanged((msg) => {
      if (!props.doc() || !msg.doc || msg.doc === props.doc()) {
        void refresh();
        void refreshLists();
      }
    }));
    askAll();
  });

  createEffect(() => {
    if (props.open() && tab() === 'versions') void refreshLists();
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
          <Show when={branches().length}>
            <select class="fc-log-branchsel"
                    title="the branch the document is on; another switches to it"
                    disabled={busy()}
                    onPointerDown={(e) => e.stopPropagation()}
                    onChange={(e) => {
                      const name = e.currentTarget.value;
                      // The host's answer is what moves it
                      e.currentTarget.value = current()?.name ?? name;
                      if (name !== current()?.name) switchTo(name);
                    }}>
              <For each={branches()}>
                {(b) => (
                  <option value={b.name} selected={b.current} disabled={b.closed && !b.current}>
                    {b.closed ? `${b.name} (closed)` : b.name}
                  </option>
                )}
              </For>
            </select>
          </Show>
          <span class="fc-log-spacer" />
          <label class="fc-log-records" title="rows that changed nothing: recomputes, logins">
            <input type="checkbox" checked={records()} onChange={toggleRecords} />
            Records
          </label>
          <button class="fc-log-close" onClick={props.onClose} title="Close">&times;</button>
        </div>

        <div class="fc-log-acts">
          <For each={['rows', 'versions'] as const}>
            {(which) => (
              <button class="fc-log-act fc-log-tab" classList={{ 'fc-log-sel': tab() === which }}
                      data-tab={which} onClick={() => setTab(which)}>
                {which === 'rows' ? 'Rows' : 'Versions'}
              </button>
            )}
          </For>
          <span class="fc-log-spacer" />
          <button class="fc-log-act" data-act="new-branch" disabled={busy()}
                  title="a new branch from where the document is, switched to"
                  onClick={() => newBranch({}, 'here')}>
            Branch...
          </button>
          <button class="fc-log-act" data-act="rename-branch" disabled={busy() || !current()}
                  title="rename the branch the document is on" onClick={renameBranch}>
            Rename...
          </button>
          <Show when={mayDelete() && branches().length > 1}>
            <select class="fc-log-act" data-act="delete-branch" disabled={busy()}
                    title="delete another branch: the rows only it holds go with it"
                    onChange={(e) => {
                      const name = e.currentTarget.value;
                      e.currentTarget.value = '';
                      if (name) deleteBranch(name);
                    }}>
              <option value="">Delete...</option>
              <For each={branches().filter((b) => !b.current)}>
                {(b) => <option value={b.name}>{b.name}</option>}
              </For>
            </select>
          </Show>
          <button class="fc-log-act" data-act="snapshot" disabled={busy()}
                  title="a version of the document as it stands" onClick={snapshot}>
            Snapshot...
          </button>
        </div>

        <Show when={ask()}>
          <form class="fc-log-ask" onSubmit={(e) => { e.preventDefault(); answer(); }}>
            <span class="fc-log-asklabel">{ask()!.label}</span>
            <Show when={ask()!.text !== undefined}>
              <input class="fc-log-askinput" type="text" value={askText()}
                     ref={(el) => setTimeout(() => el.focus(), 0)}
                     onInput={(e) => setAskText(e.currentTarget.value)}
                     onKeyDown={(e) => { if (e.key === 'Escape') setAsk(null); }} />
            </Show>
            <button class="fc-log-act" type="submit" data-act="yes"
                    disabled={!!askProblem()} title={askProblem() || undefined}>
              {ask()!.yes}
            </button>
            <button class="fc-log-act" type="button" data-act="no" onClick={() => setAsk(null)}>
              Cancel
            </button>
          </form>
        </Show>

        <Show when={status()}>
          <div class="fc-log-status" classList={{ 'fc-log-error': status()!.bad }}
               title="click to dismiss" onClick={() => setStatus(null)}>
            {status()!.text}
          </div>
        </Show>

        <Show when={error()}>
          <div class="fc-log-error">{error()}</div>
        </Show>

        <Show when={tab() === 'rows'}>
          <div class="fc-log-rows">
            <table>
              <thead>
                <tr><th class="fc-log-num">#</th><th>What</th><th>Who</th><th>When</th></tr>
              </thead>
              <tbody>
                <For each={shown()}>
                  {(row) => (
                    <tr classList={{
                          'fc-log-sel': picked().includes(row.seq),
                          'fc-log-record': row.ops === 0,
                          'fc-log-away': !row.here,
                        }}
                        data-seq={row.seq}
                        title={row.here ? undefined : `on branch ${row.branchName}`}
                        onClick={(e) => clickRow(row.seq, e)}>
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

          <Show when={picked().length}>
            <div class="fc-log-rowacts">
              <button class="fc-log-act" data-act="undo"
                      disabled={busy() || !pickedRows().length
                                || !pickedRows().every(rowUndoable)}
                      title={'one new step taking back what the rows chosen did, though none '
                             + 'is the last; refused when anything since changed what they '
                             + 'touched'}
                      onClick={undoPicked}>
                {undoLabel(picked().length, picked()[0])}
              </button>
              <button class="fc-log-act" data-act="branch-here"
                      disabled={busy() || selected() === null}
                      title="a new branch whose history ends at this row, switched to"
                      onClick={() => newBranch({ row: selected()! }, `row ${selected()}`)}>
                Branch from here...
              </button>
              <span class="fc-log-spacer" />
              <label class="fc-log-records" title="a click adds a row to those chosen">
                <input type="checkbox" data-act="several" checked={several()}
                       onChange={() => setSeveral(!several())} />
                Several
              </label>
            </div>
          </Show>

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
        </Show>

        <Show when={tab() === 'versions'}>
          <div class="fc-log-rows fc-log-versions">
            <table>
              <thead>
                <tr>
                  <th class="fc-log-num">#</th><th>Version</th><th>Branch</th>
                  <th class="fc-log-num">Row</th><th>When</th>
                </tr>
              </thead>
              <tbody>
                <For each={versions()}>
                  {(v) => (
                    <tr classList={{
                          'fc-log-sel': version() === v.num,
                          'fc-log-record': !v.named,
                          'fc-log-away': !v.here,
                        }}
                        data-num={v.num}
                        onClick={() => setVersion(v.num)}>
                      <td class="fc-log-num">{v.num}</td>
                      <td>
                        {v.name || 'unnamed'}
                        <Show when={v.head}><span class="fc-log-kind">now</span></Show>
                      </td>
                      <td>{v.branchName}</td>
                      <td class="fc-log-num">{v.seq || ''}</td>
                      <td>{logWhen(v.time)}</td>
                    </tr>
                  )}
                </For>
              </tbody>
            </table>
            <Show when={!versions().length}>
              <div class="fc-log-empty">No versions.</div>
            </Show>
          </div>

          <Show when={chosenVersion()}>
            <div class="fc-log-rowacts">
              <button class="fc-log-act" data-act="restore" disabled={busy()}
                      title={'one step making the document what this version was; undo takes '
                             + 'it back'}
                      onClick={() => restore(chosenVersion()!)}>
                Restore
              </button>
              <button class="fc-log-act" data-act="name-version" disabled={busy()}
                      title="a named version is never evicted"
                      onClick={() => nameVersion(chosenVersion()!)}>
                {chosenVersion()!.named ? 'Rename...' : 'Name...'}
              </button>
              <Show when={chosenVersion()!.named}>
                <button class="fc-log-act" data-act="unname-version" disabled={busy()}
                        onClick={() => unnameVersion(chosenVersion()!)}>
                  Unname
                </button>
              </Show>
              <button class="fc-log-act" data-act="branch-version" disabled={busy()}
                      title="a new branch from this version, switched to"
                      onClick={() => newBranch({ version: chosenVersion()!.num },
                                               `version ${chosenVersion()!.num}`)}>
                Branch from here...
              </button>
            </div>
          </Show>
        </Show>
      </div>
    </Show>
  );
}
