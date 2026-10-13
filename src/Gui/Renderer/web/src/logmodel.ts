// What the transaction log's panel says and keeps (logpanel.tsx, docs/
// TransactionLog.md sec 32.21), apart from the drawing of it: plain
// functions, so that the gate checks them in node with no DOM
// (widgets/gate.ts).

import type { LogBranch, LogOp, LogReach, LogRow, LogVersion } from './control';

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
/// the host, it is the asker's own and not from before the serving point
/// (sec 32.23). The host decides; this is what the card offers.
export function rowUndoable(row: LogRow, reach: LogReach): boolean {
  if (!row.here || row.ops === 0 || !UNDO_KINDS.includes(row.kind)) return false;
  return reach.host || (row.mine && row.within);
}

/// Why a row is not offered to be taken back, where that is who asks.
export function rowUndoWhy(row: LogRow, reach: LogReach): string {
  if (reach.host || !row.here || row.ops === 0 || !UNDO_KINDS.includes(row.kind)) return '';
  if (!row.within) return BEFORE;
  return row.mine ? '' : `${row.author} made this row: it is theirs to take back, or the host's`;
}

const BEFORE = 'from before this document was served: the host\'s';
const SHARED = 'in a shared document this is the host\'s to do';

/// Whether the one who asks may move the document in its log at all --
/// switch, restore, make or rename a branch: the host anywhere, the others
/// in a document that was opened for them from a branch.
export function mayMove(reach: LogReach): boolean {
  return reach.host || reach.own;
}

/// Why something that moves the document is not offered; empty when it is.
/// `within` is whether what it is done to -- a row, a version, a branch --
/// is from the serving point on.
export function moveWhy(reach: LogReach, within = true): string {
  if (reach.host) return '';
  if (!reach.own) return SHARED;
  return within ? '' : BEFORE;
}

/// A row a branch can be made from: any for the host; for the others one
/// made since the serving point, or the serving point itself.
export function rowBranchWhy(row: LogRow, reach: LogReach, floor?: number): string {
  return moveWhy(reach, row.within || row.seq === floor);
}

/// The branches offered to be deleted: not the one the document is on; for
/// all but the host, in a document of their own, those made since the
/// serving point but the one the document was opened on.
export function deletable(branches: LogBranch[], reach: LogReach): LogBranch[] {
  return branches.filter((b) => !b.current
    && (reach.host || (reach.own && b.within && !b.root)));
}

/// Whether a version's acts are the asker's: see moveWhy.
export function versionWhy(v: LogVersion, reach: LogReach): string {
  return moveWhy(reach, v.within);
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
    case 'Editing': return 'An edit is open: leave it first.';
    case 'Offline':
    case 'Timeout': return 'The viewer did not answer.';
    default: return e?.message || e?.code || 'not done';
  }
}
