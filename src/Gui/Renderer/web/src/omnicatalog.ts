// The command and parameter lists behind the omni search box
// (docs/OmniSearch.md sec 6): shipped whole once, kept by version.
//
// The box re-filters on every keystroke, and a round trip per key is
// what this module exists to avoid: the lists live here, in memory and
// in localStorage, and the backend is asked only "what changed since
// version N of session S" -- once when the box opens, and whenever the
// backend pushes "omni.changed". The answer is the delta (rows to add
// or replace, keys to remove), or the whole list when the version is
// from another run or out of the backend's history. Filtering is
// local; the only per-query traffic is the volatile detail of the rows
// on screen (omni.rows), which the box fetches itself.
import { createSignal } from 'solid-js';
import { omniCatalog, onPush } from './control';
import type { CatalogReply } from './control';

export interface CommandRow {
  name: string;
  title: string;
  desc: string;
  shortcut?: string;
  group?: boolean;
}

export interface ParamItem { text: string; tooltip?: string; data?: string }

export interface ParamRow {
  /// ParamRegistry key: group path + '/' + entry
  key: string;
  /// The displayed path, "Preferences/View/SyncSelect"
  path: string;
  group: string;
  entry: string;
  /// "Gui::ViewParams::SyncSelect"
  name: string;
  title: string;
  doc: string;
  type: 'Bool' | 'Int' | 'UInt' | 'Hex' | 'Float' | 'String';
  default: string;
  proxy?: string;
  min?: number;
  max?: number;
  step?: number;
  decimals?: number;
  transparency?: boolean;
  items?: ParamItem[];
  dataIsString?: boolean;
}

export type ListName = 'commands' | 'params';

/// A row with its lower-cased search text, computed once
export type Indexed<T> = T & { search: string };

/// Split a query on whitespace into lower-cased keywords
export function keywords(query: string): string[] {
  return query.toLowerCase().split(/\s+/).filter((k) => k.length > 0);
}

/// Every keyword a substring of the (lower-cased) haystack
export function matches(haystack: string, keys: string[]): boolean {
  for (const k of keys) if (!haystack.includes(k)) return false;
  return true;
}

interface Stored<T> { session: string; version: number; rows: T[] }

const KEY_OF: Record<ListName, (row: any) => string> = {
  commands: (r: CommandRow) => r.name,
  params: (r: ParamRow) => r.key,
};

const SEARCH_OF: Record<ListName, (row: any) => string> = {
  commands: (r: CommandRow) =>
    `${r.title} ${r.name} ${r.shortcut ?? ''} ${r.desc}`.toLowerCase(),
  // The desktop matches the full path, the accessor name, the title and
  // the documentation (ParamInfo::searchText)
  params: (r: ParamRow) => `${r.key} ${r.name} ${r.title} ${r.doc}`.toLowerCase(),
};

const ORDER_OF: Record<ListName, (a: any, b: any) => number> = {
  commands: (a: CommandRow, b: CommandRow) => a.title.localeCompare(b.title),
  params: (a: ParamRow, b: ParamRow) => a.path.localeCompare(b.path),
};

export class Catalog<T> {
  session = '';
  version = -1;
  private rows = new Map<string, Indexed<T>>();
  private sorted: Indexed<T>[] | null = null;
  /// Bumped on every change, for the box's memos
  readonly generation;
  private setGeneration: (n: number) => void;
  private inflight: Promise<void> | null = null;
  private wanted = false;

  constructor(readonly list: ListName) {
    const [gen, setGen] = createSignal(0);
    this.generation = gen;
    this.setGeneration = setGen;
    this.load();
  }

  get size(): number { return this.rows.size; }

  /// Every row in display order
  all(): Indexed<T>[] {
    if (!this.sorted)
      this.sorted = [...this.rows.values()].sort(ORDER_OF[this.list]);
    return this.sorted;
  }

  get(key: string): Indexed<T> | undefined { return this.rows.get(key); }

  /// The rows carrying every keyword of the query, at most `limit`
  search(query: string, limit: number): { rows: Indexed<T>[]; total: number } {
    const keys = keywords(query);
    const out: Indexed<T>[] = [];
    let total = 0;
    for (const row of this.all()) {
      if (keys.length && !matches(row.search, keys)) continue;
      ++total;
      if (out.length < limit) out.push(row);
    }
    return { rows: out, total };
  }

  /// Ask the backend for what changed since what we hold. Coalesced:
  /// a call while one is in flight runs once more after it, not N
  /// times.
  sync(): Promise<void> {
    if (this.inflight) {
      this.wanted = true;
      return this.inflight;
    }
    const t0 = performance.now();
    this.inflight = omniCatalog(this.list, this.session, this.version)
      .then((r) => this.apply(r, performance.now() - t0))
      .catch((e) => {
        console.log(`fcviewer-ui: omni ${this.list} sync failed ${e?.code ?? e}`);
      })
      .finally(() => {
        this.inflight = null;
        if (this.wanted) {
          this.wanted = false;
          void this.sync();
        }
      });
    return this.inflight;
  }

  private index(row: T): Indexed<T> {
    return Object.assign({}, row, { search: SEARCH_OF[this.list](row) });
  }

  private apply(r: CatalogReply, ms: number) {
    if (r.full || r.session !== this.session) this.rows.clear();
    const keyOf = KEY_OF[this.list];
    for (const row of r.add ?? []) this.rows.set(keyOf(row), this.index(row));
    for (const key of r.remove ?? []) this.rows.delete(key);
    this.session = r.session;
    this.version = r.version;
    this.sorted = null;
    this.setGeneration(this.generation() + 1);
    this.save();
    // The wire cost, for the record: a delta of nothing is ~100 bytes
    const bytes = JSON.stringify(r).length;
    console.log(`fcviewer-ui: omni ${this.list} v${r.version} `
      + `${r.full ? 'full' : 'delta'} +${(r.add ?? []).length} `
      + `-${(r.remove ?? []).length} ${bytes} bytes ${ms.toFixed(0)} ms `
      + `(${this.rows.size} rows)`);
  }

  private storeKey(): string { return `fc.omni.${this.list}`; }

  private load() {
    try {
      const raw = localStorage.getItem(this.storeKey());
      if (!raw) return;
      const s = JSON.parse(raw) as Stored<T>;
      if (typeof s?.session !== 'string' || typeof s?.version !== 'number'
          || !Array.isArray(s.rows))
        return;
      const keyOf = KEY_OF[this.list];
      for (const row of s.rows) this.rows.set(keyOf(row), this.index(row));
      this.session = s.session;
      this.version = s.version;
      this.setGeneration(this.generation() + 1);
    }
    catch {
      // private mode, quota, a corrupt value: the first sync ships it whole
    }
  }

  private save() {
    try {
      const rows: T[] = [];
      for (const row of this.rows.values()) {
        const { search: _search, ...plain } = row as any;
        rows.push(plain as T);
      }
      const s: Stored<T> = { session: this.session, version: this.version, rows };
      localStorage.setItem(this.storeKey(), JSON.stringify(s));
    }
    catch {
      // no memory of it next time; the delta still applied
    }
  }
}

export const commands = new Catalog<CommandRow>('commands');
export const params = new Catalog<ParamRow>('params');

export function catalog(list: ListName): Catalog<any> {
  return list === 'commands' ? commands : params;
}

// The backend announces a catalog that moved (a workbench loaded, a
// shortcut changed); fetch the delta only when it is news to us.
onPush('omni.changed', (d) => {
  const list = d.list as ListName;
  if (list !== 'commands' && list !== 'params') return;
  const c = catalog(list);
  if (d.session !== c.session || (typeof d.version === 'number' && d.version > c.version))
    void c.sync();
});
