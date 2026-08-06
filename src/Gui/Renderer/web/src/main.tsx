// Entry: mount the DOM layer over the viewer canvas and feed it the
// viewer's client-local selection (the 'fc:selection' events the WASM
// side dispatches with resolved identity — docs/ThinClient.md §3).
import { render } from 'solid-js/web';
import { createSignal } from 'solid-js';
import { Inspector } from './inspector';
import { HudCard } from './hud';
import { LauncherMenu } from './menu';
import { LoupeOverlay } from './loupe';
import type { LoupeMark } from './loupe';
import { NARROW } from './panel';
import type { SelectionItem, Subject } from './control';
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
const [clientName, setClientName] = createSignal(
  window.fcviewerClientName?.() ?? '');
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

const host = document.createElement('div');
host.id = 'fc-ui';
document.body.appendChild(host);

render(() => (
  <>
    <Inspector selection={selection} request={request}
               onCardOpen={setCardOpen} viewOnly={viewOnly} />
    <LoupeOverlay mark={loupe} />
    <HudCard text={hud} onClose={() => window.fcviewerSetHud?.(false)} />
    <LauncherMenu
      hidden={() => cardOpen() && window.innerWidth <= NARROW}
      items={[
        ...docItems(),
        { label: 'View & document properties',
          onSelect: () => openCard('viewdoc') },
        { label: clientName() ? `Name: ${clientName()}` : 'Set name…',
          onSelect: askName },
        { label: 'HUD',
          checked: () => hud() !== null,
          onSelect: () => window.fcviewerSetHud?.(hud() === null) },
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
