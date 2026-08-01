// Entry: mount the DOM layer over the viewer canvas and feed it the
// viewer's client-local selection (the 'fc:selection' events the WASM
// side dispatches with resolved identity — docs/ThinClient.md §3).
import { render } from 'solid-js/web';
import { createSignal } from 'solid-js';
import { Inspector } from './inspector';
import type { SelectionItem } from './control';
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

const host = document.createElement('div');
host.id = 'fc-ui';
document.body.appendChild(host);

render(() => <Inspector selection={selection} />, host);
