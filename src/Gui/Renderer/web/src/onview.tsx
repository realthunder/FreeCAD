// The on-view parameters an edit mode has open, drawn over the canvas
// (docs/ThinClient.md sec 8.7).
//
// These are the small entry boxes the sketcher puts next to the cursor
// while a tool is running -- the width of a rectangle, the radius of a
// circle -- and they are the last piece of an edit mode that a browser
// could not show, because on the desktop each one is a Qt widget the tool
// parents to the 3D view.
//
// **Nothing about editing lives here.** What each box says, what is
// selected inside it, which one takes the keys and what a keystroke does
// to it are all decided on the server, by the same QuantitySpinBox and
// the same DrawSketchKeyboardManager rule the desktop uses. This layer
// draws the text it is given and forwards keystrokes back. That is the
// whole contract, and it is why the two tiers cannot drift: there is only
// one implementation of the behaviour, and it is not this one.
//
// Two things are the client's, both because they run at frame rate rather
// than at human rate: WHERE a box sits, projected from the world anchor
// the server sent with the camera of the frame being drawn (sec 8.7 --
// under the default uplink policy the server is not told where this
// client is looking between clicks at all), and which box has the DOM
// focus, so that a phone raises its keyboard.
import { For, Show, createEffect, createSignal } from 'solid-js';
import { sendOp } from './control';

/// One entry box as the server states it ('fc:onview').
export interface OnViewParam {
  i: number;
  /// The world anchor. Kept by the viewer, not used here -- the pixel
  /// position arrives separately, every frame it moves.
  x: number;
  y: number;
  z: number;
  /// The box's text, exactly as a desktop user would read it, units and
  /// locale decimal separator included.
  text: string;
  /// What selectNumber() selected over there: [start, length].
  sel: [number, number];
  /// Whether this box is the one taking the keys.
  focus: boolean;
  /// Whether the value has been fixed by the user rather than driven by
  /// the pointer. The desktop says it in the label colour; so do we.
  set: boolean;
}

/// Where one box goes this frame ('fc:onviewlayout'), in CSS pixels.
export interface OnViewPlace {
  i: number;
  x: number;
  y: number;
  visible: boolean;
}

/// Modifier bits as the 'E' input frame carries them (main.cpp inputMods).
function mods(e: KeyboardEvent): number {
  return (e.shiftKey ? 1 : 0) | (e.ctrlKey ? 2 : 0) | (e.altKey ? 4 : 0);
}

export function OnViewParams(props: {
  params: () => OnViewParam[];
  places: () => OnViewPlace[];
}) {
  const placeOf = (i: number) => props.places().find((p) => p.i === i);

  return (
    <div class="fc-onview" aria-hidden={props.params().length === 0}>
      <For each={props.params()}>
        {(param) => {
          const place = () => placeOf(param.i);
          return (
            <Show when={place()?.visible !== false}>
              <OnViewBox param={param} place={place} />
            </Show>
          );
        }}
      </For>
    </div>
  );
}

function OnViewBox(props: {
  param: OnViewParam;
  place: () => OnViewPlace | undefined;
}) {
  const [el, setEl] = createSignal<HTMLInputElement>();

  // The server decides which box has the keys; the DOM follows it. The
  // other way round -- focusing on click and telling the server after --
  // would let the two disagree for a round trip, and a keystroke landing
  // in that gap would go to the wrong tool parameter.
  createEffect(() => {
    const input = el();
    if (!input) return;
    if (props.param.focus && document.activeElement !== input) {
      input.focus({ preventScroll: true });
    }
    // Both the text and the selection are the server's: this is a
    // display of a value being edited elsewhere, not an editor.
    if (input.value !== props.param.text) input.value = props.param.text;
    if (props.param.focus) {
      const [start, length] = props.param.sel;
      input.setSelectionRange(start, start + length);
    }
  });

  const onKey = (e: KeyboardEvent, down: boolean) => {
    // Every key goes up, none is acted on here. Which of them the entry
    // box keeps and which fall through to the sketch is the server's
    // decision (DrawSketchKeyboardManager), so a client that filtered
    // would be a second copy of that rule, drifting.
    const sent = window.fcviewerSendKey?.(down, e.key, e.key.length === 1 ? e.key : '',
                                          mods(e));
    // Default-prevented whether or not it was sent: the box must not
    // edit its own text, because the text it shows is the server's and
    // a local edit would be overwritten by the next push anyway --
    // visibly, as a flicker.
    e.preventDefault();
    if (!sent && down) console.warn('fcviewer-ui: key not forwarded', e.key);
  };

  return (
    <input
      ref={setEl}
      class="fc-onview-box"
      classList={{ 'fc-onview-set': props.param.set }}
      type="text"
      inputmode="decimal"
      autocomplete="off"
      spellcheck={false}
      style={{
        left: `${props.place()?.x ?? 0}px`,
        top: `${props.place()?.y ?? 0}px`,
      }}
      onKeyDown={(e) => onKey(e, true)}
      onKeyUp={(e) => onKey(e, false)}
      onPointerDown={(e) => {
        // A tap moves the keys to this box -- the one thing about them
        // the client decides, and it still goes through the server so
        // that the tool's own focus tracking stays the authority.
        e.stopPropagation();
        sendOp('onViewFocus', { index: props.param.i }).catch(() => {});
      }}
    />
  );
}
