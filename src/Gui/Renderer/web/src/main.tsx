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
               onCardOpen={setCardOpen} />
    <LoupeOverlay mark={loupe} />
    <HudCard text={hud} onClose={() => window.fcviewerSetHud?.(false)} />
    <LauncherMenu
      hidden={() => cardOpen() && window.innerWidth <= NARROW}
      items={[
        { label: 'View properties', onSelect: () => openCard('view3d') },
        { label: 'Document properties',
          onSelect: () => openCard('document') },
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
