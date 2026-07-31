// Entry: mount the DOM layer over the viewer canvas and feed it the
// viewer's client-local selection (the 'fc:selection' events the WASM
// side dispatches with resolved identity — docs/ThinClient.md §3).
import { render } from 'solid-js/web';
import { createSignal } from 'solid-js';
import { Inspector } from './inspector';
import type { SelectionItem } from './control';
import './style.css';

const [selection, setSelection] = createSignal<SelectionItem[]>([]);

window.addEventListener('fc:selection', (e: Event) => {
  const detail = (e as CustomEvent).detail;
  setSelection(Array.isArray(detail) ? detail : []);
});

const host = document.createElement('div');
host.id = 'fc-ui';
document.body.appendChild(host);

render(() => <Inspector selection={selection} />, host);
