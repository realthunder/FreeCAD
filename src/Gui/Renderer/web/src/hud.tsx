// The renderer HUD as a panel of the chrome rather than a box the WASM
// side draws over everything: same card, same header, same drag, and a
// close button — on a phone it used to be permanent and in the way.
//
// The text is still the viewer's, verbatim: it is fixed-width by
// construction (main.cpp updateHud) so the panel never resizes as the
// numbers change, and it is the only thing that says what the renderer
// is doing on a device with no console.
import { Show, createSignal } from 'solid-js';
import { Pos, draggable, fitOnScreen, loadPos, posStyle } from './panel';

export function HudCard(props: { text: () => string | null;
                                 onClose: () => void }) {
  const [pos, setPos] = createSignal<Pos | null>(loadPos('fc.hud.card'));
  return (
    <Show when={props.text() !== null}>
      {(() => {
        let panel!: HTMLDivElement;
        return (
          <div
            class="fc-hud"
            role="dialog"
            aria-label="Renderer HUD"
            style={posStyle(pos())}
            ref={(el) => {
              panel = el;
              fitOnScreen(el, pos, setPos);
            }}
          >
            <div
              class="fc-head fc-drag"
              title="Drag to move"
              ref={(el) => draggable(el, () => panel, setPos, 'fc.hud.card')}
            >
              <div class="fc-title"><span class="fc-label">HUD</span></div>
              <button class="fc-close" onClick={() => props.onClose()}
                      aria-label="Close">×</button>
            </div>
            <pre class="fc-hud-body">{props.text()}</pre>
          </div>
        );
      })()}
    </Show>
  );
}
