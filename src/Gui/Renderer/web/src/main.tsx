// Entry: mount the DOM layer over the viewer canvas and feed it the
// viewer's client-local selection (the 'fc:selection' events the WASM
// side dispatches with resolved identity — docs/ThinClient.md §3).
import { render } from 'solid-js/web';
import { createSignal } from 'solid-js';
import { Inspector } from './inspector';
import { SheetPanel } from './sheet';
import { HudCard } from './hud';
import { LauncherMenu } from './menu';
import { LoupeOverlay } from './loupe';
import { OnViewParams } from './onview';
import type { OnViewParam, OnViewPlace } from './onview';
import { SplitOverlay } from './splitview';
import type { LoupeMark } from './loupe';
import { NARROW } from './panel';
import { sendOp } from './control';
import type { CyclesDevice, CyclesState, SelectionItem, Subject } from './control';
// Extraction only (cssCodeSplit: false emits it as web/inspector.css);
// the injection below is what actually loads it.
import './style.css';

// The stylesheet is injected from here rather than linked in
// shell.html: a <link rel="stylesheet"> pending on a slow connection
// blocks the whole page's first paint, and the canvas must never wait
// for the chrome. inspector.css sits beside this module (stable name
// via vite assetFileNames).
{
  const link = document.createElement('link');
  link.rel = 'stylesheet';
  link.href = new URL('inspector.css', import.meta.url).href;
  document.head.appendChild(link);
}

const [selection, setSelection] = createSignal<SelectionItem[]>([]);

window.addEventListener('fc:selection', (e: Event) => {
  const detail = (e as CustomEvent).detail;
  setSelection(Array.isArray(detail) ? detail : []);
});

// Claim the HUD feed before the viewer can paint its own box: with a UI
// layer loaded the HUD is a card in the chrome, not an overlay drawn on
// top of everything (main.cpp fcviewer_hud). Set outside the render so
// it holds even if mounting is delayed.
window.fcviewerHudCard = true;
const [hud, setHud] = createSignal<string | null>(null);
window.addEventListener('fc:hud', (e: Event) => {
  const d = (e as CustomEvent).detail;
  setHud(typeof d === 'string' ? d : null);
});

// The entry boxes an edit mode has open (docs/ThinClient.md sec 8.7).
// Two feeds because they change at different rates: what the boxes SAY
// arrives when the tool changes it, and where they SIT arrives from the
// viewer every frame it moves -- projected there, from the world anchor,
// with the camera of the frame being drawn.
const [onView, setOnView] = createSignal<OnViewParam[]>([]);
const [onViewPlaces, setOnViewPlaces] = createSignal<OnViewPlace[]>([]);
window.addEventListener('fc:onview', (e: Event) => {
  const d = (e as CustomEvent).detail;
  setOnView(Array.isArray(d) ? d as OnViewParam[] : []);
});
window.addEventListener('fc:onviewlayout', (e: Event) => {
  const d = (e as CustomEvent).detail;
  setOnViewPlaces(Array.isArray(d) ? d as OnViewPlace[] : []);
});

// Where the touch loupe is picking, for the mark drawn over the canvas.
const [loupe, setLoupe] = createSignal<LoupeMark | null>(null);
window.addEventListener('fc:loupe', (e: Event) => {
  const d = (e as CustomEvent).detail;
  setLoupe(d && typeof d.x === 'number' ? d as LoupeMark : null);
});

// The served-document listing (docs/MultiDocServe.md §6): pushed by the
// backend whenever a document is served or unserved, re-dispatched by
// the WASM side as 'fc:docs' with the viewer's current document filled
// in. The menu's document section draws from it.
interface DocEntry { name: string; label?: string; objects?: number }
const [docs, setDocs] = createSignal<{ list: DocEntry[]; current: string }>(
  { list: [], current: '' });
window.addEventListener('fc:docs', (e: Event) => {
  const d = (e as CustomEvent).detail;
  if (d && Array.isArray(d.list))
    setDocs({ list: d.list,
              current: typeof d.current === 'string' ? d.current : '' });
});
// The switch itself is the viewer's (it owns the socket and the reset);
// the current mark moves optimistically, because no docs push follows a
// switch — the listing did not change, this viewer's place in it did.
const switchDoc = (name: string) => {
  window.fcviewerSwitchDoc?.(name);
  setDocs((d) => ({ ...d, current: name }));
  // The split chrome re-keys its persisted layout on this -- it cannot
  // learn the switch from 'fc:docs', since no docs push follows one.
  window.dispatchEvent(new CustomEvent('fc:docswitch',
                                       { detail: { current: name } }));
};
// One document needs no section — unless it is not the one this viewer
// is on (an unknown ?doc= joined nothing, and the section is the way
// back in).
const docItems = () => {
  const d = docs();
  if (d.list.length < 2
      && !(d.list.length === 1 && d.list[0].name !== d.current))
    return [];
  return d.list.map((doc) => ({
    label: doc.label && doc.label !== doc.name
      ? `${doc.label} (${doc.name})` : doc.name,
    checked: () => docs().current === doc.name,
    closeOnSelect: true,
    onSelect: () => switchDoc(doc.name),
  }));
};

// This connection's access mode (docs/MultiDocServe.md §8): the host
// can restrict a viewer at any time. Seeded from the mirror on window,
// so a layer that mounted after the push still starts out right.
const [viewOnly, setViewOnly] = createSignal(!!window.fcviewerViewOnly);
window.addEventListener('fc:viewonly', (e: Event) => {
  setViewOnly(!!(e as CustomEvent).detail);
});

// The name the host's sharing roster shows for this connection
// (docs/MultiDocServe.md §6). ?client= wins at load; after that this is
// the only way to set one, and the viewer persists it per browser.
// Pushed by the viewer as 'fc:client' once it knows (?client=, the
// stored name, or the action below) — not read at mount, which races
// the wasm module's own startup.
const [clientName, setClientName] = createSignal(
  window.fcviewerClient ?? window.fcviewerClientName?.() ?? '');
window.addEventListener('fc:client', (e: Event) => {
  const d = (e as CustomEvent).detail;
  setClientName(typeof d === 'string' ? d : '');
});
const askName = () => {
  const now = clientName();
  const next = window.prompt(
    'Name this viewer for whoever is sharing the document', now);
  if (next === null) return;   // cancelled — not the same as cleared
  const name = next.trim();
  window.fcviewerSetClient?.(name);
  setClientName(name);
};

// The menu opens the property card on a subject; the counter is what
// makes asking twice work (see Inspector's request prop).
const [request, setRequest] = createSignal<{ subject: Subject; n: number }
                                          | null>(null);
let asks = 0;
const openCard = (subject: Subject) =>
  setRequest({ subject, n: ++asks });

// The card covers the menu's corner only as a bottom sheet, which is
// the narrow layout; anywhere else both are on screen at once.
const [cardOpen, setCardOpen] = createSignal(false);

// The spreadsheet panel (docs/SpreadsheetRemote.md sec 3). Opened from the
// launcher: a sheet has no geometry, so unlike every other card in this
// chrome it can never be reached by picking something in the view. `?sheet`
// opens it on load, so a link can point straight at the numbers -- and so a
// headless run can screenshot the panel, which no click can reach.
const [sheetOpen, setSheetOpen] = createSignal(
  new URLSearchParams(location.search).has('sheet'));

// The selection menu: mode (single/multi) and pick filter, pushed to
// the viewer as it changes (docs/ThinClientUI.md). Session-local on
// purpose — a filter someone forgot yesterday reads as broken picking
// today.
type SelMode = 'single' | 'multi';
const FILTERS = ['elements', 'object', 'face', 'edge', 'vertex'] as const;
type Filter = (typeof FILTERS)[number];
const [selMode, setSelMode] = createSignal<SelMode>('single');
const [filter, setFilter] = createSignal<Filter>('elements');
const pickMode = (m: SelMode) => {
  setSelMode(m);
  window.fcviewerSetSelMode?.(m === 'multi' ? 1 : 0);
};
const pickFilter = (f: Filter) => {
  setFilter(f);
  window.fcviewerSetPickFilter?.(FILTERS.indexOf(f));
};
const FILTER_LABELS: Record<Filter, string> = {
  elements: 'All elements', object: 'Whole object',
  face: 'Faces', edge: 'Edges', vertex: 'Vertices',
};

// The served viewport (docs/CyclesIntegration.md sec 7.1): the backend
// path traces a view on a device of its own and streams the frame. The
// devices are asked once the control lane is up (retried while it is
// not); the state is the viewer's, pushed as 'fc:cycles' per sub-view.
// This menu drives the single full-canvas view (cell 0); a split
// layout's cells carry their own control in the split chrome.
const OFF: CyclesState = { cell: 0, on: false, device: '', progress: 0,
                           status: '', error: '', width: 0, height: 0,
                           frames: 0 };
const [cycles, setCycles] = createSignal<CyclesState>(
  window.fcviewerCycles?.[0] ?? OFF);
window.addEventListener('fc:cycles', (e: Event) => {
  const d = (e as CustomEvent).detail;
  if (d && d.cell === 0 && typeof d.on === 'boolean') setCycles(d as CyclesState);
});
const [cyclesDevices, setCyclesDevices] = createSignal<CyclesDevice[] | null>(null);
const askCyclesDevices = () => {
  sendOp('cycles', { action: 'devices' })
    .then((r) => {
      const list: CyclesDevice[] = r.available && Array.isArray(r.devices)
        ? r.devices : [];
      // One entry per device TYPE: the op names a type, and two GPUs
      // of one kind are the engine's to choose between.
      const seen = new Set<string>();
      const devs = list.filter((d) => {
        if (seen.has(d.type)) return false;
        seen.add(d.type);
        return true;
      });
      setCyclesDevices(devs);
      // The split chrome's per-cell control reads the same list.
      window.fcviewerCyclesDevices = devs;
      window.dispatchEvent(new CustomEvent('fc:cyclesdevices', { detail: devs }));
    })
    .catch((err) => {
      // Offline or an old backend without the op: no section, and
      // ask again later only while the lane is down.
      if (err && err.code === 'Offline') setTimeout(askCyclesDevices, 3000);
      else setCyclesDevices([]);
    });
};
askCyclesDevices();
const cyclesItems = () => {
  const devs = cyclesDevices();
  if (!devs || devs.length === 0) return [];
  const c = cycles();
  const stateLabel = (type: string) => {
    if (!c.on || c.device !== type) return '';
    if (c.error) return ` (${c.error})`;
    return ` (${Math.round(c.progress * 100)}%)`;
  };
  return [
    { label: 'Path trace', header: true },
    { label: 'Off',
      checked: () => !cycles().on,
      onSelect: () => window.fcviewerSetCycles?.('', 0) },
    ...devs.map((d) => ({
      label: `${d.type}${stateLabel(d.type)}`,
      checked: () => cycles().on && cycles().device === d.type,
      onSelect: () => window.fcviewerSetCycles?.(d.type, 0),
    })),
  ];
};

const host = document.createElement('div');
host.id = 'fc-ui';
document.body.appendChild(host);

render(() => (
  <>
    <SplitOverlay />
    <Inspector selection={selection} request={request}
               onCardOpen={setCardOpen} viewOnly={viewOnly} />
    <SheetPanel open={sheetOpen} onClose={() => setSheetOpen(false)}
                viewOnly={viewOnly} doc={() => docs().current} />
    <LoupeOverlay mark={loupe} />
    <OnViewParams params={onView} places={onViewPlaces} />
    <HudCard text={hud} onClose={() => window.fcviewerSetHud?.(false)} />
    <LauncherMenu
      hidden={() => cardOpen() && window.innerWidth <= NARROW}
      items={[
        ...docItems(),
        { label: 'View & document properties',
          onSelect: () => openCard('viewdoc') },
        { label: clientName() ? `Name: ${clientName()}` : 'Set name…',
          onSelect: askName },
        { label: 'Spreadsheet',
          checked: () => sheetOpen(),
          onSelect: () => setSheetOpen(!sheetOpen()) },
        { label: 'HUD',
          checked: () => hud() !== null,
          onSelect: () => window.fcviewerSetHud?.(hud() === null) },
        ...cyclesItems(),
      ]}
    />
    <LauncherMenu
      hidden={() => cardOpen() && window.innerWidth <= NARROW}
      glyph={
        /* Cursor-arrow "select" icon, inline so every device draws the
           same thing (a text glyph already came out as tofu once). */
        <svg width="17" height="17" viewBox="0 0 24 24" fill="none"
             stroke="currentColor" stroke-width="2" stroke-linecap="round"
             stroke-linejoin="round" aria-hidden="true">
          <path d="M3 3l7.07 16.97 2.51-7.39 7.39-2.51L3 3z" />
          <path d="M13 13l6 6" />
        </svg>
      }
      title="Selection"
      class="fc-launcher-sel"
      items={[
        { label: 'Mode', header: true },
        ...(['single', 'multi'] as const).map((m) => ({
          label: m === 'single' ? 'Single' : 'Multi',
          checked: () => selMode() === m,
          onSelect: () => pickMode(m),
        })),
        { label: 'Filter', header: true },
        ...FILTERS.map((f) => ({
          label: FILTER_LABELS[f],
          checked: () => filter() === f,
          onSelect: () => pickFilter(f),
        })),
      ]}
    />
  </>
), host);

// Breadcrumbs for devices with no devtools: the viewer's ?log overlay
// mirrors the console, so these two lines are how a phone tells us the
// UI layer booted and saw a selection at all.
console.log('fcviewer-ui: mounted');
window.addEventListener('fc:selection', (e: Event) => {
  console.log('fcviewer-ui: selection '
      + JSON.stringify((e as CustomEvent).detail));
});
