// Object and property names, completed in the browser: the omni box's
// "Box.Length" grammar (docs/OmniSearch.md sec 6) and an expression typed
// into a value's on-view editor (docs/SketcherPort.md "One editor for a
// constraint's value") ask the same two caches. Nothing per keystroke
// crosses the wire: the document's objects are fetched once, an object's
// property descriptors once per object, and the rows recompute when a fetch
// lands.
import { createSignal } from 'solid-js';
import type { Accessor } from 'solid-js';
import { getContainerProperties, getProperties, omniObjects } from './control';
import type {
  ObjectEntry,
  ObjectsReply,
  PropDescriptor,
  PropScope,
  PropertiesReply,
  SelectionItem,
} from './control';

/// Where a property lives, for the editor and its commit
export interface PropTarget {
  doc: string;
  obj: string;
  scope: PropScope;
  /// A named view under scope 'view3d'; absent = the served view
  view?: string;
  /// A ".Name" query: every selected object is edited
  objs?: string[];
  /// "Box.Length", "#.Comment", "3 objects . Height"
  title: string;
}

/// One completion: what is shown, and what the text becomes when it is taken
export interface PathRow {
  key: string;
  title: string;
  desc?: string;
  /// What the typed path becomes when the row is picked
  complete?: string;
  kind: 'object' | 'property' | 'member';
  prop?: { target: PropTarget; prop: PropDescriptor };
}

export const stripLabel = (s: string) =>
  s.length >= 4 && s.startsWith('<<') && s.endsWith('>>') ? s.slice(2, -2) : s;

export const has = (hay: string, needle: string) =>
  !needle || hay.toLowerCase().includes(needle.toLowerCase());

export interface PathCompleter {
  /// The document's objects, null until they have arrived
  objects: Accessor<ObjectsReply | null>;
  /// Ask for the objects, once until reset()
  ensureObjects(): void;
  /// Forget everything: a new opening, another document
  reset(): void;
  findObject(name: string): ObjectEntry | undefined;
  /// The rows after "Obj.": sub-objects, properties, "ViewObject."
  objectMemberRows(doc: string, obj: ObjectEntry | undefined, objName: string,
                   tail: string, prefix: string, viewObject: boolean,
                   objs?: string[]): PathRow[];
  /// The rows for a path as typed: "Bo", "Box.Le", "#.", ".Height"
  rows(query: string, selection: SelectionItem[], limit: number)
    : { rows: PathRow[]; total: number };
}

export function createPathCompleter(fail: (e: any, what: string) => void): PathCompleter {
  const [objects, setObjects] = createSignal<ObjectsReply | null>(null);
  /// Bumped when a property fetch lands, so the rows recompute
  const [propGen, setPropGen] = createSignal(0);
  const propCache = new Map<string, PropertiesReply | 'pending' | 'failed'>();
  let objectsAsked = false;

  const reset = () => {
    setObjects(null);
    objectsAsked = false;
    propCache.clear();
  };

  const ensureObjects = () => {
    if (objectsAsked) return;
    objectsAsked = true;
    const t0 = performance.now();
    omniObjects()
      .then((r) => {
        console.log(`fcviewer-ui: omni objects ${r.objects.length} `
          + `${JSON.stringify(r).length} bytes ${(performance.now() - t0).toFixed(0)} ms`);
        setObjects(r);
      })
      .catch((e) => fail(e, 'objects'));
  };

  /// The property descriptors of a container, cached for the box's
  /// life; the first ask fetches and returns nothing, and the rows
  /// recompute when it lands.
  const propsFor = (subject: 'object' | 'document' | 'view3d', doc: string, obj: string,
                    view?: string): PropertiesReply | null => {
    const key = `${subject}|${doc}|${obj}|${view ?? ''}`;
    const hit = propCache.get(key);
    if (hit && hit !== 'pending' && hit !== 'failed') return hit;
    if (hit) return null;
    propCache.set(key, 'pending');
    const p = subject === 'object'
      ? getProperties(doc, obj, 'object')
      : getContainerProperties(subject, doc, view);
    p.then((r) => { propCache.set(key, r); setPropGen(propGen() + 1); })
      .catch(() => { propCache.set(key, 'failed'); setPropGen(propGen() + 1); });
    return null;
  };

  const findObject = (name: string): ObjectEntry | undefined => {
    const objs = objects();
    if (!objs) return undefined;
    const label = stripLabel(name);
    return objs.objects.find((o) => o.name === name)
      ?? objs.objects.find((o) => (o.label ?? o.name) === label);
  };

  const propRows = (r: PropertiesReply | null, scope: PropScope, tail: string,
                    prefix: string, target: Omit<PropTarget, 'title'>): PathRow[] => {
    if (!r) return [];
    const out: PathRow[] = [];
    for (const p of r.props) {
      if (p.hidden || p.scope !== scope || !has(p.name, tail)) continue;
      out.push({
        key: prefix + p.name, kind: 'property', title: p.name,
        desc: p.doc || `${p.type}${p.readonly ? ', read-only' : ''}`,
        complete: prefix + p.name,
        prop: { target: { ...target, title: prefix + p.name }, prop: p },
      });
    }
    return out;
  };

  /// The rows after "Obj." (sub-objects, properties, "ViewObject.") or
  /// "Obj.ViewObject." (the view provider's properties)
  const objectMemberRows = (doc: string, obj: ObjectEntry | undefined, objName: string,
                            tail: string, prefix: string, viewObject: boolean,
                            objs?: string[]): PathRow[] => {
    const r = propsFor('object', doc, objName);
    if (viewObject)
      return propRows(r, 'view', tail, prefix, { doc, obj: objName, scope: 'view', objs });
    const out: PathRow[] = [];
    for (const child of obj?.children ?? []) {
      if (!has(child, tail)) continue;
      const entry = findObject(child);
      out.push({
        key: prefix + child, kind: 'object', title: child,
        desc: entry ? `${entry.label ? `<<${entry.label}>>  ` : ''}${entry.type}` : 'sub-object',
        complete: prefix + child,
      });
    }
    out.push(...propRows(r, 'object', tail, prefix, { doc, obj: objName, scope: 'object', objs }));
    if (has('ViewObject.', tail))
      out.push({ key: prefix + 'ViewObject.', kind: 'member', title: 'ViewObject.',
                 desc: 'The properties of the view provider', complete: prefix + 'ViewObject.' });
    return out;
  };

  const rows = (query: string, selection: SelectionItem[], limit: number)
      : { rows: PathRow[]; total: number } => {
    const objs = objects();
    propGen();
    if (!objs) return { rows: [], total: 0 };
    let q = query;

    // "#." forms: the document itself, or one of its views
    const m = /^(.*?)#\.(?:([A-Za-z_]\w*)\.)?([^.]*)$/.exec(q);
    if (m) {
      const docName = m[1];
      const view = m[2];
      const tail = m[3];
      const prefix = q.slice(0, q.length - tail.length);
      const doc = docName ? stripLabel(docName) : '';
      if (!view) {
        const rows = propRows(propsFor('document', doc, ''), 'document', tail, prefix,
                              { doc, obj: '', scope: 'document' });
        // The view rows are `objs`, which is THIS document's listing
        // (omni.objects) -- so they are only an answer when the query
        // names this document or names none. Offering them under
        // another document's name showed the served view's own title
        // beside a name that document may not even have, and the row
        // could not be opened: reaching a view of another document is
        // the reach rule's business (docs/OmniSearch.md sec 6.4).
        if (!docName || doc === objs.doc || doc === objs.label) {
          if (has('ActiveView.', tail))
            rows.push({ key: prefix + 'ActiveView.', kind: 'member', title: 'ActiveView.',
                        desc: 'The view you are looking at', complete: prefix + 'ActiveView.' });
          for (const v of objs.views) {
            if (!has(v.name, tail)) continue;
            rows.push({ key: prefix + v.name + '.', kind: 'member', title: v.name + '.',
                        desc: v.title + (v.served ? ' (this view)' : ''),
                        complete: prefix + v.name + '.' });
          }
        }
        return { rows, total: rows.length };
      }
      const named = view === 'ActiveView' ? undefined : view;
      const rows = propRows(propsFor('view3d', doc, '', named), 'view3d', tail, prefix,
                            { doc, obj: '', scope: 'view3d', view: named });
      return { rows, total: rows.length };
    }

    // "#Box": this document's Box
    if (q.startsWith('#')) q = q.slice(1);

    // ".Height": the selection
    if (q.startsWith('.')) {
      const sel = selection.filter((s) => s.obj);
      const first = sel[0];
      if (!first) return { rows: [], total: 0 };
      const names = sel.map((s) => s.obj!);
      const objsArg = names.length > 1 ? names : undefined;
      let tail = q.slice(1);
      let viewObject = false;
      let prefix = '.';
      if (tail.startsWith('ViewObject.')) {
        viewObject = true;
        tail = tail.slice('ViewObject.'.length);
        prefix = '.ViewObject.';
      }
      if (tail.includes('.')) return { rows: [], total: 0 };
      const rows = objectMemberRows(first.doc ?? '', findObject(first.obj!), first.obj!,
                                    tail, prefix, viewObject, objsArg);
      return { rows, total: rows.length };
    }

    const dot = q.lastIndexOf('.');
    if (dot < 0) {
      // Objects by name or label
      const out: PathRow[] = [];
      let total = 0;
      for (const o of objs.objects) {
        if (!has(o.name, q) && !has(o.label ?? '', q)) continue;
        ++total;
        if (out.length >= limit) continue;
        out.push({
          key: o.name, kind: 'object', title: o.name,
          desc: `${o.label ? `<<${o.label}>>  ` : ''}${o.type}`,
          complete: o.name,
        });
      }
      return { rows: out, total };
    }

    // "Part.Box.<tail>", "Box.ViewObject.<tail>": the members of the
    // object the head's last component names
    const head = q.slice(0, dot);
    const tail = q.slice(dot + 1);
    const parts = head.split('.');
    let leaf = parts[parts.length - 1];
    let viewObject = false;
    if (leaf === 'ViewObject' && parts.length >= 2) {
      viewObject = true;
      leaf = parts[parts.length - 2];
    }
    const o = findObject(leaf);
    if (!o) return { rows: [], total: 0 };
    const rows = objectMemberRows(objs.doc, o, o.name, tail, head + '.', viewObject);
    return { rows, total: rows.length };
  };


  return { objects, ensureObjects, reset, findObject, objectMemberRows, rows };
}
