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
//
// What the one at the page may (sec 32.25) is the host's to say with every
// list: by branch, read or write. Reading a branch lists it, lets its rows
// be applied here and a merge of it be asked for; writing the one the
// document is on lets the document be moved -- restore, switch, branch --
// from a point on, and writing a branch lets the requests to merge into it
// be dealt with. A client of a shared document reads its branch: it takes
// back its own rows and takes versions. The card offers what would be done
// and says of the rest why not.
//
// The merge's own (sec 32.26): where an apply or a merge meets what was set
// here since, the card asks one side for all of them, or opens the list of
// what it would do -- `log.preview` -- and a side is picked for each
// conflict; a merge undone at the head is rolled back.
//
// What is kept in the file (sec 32.27): a fourth tab lists the values a
// save writes though no kept version reaches them -- each kept by itself,
// each object kept -- and an op's value, an object, or the objects selected
// as they are at a version are kept, or let go, from here.
//
// The host's alone (sec 32.30): a trim and a squash at a version, which take
// rows out for good, and the requests a file sent or an import made -- the
// files brought in or let go unread, the branches merged or deleted. A
// client is listed the files it sent, and withdraws them. Nothing here opens
// a document of its own. A branch the asker does not read is listed greyed.

import { createEffect, createSignal, For, onCleanup, Show } from 'solid-js';
import type { Accessor } from 'solid-js';

import {
  logApply, logBranches, logBring, logCloseRequest, logDeleteBranch, logDropFile, logKeep,
  logKeepObject, logKept, logMergeImport, logMergeRequest, logNameVersion, logNewBranch, logOps,
  logPreview, logRelease, logRenameBranch, logRequest, logRequests, logRestore, logRollBack,
  logRows, logSnapshot, logSquash, logSwitchBranch, logTrim, logUndoRows, logValue, logVersions,
  onConnection, onLogChanged, showKeptValue, showRowChange,
  type LogBranch, type LogChange, type LogImport, type LogOp, type LogOpsReply, type LogReach,
  type LogRequest, type LogRow, type LogSentFile, type LogSides, type LogValueReply,
  type LogVersion,
} from './control';
import { draggable, fitOnScreen, loadPos, posStyle, type Pos } from './panel';
import {
  actRefusal, branchNameProblem, changeLeavable, changeLeftOut, changeName, changePicked,
  changeTakes, deletable, keepText,
  keptCountText, keptLines, keptRowText, keptTabText, logAuthor, logOpText, logRefusal,
  logWhen, mayMove, mergeNewest, moveWhy, opKeepable, opKeptText, picksOf, pickRows,
  keeperText, keepWhy,
  previewText, requestActs, requestProblem, requestState, requestText, rowBranchWhy,
  rowUndoable, rowUndoWhy, rowsAppliable, sortChanges, sortRequests, switchable, undoLabel,
  unpickedConflicts, versionText, versionWhy, type KeptLine,
  branchRead, bringWhy, importText, importWho, sentFileText, squashChoices, trimWhy, UNREAD,
} from './logmodel.ts';

const POS_KEY = 'fcviewer.log.pos';
const RECORDS_KEY = 'fcviewer.log.records';

export interface LogPanelProps {
  open: Accessor<boolean>;
  onClose: () => void;
  viewOnly: Accessor<boolean>;
  /// The document the viewer is joined to; empty means the backend's own.
  doc: Accessor<string>;
  /// The objects selected in the page, by name: what is kept as it is at a
  /// version
  selected?: Accessor<string[]>;
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
  /// A second answer, where there are two ways to go on
  other?: { label: string; run: () => Promise<string> };
  /// A third, which is no act: it opens something more to answer
  more?: { label: string; open: () => void };
  /// The text is one of these, chosen from a list: the value is what `run`
  /// is given
  choices?: Array<{ value: string; label: string }>;
}

type Side = 'ours' | 'theirs';

/// The list of what an apply or a merge would do, a side to pick for each
/// conflict (sec 32.26): open in the card in place of the lists.
interface Picker {
  title: string;
  yes: string;
  line: string;
  changes: LogChange[];
  /// The act, with the sides picked: what it says when done
  run: (sides: LogSides) => Promise<string>;
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
  const [tab, setTab] = createSignal<'rows' | 'versions' | 'requests' | 'kept'>('rows');
  const [kept, setKept] = createSignal<KeptLine[]>([]);
  const [keptCount, setKeptCount] = createSignal(0);
  const [keptKey, setKeptKey] = createSignal<string | null>(null);
  const [keptValue, setKeptValue] = createSignal<LogValueReply | null>(null);
  const [requests, setRequests] = createSignal<LogRequest[]>([]);
  const [request, setRequest] = createSignal<number | null>(null);
  const [files, setFiles] = createSignal<LogSentFile[]>([]);
  const [imports, setImports] = createSignal<LogImport[]>([]);
  /// The file sent, or the import, chosen: `file:<id>`, `import:<branch>`
  const [sent, setSent] = createSignal<string | null>(null);
  const [reqSource, setReqSource] = createSignal('');
  const [reqTarget, setReqTarget] = createSignal('');
  const [reqNote, setReqNote] = createSignal('');
  const [branches, setBranches] = createSignal<LogBranch[]>([]);
  // Until the host says: nothing but reading is offered
  const [reach, setReach] = createSignal<LogReach>({ host: false, write: false });
  const [versions, setVersions] = createSignal<LogVersion[]>([]);
  const [version, setVersion] = createSignal<number | null>(null);
  const [ask, setAsk] = createSignal<Ask | null>(null);
  const [askText, setAskText] = createSignal('');
  const [status, setStatus] = createSignal<{ text: string; bad: boolean } | null>(null);
  const [busy, setBusy] = createSignal(false);
  const [picker, setPicker] = createSignal<Picker | null>(null);
  const [sides, setSides] = createSignal<Record<string, Side>>({});
  /// The merge undone at the head, asked when a merge or an undo is chosen
  const [undone, setUndone] = createSignal<{ source: string; rows: number; may: boolean;
                                             why: string } | null>(null);

  let panelRef: HTMLDivElement | undefined;
  let headerRef: HTMLDivElement | undefined;
  /// What the list was last answered for: the document's place in its log.
  let at = { head: 0, branch: 0, last: 0 };
  let asking = false;
  let again = false;
  /// The row the rows between are counted from, with shift held
  let anchor: number | null = null;
  /// How far back the log was read: the rows of branches that are not read
  /// are left out of a page, so the oldest row listed is not it
  let oldest = 0;

  const shown = () => (records() ? rows() : rows().filter((r) => r.ops > 0));
  /// The one row chosen, whose ops are listed; none where several are
  const selected = () => (picked().length === 1 ? picked()[0] : null);
  const current = () => branches().find((b) => b.current) ?? null;
  const pickedRows = () => rows().filter((r) => picked().includes(r.seq));
  const chosenVersion = () => versions().find((v) => v.num === version()) ?? null;
  const chosenRow = () => rows().find((r) => r.seq === selected()) ?? null;
  const chosenRequest = () => requests().find((r) => r.id === request()) ?? null;
  const openRequests = () => requests().filter((r) => r.state === 'open').length
    + files().length + imports().length;
  const chosenFile = () => files().find((f) => `file:${f.id}` === sent()) ?? null;
  const chosenImport = () => imports().find((i) => `import:${i.branch}` === sent()) ?? null;
  const chosenKept = () => kept().find((k) => k.key === keptKey()) ?? null;
  const selectedObjects = () => props.selected?.() ?? [];
  /// Why the document is not the asker's to move
  const hereWhy = () => moveWhy(reach());
  /// Why the rows chosen are not taken back: the first that says
  const undoWhy = () => pickedRows().map((r) => rowUndoWhy(r, reach())).find((w) => w) ?? '';

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
      oldest = kept && oldest ? oldest : (r.oldest ?? 0);
      setReach({ host: r.host, write: r.write });
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
      setReach({ host: b.host, write: b.write });
    } catch { /* said by the rows */ }
    try {
      const q = await logRequests(props.doc());
      setRequests(sortRequests(q.requests));
      if (request() !== null && !q.requests.some((x) => x.id === request())) setRequest(null);
      setFiles(q.files ?? []);
      setImports(q.imports ?? []);
      if (sent() !== null && !chosenFile() && !chosenImport()) setSent(null);
    } catch { /* said by the rows */ }
    if (tab() === 'versions') {
      try {
        const v = await logVersions(props.doc());
        // Newest first, as the rows are
        setVersions([...v.versions].reverse());
        if (version() !== null && !v.versions.some((x) => x.num === version())) setVersion(null);
      } catch { /* said by the rows */ }
    }
    // Asked with every list, whichever is shown: the tab's name counts them
    try {
      const k = await logKept(props.doc());
      setKept(keptLines(k.values, k.objects));
      setKeptCount(k.count);
      if (keptKey() !== null && !kept().some((x) => x.key === keptKey())) chooseKept(null);
    } catch { /* said by the rows */ }
  };

  const loadOlder = async () => {
    const list = rows();
    const before = oldest || (list.length ? list[list.length - 1].seq : 0);
    if (!before) return;
    try {
      const r = await logRows(props.doc(), { before });
      setRows([...rows(), ...r.rows]);
      oldest = r.oldest ?? before;
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
    setUndone(null);
    if (seqs.length !== 1) return;
    const seq = seqs[0];
    // A merge, or an undo of one: whether a merge undone at the head can be
    // rolled back -- asked now, and not with every list (Gui/SceneLog.cpp)
    const row = rows().find((r) => r.seq === seq);
    if (row && (row.kind === 'merge' || row.kind === 'undo')) {
      logRollBack(true, props.doc())
        .then((r) => {
          if (selected() === seq)
            setUndone({ source: r.source, rows: r.rows, may: !!r.may, why: r.why ?? '' });
        })
        .catch(() => { /* none undone at the head */ });
    }
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
    setAskText(q.choices?.length ? q.choices[0].value : (q.text ?? ''));
    setAsk(q);
  };
  const askProblem = () => {
    const q = ask();
    return q?.problem && (q.text !== undefined || q.choices) ? q.problem(askText()) : '';
  };
  const answer = () => {
    const q = ask();
    if (!q || askProblem()) return;
    const text = askText().trim();
    setAsk(null);
    void act(() => q.run(text));
  };
  const answerOther = () => {
    const q = ask();
    if (!q?.other) return;
    setAsk(null);
    void act(q.other.run);
  };
  const answerMore = () => {
    const q = ask();
    if (!q?.more) return;
    setAsk(null);
    q.more.open();
  };

  /// The list of what it would do, asked of the host; a side picked for
  /// each conflict, the act goes on with them
  const openPicker = (of: { rows: number[] } | { request: number } | { import: string },
                      title: string, yes: string,
                      run: (sides: LogSides) => Promise<string>) => {
    if (busy()) return;
    setBusy(true);
    setStatus(null);
    logPreview(of, props.doc()).then(
      (p) => {
        setSides({});
        setPicker({ title, yes, line: previewText(p, title), changes: sortChanges(p.changes),
                    run });
      },
      (e: any) => setStatus({ text: actRefusal(e), bad: true }),
    ).finally(() => setBusy(false));
  };
  const pickSide = (key: string, side: Side) => setSides({ ...sides(), [key]: side });
  /// A change of theirs left out, or in again: by its key, so with every
  /// change of the same key
  const leaveOut = (key: string, out: boolean) => {
    const next = { ...sides() };
    if (out) next[key] = 'ours'; else delete next[key];
    setSides(next);
  };
  const pickAll = (side: Side) => {
    const all: Record<string, Side> = {};
    for (const c of picker()?.changes ?? []) {
      if (changePicked(c)) all[c.key] = side;
      else if (changeLeftOut(c, sides())) all[c.key] = 'ours';   // stays out
    }
    setSides(all);
  };
  const pickerLeft = () => unpickedConflicts(picker()?.changes ?? [], sides());
  const answerPicker = () => {
    const p = picker();
    if (!p || pickerLeft()) return;
    const picks = picksOf(p.changes, sides());
    setPicker(null);
    void act(() => p.run({ picks }));
  };

  /// What was asked met what was set here since: the host says how many,
  /// and the card asks a side for them all -- or opens the list, where a
  /// side is picked for each.
  interface Sided {
    run: (sides?: LogSides) => Promise<string>;
    of: { rows: number[] } | { request: number } | { import: string };
    title: string;
    yes: string;
  }
  const askSide = (said: string, s: Sided) => question({
    label: `${said} Which side for all of them?`,
    yes: 'Take theirs',
    run: () => s.run({ fallback: 'theirs' }),
    other: { label: 'Keep ours', run: () => s.run({ fallback: 'ours' }) },
    more: { label: 'Pick each...', open: () => openPicker(s.of, s.title, s.yes, s.run) },
  });
  /// An act that may meet conflicts: done, or asked a side and done again
  const actWithSides = (s: Sided) => {
    const run = s.run;
    if (busy()) return;
    setBusy(true);
    setStatus(null);
    run().then(
      (text) => setStatus({ text, bad: false }),
      (e: any) => {
        if (e?.code === 'Conflicts') askSide(actRefusal(e), s);
        else setStatus({ text: actRefusal(e), bad: true });
      },
    ).finally(() => {
      setBusy(false);
      void refresh();
      void refreshLists();
    });
  };

  const applyPicked = () => {
    const seqs = [...picked()].sort((a, b) => a - b);
    if (!seqs.length) return;
    actWithSides({
      of: { rows: seqs },
      title: seqs.length === 1 ? `Apply row ${seqs[0]}` : `Apply ${seqs.length} rows`,
      yes: 'Apply',
      run: async (sides) => {
        const r = await logApply(seqs, sides, props.doc());
        choose([]);
        if (!r.row) return 'Applied: nothing changed here';
        return seqs.length === 1 ? `Row ${seqs[0]} applied as row ${r.row}`
                                 : `${seqs.length} rows applied as row ${r.row}`;
      },
    });
  };

  const askMerge = () => {
    const source = reqSource();
    const target = reqTarget();
    const note = reqNote().trim();
    if (requestProblem(source, target)) return;
    void act(async () => {
      const r = await logRequest(source, target, note, props.doc());
      setReqNote('');
      return `Request ${r.request}: merge ${source} into ${target}`;
    });
  };
  const mergeRequested = (r: LogRequest) => actWithSides({
    of: { request: r.id },
    title: `Merge ${r.source} into ${r.target}`,
    yes: 'Merge',
    run: async (sides) => {
      const m = await logMergeRequest(r.id, sides, props.doc());
      return m.forwarded ? `Merged ${r.source}: ${m.forwarded} rows taken as they are`
                         : `Merged ${r.source} as row ${m.row}`;
    },
  });
  /// The merge of a request looked at first: what it would do, a side for
  /// each conflict
  const previewRequested = (r: LogRequest) => openPicker(
    { request: r.id }, `Merge ${r.source} into ${r.target}`, 'Merge',
    async (sides) => {
      const m = await logMergeRequest(r.id, sides, props.doc());
      return m.forwarded ? `Merged ${r.source}: ${m.forwarded} rows taken as they are`
                         : `Merged ${r.source} as row ${m.row}`;
    });
  const rollBack = () => {
    const u = undone();
    if (!u) return;
    question({
      label: `Take the merge of ${u.source} out of the history? Its ${u.rows} rows go, and a `
             + 'merge of it asks everything again. This cannot be undone.',
      yes: 'Roll back',
      run: async () => {
        const r = await logRollBack(false, props.doc());
        choose([]);
        return `The merge of ${r.source} rolled back: ${r.rows} rows out`;
      },
    });
  };
  const closeRequest = (r: LogRequest, state: 'withdrawn' | 'rejected') => void act(async () => {
    await logCloseRequest(r.id, state, props.doc());
    return `Request ${r.id} ${state}`;
  });

  // The requests of sec 30.20, and what takes rows out for good: the host's
  const bringFile = (f: LogSentFile) => void act(async () => {
    const r = await logBring(f.id, props.doc());
    if (r.stopped) return `${r.name}: ${r.rows} rows came to branch ${r.into}, then one could `
                          + `not be applied: ${r.why ?? ''}`;
    return r.rows ? `${r.name} brought in: ${r.rows} rows on branch ${r.into}, not merged`
                  : `${r.name}: nothing in it to bring`;
  });
  const dropFile = (f: LogSentFile) => question({
    label: f.mine ? `Withdraw ${f.name}? It goes unread.`
                  : `Delete ${f.name}, unread? Whoever sent it is not told.`,
    yes: f.mine ? 'Withdraw' : 'Delete',
    run: async () => {
      await logDropFile(f.id, props.doc());
      setSent(null);
      return f.mine ? `${f.name} withdrawn, unread` : `${f.name} deleted, unread`;
    },
  });
  const mergeImport = (i: LogImport) => actWithSides({
    of: { import: i.branch },
    title: `Merge ${i.branch}`,
    yes: 'Merge',
    run: async (sides) => {
      const m = await logMergeImport(i.branch, sides, props.doc());
      return m.forwarded ? `Merged ${i.branch}: ${m.forwarded} rows taken as they are`
                         : `Merged ${i.branch} as row ${m.row}`;
    },
  });
  const previewImport = (i: LogImport) => openPicker(
    { import: i.branch }, `Merge ${i.branch}`, 'Merge',
    async (sides) => {
      const m = await logMergeImport(i.branch, sides, props.doc());
      return m.forwarded ? `Merged ${i.branch}: ${m.forwarded} rows taken as they are`
                         : `Merged ${i.branch} as row ${m.row}`;
    });
  const deleteImport = (i: LogImport) => question({
    label: `Delete branch ${i.branch}, and with it what was brought in from ${i.file}? This `
           + 'cannot be undone.',
    yes: 'Delete',
    run: async () => {
      const r = await logDeleteBranch(i.branch, props.doc());
      setSent(null);
      return `Request ${i.branch} deleted: ${r.rows} rows gone`;
    },
  });
  const trimTo = (v: LogVersion) => question({
    label: `Remove the history of ${v.branchName} before ${versionText(v)}, but what another `
           + 'branch holds and the named versions? This cannot be undone.',
    yes: 'Trim',
    run: async () => {
      const r = await logTrim(v.branchName, v.num, props.doc());
      return `${v.branchName} trimmed to ${versionText(v)}: ${r.rows} rows gone`;
    },
  });
  /// The versions it may start from are asked first: the card offers them
  const squashTo = (v: LogVersion) => {
    if (busy()) return;
    setStatus(null);
    logSquash(v.num, undefined, props.doc()).then((r) => {
      const behind = r.behind ?? [];
      if (!behind.length) {
        setStatus({ text: `No version behind ${versionText(v)} to squash from`, bad: true });
        return;
      }
      question({
        label: `Fold the rows from a version to ${versionText(v)} into one row of their net `
               + 'change? The unnamed versions between go. This cannot be undone. From:',
        choices: squashChoices(behind),
        yes: 'Squash',
        run: async (text) => {
          const from = Number(text);
          const done = await logSquash(v.num, from, props.doc());
          return `Squashed ${done.rows ?? 0} rows from version ${from} into row `
                 + `${done.row ?? v.seq}`;
        },
      });
    }, (e: any) => setStatus({ text: actRefusal(e), bad: true }));
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

  // What is kept in the file (sec 32.27). Nothing of it is a row: the list
  // is asked again after each act, and the host's cue says when another
  // kept or let go.

  /// A line of the list chosen: a value's text is read, as an op's is
  const chooseKept = (key: string | null) => {
    setKeptKey(key);
    setKeptValue(null);
    const line = kept().find((k) => k.key === key);
    if (!line?.ref) return;
    logValue(line.ref, props.doc())
      .then((r) => { if (keptKey() === key) setKeptValue(r); })
      .catch(() => { /* the line says what it is */ });
  };
  /// An op's value, at the row chosen
  const keepOpValue = (op: LogOp, keep: boolean) => {
    const seq = selected();
    if (seq === null || !op.obj) return;
    void act(async () => {
      const r = await logKeep({ obj: op.obj, prop: op.prop, row: seq }, keep, props.doc());
      if (selected() === seq) choose([seq]);
      return keepText(r.values, keep, `of ${op.label || op.obj} at row ${seq}`);
    });
  };
  const keepObject = (what: { obj?: string; cid?: number }, name: string, keep: boolean) =>
    void act(async () => {
      const r = await logKeepObject(what, keep, props.doc());
      const seq = selected();
      if (seq !== null) choose([seq]);
      if (!r.changed) return keep ? `Every value of ${name} was kept already`
                                  : `The values of ${name} were not kept`;
      return keep ? `Every value of ${name} is kept: a save writes them into the file`
                  : `The values of ${name} are no longer kept`;
    });
  /// The objects selected, as they are at a version's row
  const keepSelectedAt = (v: LogVersion, keep: boolean) => {
    const objects = selectedObjects();
    if (!objects.length || !v.seq) return;
    void act(async () => {
      const r = await logKeep({ objects, row: v.seq }, keep, props.doc());
      return keepText(r.values, keep, `of ${objects.length === 1 ? objects[0]
                                             : `${objects.length} objects`} at ${versionText(v)}`);
    });
  };
  const releaseKept = (line: KeptLine) => void act(async () => {
    await logRelease(line.ref, props.doc());
    return `${line.object}.${line.prop} let go`;
  });
  /// A value under an object kept by itself too, or no more
  const keepAlone = (line: KeptLine) => void act(async () => {
    const r = await logKeep({ cid: line.cid, prop: line.prop, row: line.seq }, !line.alone,
                            props.doc());
    return keepText(r.values, !line.alone, `of ${line.object} at row ${line.seq}`);
  });
  /// The kept value over its object, against what it has now: the edit a
  /// row's change is shown as
  const showKept = (line: KeptLine) => {
    if (!line.obj || !line.ref) return;
    setStatus(null);
    showKeptValue(line.obj, line.ref, props.doc())
      .catch((e) => setStatus({ text: e?.code === 'ViewOnly' ? logRefusal(e)
                                       : (e?.message ?? e?.code ?? 'not shown'), bad: true }));
  };
  const showKeptRow = (line: KeptLine) => {
    setTab('rows');
    choose([line.seq]);
  };

  createEffect(() => {
    if (!props.open()) return;
    props.doc();
    at = { head: 0, branch: 0, last: 0 };
    anchor = null;
    setRows([]);
    setBranches([]);
    setVersions([]);
    setVersion(null);
    setRequests([]);
    setRequest(null);
    setFiles([]);
    setImports([]);
    setSent(null);
    setKept([]);
    setKeptCount(0);
    // (not chooseKept: it reads the list, which this effect must not follow)
    setKeptKey(null);
    setKeptValue(null);
    oldest = 0;
    setAsk(null);
    setPicker(null);
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
                    title={mayMove(reach())
                      ? 'the branch the document is on; another switches to it'
                      : `the branch the document is on; ${moveWhy(reach())}`}
                    disabled={busy() || !mayMove(reach())}
                    onPointerDown={(e) => e.stopPropagation()}
                    onChange={(e) => {
                      const name = e.currentTarget.value;
                      // The host's answer is what moves it
                      e.currentTarget.value = current()?.name ?? name;
                      if (name !== current()?.name) switchTo(name);
                    }}>
              <For each={branches()}>
                {(b) => (
                  <option value={b.name} selected={b.current} disabled={!switchable(b, reach())}
                          title={branchRead(b) ? undefined : UNREAD}
                          classList={{ 'fc-log-unread': !branchRead(b) }}>
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
          <For each={['rows', 'versions', 'requests', 'kept'] as const}>
            {(which) => (
              <button class="fc-log-act fc-log-tab" classList={{ 'fc-log-sel': tab() === which }}
                      data-tab={which} onClick={() => setTab(which)}
                      title={which === 'kept'
                        ? 'the values a recompute wrote -- shapes -- that a save writes into the '
                          + 'file though no kept version reaches them'
                        : undefined}>
                {which === 'rows' ? 'Rows' : which === 'versions' ? 'Versions'
                  : which === 'kept' ? keptTabText(keptCount())
                    : openRequests() ? `Requests (${openRequests()})` : 'Requests'}
              </button>
            )}
          </For>
          <span class="fc-log-spacer" />
          <button class="fc-log-act" data-act="new-branch" disabled={busy() || !!hereWhy()}
                  title={hereWhy() || 'a new branch from where the document is, switched to'}
                  onClick={() => newBranch({}, 'here')}>
            Branch...
          </button>
          <button class="fc-log-act" data-act="rename-branch"
                  disabled={busy() || !current() || !!hereWhy()}
                  title={hereWhy() || 'rename the branch the document is on'}
                  onClick={renameBranch}>
            Rename...
          </button>
          <Show when={deletable(branches(), reach()).length}>
            <select class="fc-log-act" data-act="delete-branch" disabled={busy()}
                    title="delete another branch: the rows only it holds go with it"
                    onChange={(e) => {
                      const name = e.currentTarget.value;
                      e.currentTarget.value = '';
                      if (name) deleteBranch(name);
                    }}>
              <option value="">Delete...</option>
              <For each={deletable(branches(), reach())}>
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
            <Show when={ask()!.choices}>
              <select class="fc-log-act fc-log-askchoice" value={askText()}
                      onChange={(e) => setAskText(e.currentTarget.value)}>
                <For each={ask()!.choices!}>
                  {(c) => <option value={c.value}>{c.label}</option>}
                </For>
              </select>
            </Show>
            <Show when={ask()!.text !== undefined && !ask()!.choices}>
              <input class="fc-log-askinput" type="text" value={askText()}
                     ref={(el) => setTimeout(() => el.focus(), 0)}
                     onInput={(e) => setAskText(e.currentTarget.value)}
                     onKeyDown={(e) => { if (e.key === 'Escape') setAsk(null); }} />
            </Show>
            <button class="fc-log-act" type="submit" data-act="yes"
                    disabled={!!askProblem()} title={askProblem() || undefined}>
              {ask()!.yes}
            </button>
            <Show when={ask()!.other}>
              <button class="fc-log-act" type="button" data-act="other" onClick={answerOther}>
                {ask()!.other!.label}
              </button>
            </Show>
            <Show when={ask()!.more}>
              <button class="fc-log-act" type="button" data-act="more" onClick={answerMore}>
                {ask()!.more!.label}
              </button>
            </Show>
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

        <Show when={picker()}>
          <div class="fc-log-picker">
            <div class="fc-log-asklabel">{picker()!.line}</div>
            <div class="fc-log-rowacts">
              <button class="fc-log-act" data-act="pick-ours" onClick={() => pickAll('ours')}>
                All ours
              </button>
              <button class="fc-log-act" data-act="pick-theirs" onClick={() => pickAll('theirs')}>
                All theirs
              </button>
            </div>
            <div class="fc-log-rows fc-log-changes">
              <For each={picker()!.changes}>
                {(c) => (
                  <div class="fc-log-change"
                       classList={{ 'fc-log-conflict': c.kind === 'conflict',
                                    'fc-log-record': changeLeftOut(c, sides()) }}
                       data-key={c.key} title={c.note || undefined}>
                    <div class="fc-log-changehead">
                      <Show when={changeLeavable(c)}>
                        <input type="checkbox" class="fc-log-take" data-key={c.key}
                               checked={!changeLeftOut(c, sides())}
                               title="untick to leave it out: what is here stays"
                               onChange={(e) => leaveOut(c.key, !e.currentTarget.checked)} />
                      </Show>
                      <span>{changeName(c)}</span>
                      <span class="fc-log-kind">{c.kind === 'conflict' ? 'conflict'
                        : `${c.kind} ${c.op}`.trim()}</span>
                      <span class="fc-log-spacer" />
                      <Show when={!changePicked(c)}>
                        <span class="fc-log-kind">{changeTakes(c, sides())}</span>
                      </Show>
                    </div>
                    <Show when={changePicked(c)}>
                      <div class="fc-log-sides">
                        <For each={['ours', 'theirs'] as const}>
                          {(which) => (
                            <button class="fc-log-pick"
                                    classList={{ 'fc-log-sel': sides()[c.key] === which
                                                 || (c.kind === 'view' && !sides()[c.key]
                                                     && which === 'ours') }}
                                    data-side={which}
                                    title={c.base !== undefined ? `at the base: ${c.base || '-'}`
                                                                : undefined}
                                    onClick={() => pickSide(c.key, which)}>
                              <span class="fc-log-kind">{which}</span>
                              {(which === 'ours' ? c.ours : c.theirs) || '-'}
                            </button>
                          )}
                        </For>
                      </div>
                    </Show>
                  </div>
                )}
              </For>
            </div>
            <div class="fc-log-rowacts">
              <button class="fc-log-act" data-act="picker-yes" disabled={busy() || !!pickerLeft()}
                      title={pickerLeft() ? `${pickerLeft()} conflicts have no side yet`
                                          : undefined}
                      onClick={answerPicker}>
                {picker()!.yes}
              </button>
              <button class="fc-log-act" data-act="picker-no" onClick={() => setPicker(null)}>
                Cancel
              </button>
            </div>
          </div>
        </Show>

        <Show when={!picker() && tab() === 'rows'}>
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
                          'fc-log-before': reach().write && !reach().host && !row.within,
                        }}
                        data-seq={row.seq}
                        title={!row.here ? `on branch ${row.branchName}`
                          : reach().write && !reach().host && !row.within
                            ? 'from before the point this branch is yours to write from'
                            : undefined}
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
                                || !pickedRows().every((r) => rowUndoable(r, reach()))}
                      title={undoWhy()
                             || ('one new step taking back what the rows chosen did, though none '
                                 + 'is the last; refused when anything since changed what they '
                                 + 'touched')}
                      onClick={undoPicked}>
                {undoLabel(picked().length, picked()[0])}
              </button>
              <button class="fc-log-act" data-act="branch-here"
                      disabled={busy() || !chosenRow()
                                || !!rowBranchWhy(chosenRow()!, reach())}
                      title={(chosenRow() && rowBranchWhy(chosenRow()!, reach()))
                             || 'a new branch whose history ends at this row, switched to'}
                      onClick={() => newBranch({ row: selected()! }, `row ${selected()}`)}>
                Branch from here...
              </button>
              <Show when={undone()}>
                <button class="fc-log-act" data-act="roll-back"
                        disabled={busy() || !undone()!.may}
                        title={undone()!.why
                               || (`the merge of ${undone()!.source} was undone: its rows leave `
                                   + 'the history')}
                        onClick={rollBack}>
                  Roll back merge...
                </button>
              </Show>
              <Show when={pickedRows().some((r) => !r.here)}>
                <button class="fc-log-act" data-act="apply"
                        disabled={busy() || !rowsAppliable(pickedRows())}
                        title={'what the rows chosen set, written to this branch as a step '
                               + 'of its own; their branch is not merged by it'}
                        onClick={applyPicked}>
                  {picked().length === 1 ? `Apply row ${picked()[0]} here`
                                         : `Apply ${picked().length} rows here`}
                </button>
              </Show>
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
                        <Show when={opKeptText(op)}>
                          <span class="fc-log-kind fc-log-kept">{opKeptText(op)}</span>
                        </Show>
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
              <Show when={opKeepable(chosenOp()!)}>
                <button class="fc-log-side" data-act="keep-value"
                        disabled={busy() || (!chosenOp()!.keptValue && !ops()!.keep)}
                        title={chosenOp()!.keptValue
                          ? 'let go of this value: a save then leaves it out of the file, unless '
                            + 'a kept version reaches it or its object is kept'
                          : ops()!.keepWhy
                            || ('keep this value in the file: a save leaves a value a recompute '
                                + 'wrote out, unless a kept version reaches it')}
                        onClick={() => keepOpValue(chosenOp()!, !chosenOp()!.keptValue)}>
                  {chosenOp()!.keptValue ? 'Let go' : 'Keep'}
                </button>
                <button class="fc-log-side" data-act="keep-object"
                        disabled={busy() || !ops()!.keepObject}
                        title={!ops()!.keepObject
                          ? 'every value of an object is kept on every branch: the host keeps '
                            + 'them, and lets them go'
                          : chosenOp()!.keptObject ? 'stop keeping every value of it'
                            : 'keep every value of it the log has and will have: the file grows '
                              + 'by each'}
                        onClick={() => keepObject({ obj: chosenOp()!.obj },
                                                  chosenOp()!.label || chosenOp()!.obj!,
                                                  !chosenOp()!.keptObject)}>
                  {chosenOp()!.keptObject ? 'Stop keeping all' : 'Keep all of it'}
                </button>
              </Show>
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

        <Show when={!picker() && tab() === 'versions'}>
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
                          'fc-log-before': reach().write && !reach().host && !v.within,
                        }}
                        data-num={v.num}
                        onClick={() => setVersion(v.num)}>
                      <td class="fc-log-num">{v.num}</td>
                      <td>
                        {v.name || 'unnamed'}
                        <Show when={v.head}><span class="fc-log-kind">now</span></Show>
                      </td>
                      <td classList={{ 'fc-log-unread': v.branchRead === false }}
                          title={v.branchRead === false ? UNREAD : undefined}>
                        {v.branchName}
                      </td>
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
              <button class="fc-log-act" data-act="restore"
                      disabled={busy() || !!versionWhy(chosenVersion()!, reach())}
                      title={versionWhy(chosenVersion()!, reach())
                             || ('one step making the document what this version was; undo '
                                 + 'takes it back')}
                      onClick={() => restore(chosenVersion()!)}>
                Restore
              </button>
              <button class="fc-log-act" data-act="name-version"
                      disabled={busy() || !!versionWhy(chosenVersion()!, reach())}
                      title={versionWhy(chosenVersion()!, reach())
                             || 'a named version is never evicted'}
                      onClick={() => nameVersion(chosenVersion()!)}>
                {chosenVersion()!.named ? 'Rename...' : 'Name...'}
              </button>
              <Show when={chosenVersion()!.named}>
                <button class="fc-log-act" data-act="unname-version"
                        disabled={busy() || !!versionWhy(chosenVersion()!, reach())}
                        title={versionWhy(chosenVersion()!, reach()) || undefined}
                        onClick={() => unnameVersion(chosenVersion()!)}>
                  Unname
                </button>
              </Show>
              <button class="fc-log-act" data-act="branch-version"
                      disabled={busy() || !!versionWhy(chosenVersion()!, reach())}
                      title={versionWhy(chosenVersion()!, reach())
                             || 'a new branch from this version, switched to'}
                      onClick={() => newBranch({ version: chosenVersion()!.num },
                                               `version ${chosenVersion()!.num}`)}>
                Branch from here...
              </button>
              <button class="fc-log-act" data-act="keep-selected"
                      disabled={busy() || !selectedObjects().length || !chosenVersion()!.seq
                                || !!keepWhy(reach(), chosenVersion()!.within)}
                      title={!chosenVersion()!.seq ? 'this version was taken at no row'
                        : keepWhy(reach(), chosenVersion()!.within)
                          || (!selectedObjects().length
                            ? 'select the objects to keep as they are here'
                          : 'what a recompute made of the objects selected -- their shapes -- '
                            + 'there, written into the file by a save though the version is not '
                            + 'kept')}
                      onClick={() => keepSelectedAt(chosenVersion()!, true)}>
                Keep selected here
              </button>
              <button class="fc-log-act" data-act="let-go-selected"
                      disabled={busy() || !selectedObjects().length || !chosenVersion()!.seq}
                      title="let go of the objects selected as they are here"
                      onClick={() => keepSelectedAt(chosenVersion()!, false)}>
                Let go of selected
              </button>
              <button class="fc-log-act" data-act="trim"
                      disabled={busy() || !!trimWhy(reach(), chosenVersion()!)}
                      title={trimWhy(reach(), chosenVersion()!)
                             || (`remove the history of ${chosenVersion()!.branchName} before `
                                 + 'this version, but what another branch holds and the named '
                                 + 'versions')}
                      onClick={() => trimTo(chosenVersion()!)}>
                Trim to here...
              </button>
              <button class="fc-log-act" data-act="squash"
                      disabled={busy() || !reach().host}
                      title={!reach().host ? trimWhy(reach(), chosenVersion()!)
                             : ('fold the rows between an older version and this one into one row '
                                + 'of their net change; both versions stay')}
                      onClick={() => squashTo(chosenVersion()!)}>
                Squash to here...
              </button>
            </div>
          </Show>
        </Show>

        <Show when={!picker() && tab() === 'kept'}>
          <div class="fc-log-rows fc-log-keptlist">
            <table>
              <thead>
                <tr>
                  <th>Kept</th><th>Object</th><th>Property</th><th class="fc-log-num">Row</th>
                </tr>
              </thead>
              <tbody>
                <For each={kept()}>
                  {(k) => (
                    <tr classList={{ 'fc-log-sel': keptKey() === k.key,
                                     'fc-log-under': k.kind === 'under',
                                     'fc-log-away': k.kind !== 'under' && !k.rowThere
                                                    && k.kind === 'value' }}
                        data-key={k.key} data-kind={k.kind}
                        title={k.kind === 'value' && !k.rowThere
                          ? 'the row it was kept at is no longer in the log; the value is there '
                            + 'all the same'
                          : k.ref || undefined}
                        onClick={() => chooseKept(k.key)}>
                      <td>
                        {k.kind === 'value' ? 'one value' : k.kind === 'object' ? 'every value'
                          : ''}
                        <Show when={k.kind === 'under' && k.alone}>
                          <span class="fc-log-kind">by itself too</span>
                        </Show>
                        <Show when={keeperText(k)}>
                          <span class="fc-log-kind" data-keeper={k.keeper}>{keeperText(k)}</span>
                        </Show>
                      </td>
                      <td>{k.kind === 'under' ? '' : k.object}</td>
                      <td>{k.kind === 'object' ? keptCountText(k.count) : k.prop}</td>
                      <td class="fc-log-num">{keptRowText(k)}</td>
                    </tr>
                  )}
                </For>
              </tbody>
            </table>
            <Show when={!kept().length}>
              <div class="fc-log-empty">Nothing is kept beyond what the versions keep.</div>
            </Show>
          </div>

          <Show when={chosenKept()}>
            <div class="fc-log-rowacts">
              <Show when={chosenKept()!.kind === 'value'}>
                <button class="fc-log-act" data-act="kept-release"
                        disabled={busy() || !chosenKept()!.release}
                        title={chosenKept()!.why
                               || ('a save then leaves it out of the file, unless a kept version '
                                   + 'reaches it or its object is kept')}
                        onClick={() => releaseKept(chosenKept()!)}>
                  Let go
                </button>
              </Show>
              <Show when={chosenKept()!.kind === 'object'}>
                <button class="fc-log-act" data-act="kept-stop"
                        disabled={busy() || !chosenKept()!.release}
                        title={chosenKept()!.why || 'those kept by themselves stay kept'}
                        onClick={() => keepObject({ cid: chosenKept()!.cid },
                                                  chosenKept()!.object, false)}>
                  Stop keeping all
                </button>
              </Show>
              <Show when={chosenKept()!.kind === 'under'}>
                <button class="fc-log-act" data-act="kept-alone"
                        disabled={busy() || (chosenKept()!.alone ? !chosenKept()!.release
                                                                 : !chosenKept()!.keep)}
                        title={chosenKept()!.alone
                          ? (chosenKept()!.why || 'kept by itself no more: it goes with its row')
                          : chosenKept()!.keepWhy
                            || ('it then stays when its row goes, in a trim or a squash, and when '
                                + 'the object is no longer kept')}
                        onClick={() => keepAlone(chosenKept()!)}>
                  {chosenKept()!.alone ? 'Not by itself' : 'Keep by itself'}
                </button>
              </Show>
              <Show when={chosenKept()!.kind !== 'object' && chosenKept()!.obj}>
                <button class="fc-log-act" data-act="kept-show" disabled={props.viewOnly()}
                        title="the kept value over its object, against what the object has now"
                        onClick={() => showKept(chosenKept()!)}>
                  Show
                </button>
              </Show>
              <Show when={chosenKept()!.kind !== 'object' && chosenKept()!.rowThere}>
                <button class="fc-log-act" data-act="kept-row"
                        onClick={() => showKeptRow(chosenKept()!)}>
                  Show row {chosenKept()!.seq}
                </button>
              </Show>
            </div>
            <Show when={chosenKept()!.kind === 'object'}>
              <div class="fc-log-empty">
                Every value a recompute writes of this object is written into the file by a
                save, and each one to come is kept as it is made. They are kept by the rows that
                made them: a row trimmed or squashed takes its value with it.
              </div>
            </Show>
            <Show when={keptValue()}>
              <div class="fc-log-valuehead">
                <span class="fc-log-kind">
                  {keptValue()!.bytes} bytes{keptValue()!.truncated ? ', the start of it' : ''}
                  {keptValue()!.attachments.length
                    ? `, ${keptValue()!.attachments.length} attached` : ''}
                </span>
              </div>
              <pre class="fc-log-value">{keptValue()!.text}</pre>
            </Show>
          </Show>
        </Show>

        <Show when={!picker() && tab() === 'requests'}>
          <form class="fc-log-ask fc-log-reqnew" onSubmit={(e) => { e.preventDefault(); askMerge(); }}>
            <span>Merge</span>
            <select class="fc-log-act fc-log-reqsource" value={reqSource()}
                    onChange={(e) => setReqSource(e.currentTarget.value)}>
              <option value="">a branch...</option>
              <For each={branches()}>
                {(b) => <option value={b.name} disabled={!branchRead(b)}
                                title={branchRead(b) ? undefined : UNREAD}>{b.name}</option>}
              </For>
            </select>
            <span>into</span>
            <select class="fc-log-act fc-log-reqtarget" value={reqTarget()}
                    onChange={(e) => setReqTarget(e.currentTarget.value)}>
              <option value="">a branch...</option>
              <For each={branches()}>
                {(b) => <option value={b.name} disabled={!branchRead(b)}
                                title={branchRead(b) ? undefined : UNREAD}>{b.name}</option>}
              </For>
            </select>
            <input class="fc-log-askinput fc-log-reqnote" type="text" placeholder="a note"
                   value={reqNote()} onInput={(e) => setReqNote(e.currentTarget.value)} />
            <button class="fc-log-act" type="submit" data-act="request-new"
                    disabled={busy() || !!requestProblem(reqSource(), reqTarget())}
                    title={requestProblem(reqSource(), reqTarget())
                           || 'ask whoever writes the second branch to merge the first into it'}>
              Ask
            </button>
          </form>
          <div class="fc-log-rows fc-log-requests">
            <table>
              <thead>
                <tr><th class="fc-log-num">#</th><th>Request</th><th>Who</th><th>When</th></tr>
              </thead>
              <tbody>
                <For each={requests()}>
                  {(r) => (
                    <tr classList={{
                          'fc-log-sel': request() === r.id,
                          'fc-log-record': r.state !== 'open',
                        }}
                        data-id={r.id} title={r.note || undefined}
                        onClick={() => setRequest(r.id)}>
                      <td class="fc-log-num">{r.id}</td>
                      <td>
                        {requestText(r)}
                        <span class="fc-log-kind">{requestState(r)}</span>
                      </td>
                      <td>{logAuthor({ author: r.by, authorKind: r.byKind })}</td>
                      <td>{logWhen(r.time)}</td>
                    </tr>
                  )}
                </For>
              </tbody>
            </table>
            <Show when={!requests().length}>
              <div class="fc-log-empty">No requests.</div>
            </Show>
          </div>

          <Show when={chosenRequest()}>
            <div class="fc-log-rowacts">
              <Show when={chosenRequest()!.note}>
                <span class="fc-log-asklabel fc-log-reqsaid">{chosenRequest()!.note}</span>
              </Show>
              <button class="fc-log-act" data-act="request-merge"
                      disabled={busy() || !requestActs(chosenRequest()!).merge}
                      title={requestActs(chosenRequest()!).mergeWhy
                             || 'merge it into the branch the document is on'}
                      onClick={() => mergeRequested(chosenRequest()!)}>
                Merge
              </button>
              <button class="fc-log-act" data-act="request-preview"
                      disabled={busy() || !requestActs(chosenRequest()!).merge}
                      title={requestActs(chosenRequest()!).mergeWhy
                             || 'what it would merge, a side picked for each conflict'}
                      onClick={() => previewRequested(chosenRequest()!)}>
                Look first...
              </button>
              <button class="fc-log-act" data-act="request-reject"
                      disabled={busy() || !requestActs(chosenRequest()!).reject}
                      onClick={() => closeRequest(chosenRequest()!, 'rejected')}>
                Reject
              </button>
              <button class="fc-log-act" data-act="request-withdraw"
                      disabled={busy() || !requestActs(chosenRequest()!).withdraw}
                      onClick={() => closeRequest(chosenRequest()!, 'withdrawn')}>
                Withdraw
              </button>
            </div>
          </Show>

          <Show when={files().length || imports().length}>
            <div class="fc-log-rows fc-log-sentlist">
              <table>
                <thead>
                  <tr><th>Sent, or brought in</th><th>Who</th><th>When</th></tr>
                </thead>
                <tbody>
                  <For each={files()}>
                    {(f) => (
                      <tr classList={{ 'fc-log-sel': sent() === `file:${f.id}` }}
                          data-file={f.id}
                          title={'nothing of it has been opened: bringing it in reads it, as a '
                                 + 'file handed over any other way would be read'}
                          onClick={() => setSent(`file:${f.id}`)}>
                        <td>{sentFileText(f)}</td>
                        <td>{logAuthor({ author: f.by, authorKind: f.byKind })}</td>
                        <td>{logWhen(f.time)}</td>
                      </tr>
                    )}
                  </For>
                  <For each={imports()}>
                    {(i) => (
                      <tr classList={{ 'fc-log-sel': sent() === `import:${i.branch}` }}
                          data-import={i.branch}
                          title={`brought in from ${i.file} and not merged into this branch`}
                          onClick={() => setSent(`import:${i.branch}`)}>
                        <td>{importText(i)}</td>
                        <td>{importWho(i)}</td>
                        <td>{logWhen(i.time)}</td>
                      </tr>
                    )}
                  </For>
                </tbody>
              </table>
            </div>
          </Show>

          <Show when={chosenFile()}>
            <div class="fc-log-rowacts">
              <button class="fc-log-act" data-act="file-bring"
                      disabled={busy() || !!bringWhy(reach())}
                      title={bringWhy(reach())
                             || 'read it into a branch of its own, to be merged from this list'}
                      onClick={() => bringFile(chosenFile()!)}>
                Bring in
              </button>
              <button class="fc-log-act" data-act="file-drop"
                      disabled={busy() || !(reach().host || chosenFile()!.mine)}
                      title={reach().host || chosenFile()!.mine ? 'let it go, unread'
                                                                : bringWhy(reach())}
                      onClick={() => dropFile(chosenFile()!)}>
                {chosenFile()!.mine && !reach().host ? 'Withdraw' : 'Delete, unread'}
              </button>
            </div>
          </Show>

          <Show when={chosenImport()}>
            <div class="fc-log-rowacts">
              <button class="fc-log-act" data-act="import-merge"
                      disabled={busy() || !!bringWhy(reach())}
                      title={bringWhy(reach()) || 'merge it into the branch the document is on'}
                      onClick={() => mergeImport(chosenImport()!)}>
                Merge
              </button>
              <button class="fc-log-act" data-act="import-preview"
                      disabled={busy() || !!bringWhy(reach())}
                      title={bringWhy(reach())
                             || 'what it would merge, a side picked for each conflict'}
                      onClick={() => previewImport(chosenImport()!)}>
                Look first...
              </button>
              <button class="fc-log-act" data-act="import-delete"
                      disabled={busy() || !!bringWhy(reach())}
                      title={bringWhy(reach()) || 'delete the branch, and what was brought in'}
                      onClick={() => deleteImport(chosenImport()!)}>
                Delete
              </button>
            </div>
          </Show>
        </Show>
      </div>
    </Show>
  );
}
