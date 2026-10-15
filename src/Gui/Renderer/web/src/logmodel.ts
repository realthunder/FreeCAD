// What the transaction log's panel says and keeps (logpanel.tsx, docs/
// TransactionLog.md sec 32.21), apart from the drawing of it: plain
// functions, so that the gate checks them in node with no DOM
// (widgets/gate.ts).

import type {
  LogBehind, LogBranch, LogChange, LogImport, LogKeptObject, LogKeptValue, LogOp, LogReach,
  LogRequest, LogRow, LogSentFile, LogVersion,
} from './control';

/// What a refusal says, in the card's words.
export function logRefusal(e: { code?: string; message?: string } | undefined): string {
  switch (e?.code) {
    case 'ViewOnly': return 'Seeing the history needs the right to edit.';
    case 'NoLog': return 'This document keeps no history.';
    case 'UnknownDocument': return 'No document is served here.';
    default: return e?.message ?? e?.code ?? 'the history could not be read';
  }
}

/// When a row was written: the time today, the day and the time before.
export function logWhen(seconds: number, now = new Date()): string {
  if (!seconds) return '';
  const d = new Date(seconds * 1000);
  const time = d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
  if (d.toDateString() === now.toDateString()) return time;
  return d.toLocaleDateString([], { month: 'short', day: 'numeric' }) + ' ' + time;
}

/// Who wrote it: a name a login verified, or the desktop's, stands alone;
/// one nothing verified says so beside it, as the desktop's panel does.
export function logAuthor(row: { author: string; authorKind: string }): string {
  const unverified = ['invited', 'declared', 'enrolled'].includes(row.authorKind);
  return unverified ? `${row.author} (${row.authorKind})` : row.author;
}

/// The rows after an answer to "the newest page": that page, and under it
/// what was loaded of the rows older than it, where those are kept.
export function mergeNewest(old: LogRow[], page: LogRow[], keep: boolean): LogRow[] {
  if (!keep || !page.length) return page;
  const oldest = page[page.length - 1].seq;
  return [...page, ...old.filter((r) => r.seq < oldest)];
}

/// What an op did, in a word or two.
export function logOpText(op: LogOp): string {
  const what = op.kind === 'doc' ? 'document' : (op.label || op.obj || `#${op.cid ?? '?'}`);
  switch (op.op) {
    case 'create': return `${what} made`;
    case 'remove': return `${what} removed`;
    case 'addprop': return `${what}: ${op.prop} added`;
    case 'delprop': return `${what}: ${op.prop} taken away`;
    default: return `${what}: ${op.prop}`;
  }
}

// Acting on what is listed (docs/TransactionLog.md sec 32.22): rows taken
// back, versions, branches.

/// The kinds of row that have something to take back, as the host has them.
const UNDO_KINDS = ['user', 'implicit', 'undo', 'redo', 'pick'];

/// Whether a row can be taken back from where the document is: it changed
/// something, by hand, and is of this branch's history -- and, for all but
/// the host, it is the asker's own (sec 30.10, 32.25). The host decides;
/// this is what the card offers.
export function rowUndoable(row: LogRow, reach: LogReach): boolean {
  if (!row.here || row.ops === 0 || !UNDO_KINDS.includes(row.kind)) return false;
  return reach.host || row.mine;
}

/// Why a row is not offered to be taken back, where that is who asks.
export function rowUndoWhy(row: LogRow, reach: LogReach): string {
  if (reach.host || !row.here || row.ops === 0 || !UNDO_KINDS.includes(row.kind)) return '';
  return row.mine ? '' : `${row.author} made this row: it is theirs to take back, or the host's`;
}

const BEFORE = 'from before the point this branch is yours to write from';
const NEEDS = 'this needs write access of the branch, which the host gives';

/// Whether the one who asks may move the document in its log at all --
/// switch, restore, make a branch: the host, and one who writes the branch
/// the document is on.
export function mayMove(reach: LogReach): boolean {
  return reach.host || reach.write;
}

/// Why something that moves the document is not offered; empty when it is.
/// `within` is whether what it is done to -- a row, a version -- is one the
/// asker may move a document from.
export function moveWhy(reach: LogReach, within = true): string {
  if (reach.host) return '';
  if (!reach.write) return NEEDS;
  return within ? '' : BEFORE;
}

/// A row a branch can be made from.
export function rowBranchWhy(row: LogRow, reach: LogReach): string {
  return moveWhy(reach, row.within);
}

/// The branches offered to be deleted: not the one the document is on; for
/// all but the host, those of the asker's own making.
export function deletable(branches: LogBranch[], reach: LogReach): LogBranch[] {
  return branches.filter((b) => !b.current && (reach.host || b.made));
}

/// A branch the asker reads. One it does not is listed greyed, by its name
/// alone (docs/TransactionLog.md sec 32.30).
export function branchRead(b: { read?: boolean }): boolean {
  return b.read !== false;
}

export const UNREAD = 'a branch this connection does not read: the host gives that';

/// The branches offered to be switched to: for all but the host, those the
/// asker writes -- and none where it does not write the one it is on.
export function switchable(b: LogBranch, reach: LogReach): boolean {
  if (b.current) return true;
  if (!branchRead(b)) return false;
  if (b.closed) return false;
  return reach.host || (reach.write && b.write);
}

/// Whether a version's acts are the asker's: see moveWhy.
export function versionWhy(v: LogVersion, reach: LogReach): string {
  return moveWhy(reach, v.within);
}

/// Rows that can be applied here: of another branch, and having set
/// something. Anyone who is listed them may.
export function rowsAppliable(rows: LogRow[]): boolean {
  return rows.length > 0 && rows.every((r) => !r.here && r.ops > 0);
}

/// What a request asks, in words.
export function requestText(r: LogRequest): string {
  return `merge ${r.source || '(a branch that is gone)'} into ${r.target || '(a branch that is gone)'}`;
}

/// How a request stands, in words.
export function requestState(r: LogRequest): string {
  if (r.state === 'open') return 'open';
  return r.closedBy ? `${r.state} by ${r.closedBy}` : r.state;
}

/// What the asker may do of a request, and why it may not merge one it
/// could: a merge is done in a document that is on the branch merged into.
export function requestActs(r: LogRequest): {
  merge: boolean; reject: boolean; withdraw: boolean; mergeWhy: string;
} {
  const open = r.state === 'open';
  return {
    merge: open && r.handles && r.here,
    reject: open && r.handles,
    withdraw: open && r.mine,
    mergeWhy: open && r.handles && !r.here
      ? `it is merged in a document that is on ${r.target}` : '',
  };
}

/// The requests as listed: those still open first, then the newest.
export function sortRequests(list: LogRequest[]): LogRequest[] {
  return [...list].sort((a, b) =>
    (a.state === 'open' ? 0 : 1) - (b.state === 'open' ? 0 : 1) || b.id - a.id);
}

/// Why two branches are not one to ask a merge of; empty when they are.
export function requestProblem(source: string, target: string): string {
  if (!source || !target) return 'two branches';
  return source === target ? 'a branch is not merged into itself' : '';
}

/// The rows chosen after a click on one: alone, or with the others where
/// `toggle` is held (and it goes if it was one of them), or all of those
/// listed between it and the one chosen last where `range` is.
export function pickRows(
  listed: number[], picked: number[], anchor: number | null, seq: number,
  how: { toggle?: boolean; range?: boolean } = {},
): { picked: number[]; anchor: number | null } {
  if (how.range && anchor !== null && listed.includes(anchor) && listed.includes(seq)) {
    const a = listed.indexOf(anchor);
    const b = listed.indexOf(seq);
    const between = listed.slice(Math.min(a, b), Math.max(a, b) + 1);
    const kept = how.toggle ? picked.filter((p) => !between.includes(p)) : [];
    return { picked: [...kept, ...between], anchor };
  }
  if (how.toggle) {
    return picked.includes(seq)
      ? { picked: picked.filter((p) => p !== seq), anchor }
      : { picked: [...picked, seq], anchor: seq };
  }
  return { picked: [seq], anchor: seq };
}

/// What the button that takes rows back says.
export function undoLabel(count: number, seq?: number): string {
  return count === 1 ? `Undo row ${seq ?? ''}`.trim() : `Undo ${count} rows`;
}

/// Why a name is not one a new branch can have; empty when it is.
export function branchNameProblem(name: string, taken: string[]): string {
  const said = name.trim();
  if (!said) return 'a branch needs a name';
  if (taken.includes(said)) return `there is a branch ${said} already`;
  return '';
}

/// A version in a word: its name, or its number where it has none.
export function versionText(v: { num: number; name: string }): string {
  return v.name ? v.name : `Version ${v.num}`;
}

/// What a refused act says, in the card's words: the host's reason where it
/// gave one -- which row is in the way, which name is taken.
export function actRefusal(e: { code?: string; message?: string } | undefined): string {
  switch (e?.code) {
    case 'ViewOnly': return 'Changing the history needs the right to edit.';
    case 'Forbidden': return e?.message || 'Only the host may do that.';
    case 'NotYours': return e?.message || 'That row is not yours to take back.';
    case 'Conflicts': return e?.message || 'It meets what was set here since.';
    case 'Editing': return 'An edit is open: leave it first.';
    case 'Offline':
    case 'Timeout': return 'The viewer did not answer.';
    default: return e?.message || e?.code || 'not done';
  }
}

// -- The merge's own panel (docs/TransactionLog.md sec 32.26) --------------

/// The changes of a preview in the order the desktop's merge dialog lists
/// them: the conflicts first, then what goes in, then what is kept or
/// recomputed.
export function sortChanges(changes: LogChange[]): LogChange[] {
  const rank = (kind: string) => (kind === 'conflict' ? 0
    : kind === 'merge' || kind === 'take' ? 1 : kind === 'view' ? 2
      : kind === 'derived' ? 3 : 4);
  return [...changes].sort((a, b) => rank(a.kind) - rank(b.kind));
}

/// A change's place: the object, by its label where it has one, and the
/// property.
export function changeName(c: LogChange): string {
  const what = c.ckind === 'doc' ? '(document)' : (c.label || c.object);
  return c.prop ? `${what}.${c.prop}` : what;
}

/// Whether a change is given a side here: a conflict, and a view's, which
/// keeps ours unless picked.
export function changePicked(c: LogChange): boolean {
  return c.kind === 'conflict' || c.kind === 'view';
}

/// Whether a change can be left out of the act, as the desktop's dialog
/// unticks it: one of theirs that goes in. Left out, it is sent as `ours`,
/// and every change of the same key with it -- the properties of one sketch
/// merged together share one (sec 31.10).
export function changeLeavable(c: LogChange): boolean {
  return c.kind === 'take' || c.kind === 'merge';
}

/// Whether a change is left out.
export function changeLeftOut(c: LogChange, sides: Record<string, 'ours' | 'theirs'>): boolean {
  return changeLeavable(c) && sides[c.key] === 'ours';
}

/// What goes in of a change: the side picked, or what its kind says.
export function changeTakes(c: LogChange, sides: Record<string, 'ours' | 'theirs'>): string {
  if (changePicked(c)) return sides[c.key] ?? (c.kind === 'view' ? 'ours' : 'pick a side');
  if (changeLeftOut(c, sides)) return 'left out';
  switch (c.kind) {
    case 'take': return 'theirs';
    case 'merge': return 'both';
    case 'same': return 'the same';
    case 'derived': return 'recomputed';
    case 'name': return 'named';
    case 'unit': return 'with its conflict';
    default: return c.kind;
  }
}

/// How many conflicts have no side yet: the act waits for none.
export function unpickedConflicts(changes: LogChange[],
                                  sides: Record<string, 'ours' | 'theirs'>): number {
  return changes.filter((c) => c.kind === 'conflict' && !sides[c.key]).length;
}

/// The picks an act sends: the sides said, of the changes that ask one,
/// and `ours` of those left out.
export function picksOf(changes: LogChange[], sides: Record<string, 'ours' | 'theirs'>):
  Record<string, 'ours' | 'theirs'> {
  const out: Record<string, 'ours' | 'theirs'> = {};
  for (const c of changes) {
    if (changePicked(c) && sides[c.key]) out[c.key] = sides[c.key];
    else if (changeLeftOut(c, sides)) out[c.key] = 'ours';
  }
  return out;
}

/// The line above the list: what is merged or applied, and how much of it.
export function previewText(p: { what: string; source?: string; count: number;
                                  conflicts: number; forward: number },
                            title: string): string {
  const many = (n: number, one: string) => `${n} ${one}${n === 1 ? '' : 's'}`;
  if (p.forward) return `${title}: nothing here moved since, ${many(p.forward, 'row')} taken as they are`;
  return `${title}: ${many(p.count, 'change')}, ${many(p.conflicts, 'conflict')}`;
}

// -- What is kept in the file (docs/TransactionLog.md sec 32.27) ----------

/// One line of the list of the kept, as the desktop's Kept tab has it: each
/// value kept by itself, then each object kept and under it the values the
/// log has of it.
export interface KeptLine {
  key: string;               // what a line is chosen by
  kind: 'value' | 'object' | 'under';
  cid: number;
  obj: string;               // its name, where the document has it
  object: string;            // in words
  prop: string;
  seq: number;
  rowThere: boolean;
  ref: string;               // none: an object kept
  alone: boolean;            // a value kept by itself
  release: boolean;          // the asker may let it go, or stop keeping it
  why: string;               // why not
  count: number;             // an object's: how many values are listed
  keeper: string;            // who kept it by itself, where not the host
  mine: boolean;             // the asker did
  keep: boolean;             // under an object: the asker may keep it by itself
  keepWhy: string;
}

/// An object of the list in words: its label, or its number where the
/// document no longer has it.
export function keptObjectText(o: { cid: number; obj?: string; label?: string;
                                     here: boolean }): string {
  if (!o.here) return `obj:${o.cid}, not in the document`;
  return o.label && o.label !== o.obj ? `${o.label} (${o.obj})` : (o.obj ?? `obj:${o.cid}`);
}

export function keptLines(values: LogKeptValue[], objects: LogKeptObject[]): KeptLine[] {
  const lines: KeptLine[] = [];
  for (const v of values) {
    lines.push({ key: `v:${v.ref}`, kind: 'value', cid: v.cid, obj: v.here ? v.obj ?? '' : '',
                 object: keptObjectText(v),
                 prop: v.prop, seq: v.seq, rowThere: v.row, ref: v.ref, alone: true,
                 release: v.release, why: v.why ?? '', count: 0, keeper: v.keeper ?? '',
                 mine: !!v.mine, keep: false, keepWhy: '' });
  }
  for (const o of objects) {
    lines.push({ key: `o:${o.cid}`, kind: 'object', cid: o.cid, obj: o.here ? o.obj ?? '' : '',
                 object: keptObjectText(o),
                 prop: '', seq: 0, rowThere: false, ref: '', alone: false,
                 release: o.release, why: o.why ?? '', count: o.values.length, keeper: '',
                 mine: false, keep: false, keepWhy: '' });
    for (const v of o.values) {
      lines.push({ key: `u:${o.cid}:${v.ref}`, kind: 'under', cid: o.cid,
                   obj: o.here ? o.obj ?? '' : '', object: keptObjectText(o), prop: v.prop, seq: v.seq, rowThere: true,
                   ref: v.ref, alone: v.alone, release: !!v.release, why: v.why ?? '',
                   count: 0, keeper: v.keeper ?? '', mine: !!v.mine, keep: !!v.keep,
                   keepWhy: v.keepWhy ?? '' });
    }
  }
  return lines;
}

/// Who kept a value by itself, in words: none where the host did.
export function keeperText(line: { keeper: string; mine: boolean }): string {
  if (line.mine) return 'by you';
  return line.keeper ? `by ${line.keeper}` : '';
}

/// Why keeping at a row -- a version's -- is not the asker's: the host's,
/// and of one who writes the branch since the point, which `within` says.
export function keepWhy(reach: LogReach, within: boolean): string {
  if (reach.host || within) return '';
  return reach.write ? BEFORE : NEEDS;
}

/// The tab's title: the count, so that something kept is seen unopened.
export function keptTabText(count: number): string {
  return count ? `Kept (${count})` : 'Kept';
}

/// The row a kept value is of, in words: a row trimmed or squashed away
/// says so, and the value is there all the same.
export function keptRowText(line: { kind: string; seq: number; rowThere: boolean }): string {
  if (line.kind === 'object') return '';
  return line.rowThere ? String(line.seq) : `${line.seq}, gone`;
}

/// What an object's line says of its values.
export function keptCountText(n: number): string {
  return n === 1 ? '1 value' : `${n} values`;
}

/// What an op says of its value where it is kept, as the desktop's "yes,
/// kept"; empty where it is not.
export function opKeptText(op: LogOp): string {
  if (op.keptValue && op.keptObject) return 'kept, and all of it';
  if (op.keptValue) return 'kept';
  return op.keptObject ? 'all of it kept' : '';
}

/// Whether an op's value can be kept from it: a value an object's property
/// was set to.
export function opKeepable(op: LogOp): boolean {
  return op.kind === 'obj' && op.op === 'set' && !!op.after && !!op.obj && op.here !== false;
}

/// What a keep said done, in the card's words.
export function keepText(values: number, keep: boolean, where: string): string {
  if (!values) return keep ? 'Nothing kept: the log holds no such value'
                           : 'Nothing let go: no such value was kept';
  const many = values === 1 ? '1 value' : `${values} values`;
  return keep ? `${many} ${where} kept: a save writes ${values === 1 ? 'it' : 'them'} into the file`
              : `${many} ${where} let go`;
}

// What takes rows out for good, and the requests of sec 30.20 -- a file
// sent, a branch an import made: the host's (docs/TransactionLog.md sec
// 32.30).

const HOST_TRIMS = "that takes rows out of the log for good, which is the host's";
const HOST_BRINGS = "a file sent, or a branch an import made, is the host's to bring in and merge";

/// Why a trim or a squash at a version is not offered; empty when it is.
export function trimWhy(reach: LogReach, v: { branchName?: string }): string {
  if (!reach.host) return HOST_TRIMS;
  return v.branchName ? '' : 'the version names no branch';
}

/// Why bringing in, or merging, a request of sec 30.20 is not offered.
export function bringWhy(reach: LogReach): string {
  return reach.host ? '' : HOST_BRINGS;
}

/// A size in words.
export function sizeText(bytes: number): string {
  if (bytes < 1024) return `${bytes} bytes`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

/// A file sent, in words: its name and size, and that nothing read it.
export function sentFileText(f: LogSentFile): string {
  return `${f.name}, ${sizeText(f.size)}, sent and not read`;
}

/// A branch an import made, in words.
export function importText(i: LogImport): string {
  let what = `${i.branch}: ${i.rows === 1 ? '1 operation' : `${i.rows} operations`}`;
  if (i.independent) what += ', shares no history';
  if (i.stopped) what += ', stopped at a row';
  return what;
}

/// Who made an import's rows, and who sent the file.
export function importWho(i: LogImport): string {
  const made = i.authors.join(', ');
  return i.by ? (made ? `${made}; sent by ${i.by}` : `sent by ${i.by}`) : made;
}

/// The versions a squash may start from, as the choices the card offers:
/// the value is the version's number.
export function squashChoices(behind: LogBehind[]): Array<{ value: string; label: string }> {
  return behind.map((b) => ({
    value: String(b.num),
    label: `version ${b.num}${b.name ? ` (${b.name})` : ''}: `
           + `${b.rows === 1 ? '1 row' : `${b.rows} rows`}`,
  }));
}
