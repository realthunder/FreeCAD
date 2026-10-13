// What the transaction log's panel says and keeps (logpanel.tsx, docs/
// TransactionLog.md sec 32.21), apart from the drawing of it: plain
// functions, so that the gate checks them in node with no DOM
// (widgets/gate.ts).

import type { LogOp, LogRow } from './control';

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
