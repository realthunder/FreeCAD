// The on-view parameters an edit mode has open, drawn over the canvas
// (docs/ThinClient.md sec 8.7).
//
// These are the small entry boxes the sketcher puts next to the cursor
// while a tool is running -- the width of a rectangle, the radius of a
// circle -- and they are the last piece of an edit mode a browser could
// not show, because on the desktop each one is a Qt widget the tool
// parents to the 3D view.
//
// **Nothing about editing lives here.** What each box says, what is
// selected inside it, which one takes the keys and what a keystroke does
// to it are all decided on the server, by the same QuantitySpinBox and
// the same DrawSketchKeyboardManager rule the desktop uses. This layer
// draws the text it is given and forwards keystrokes back. That is the
// whole contract, and it is why the two tiers cannot drift: there is one
// implementation of the behaviour, and it is not this one.
//
// Two things are the client's, both because they run at frame rate
// rather than at human rate: WHERE a box sits, projected by the viewer
// from the world anchor the server sent with the camera of the frame
// being drawn (under the default uplink policy the server is not told
// where this client is looking between clicks at all), and which box has
// the DOM focus, so that a phone raises its keyboard.
import { For, Index, Show, createEffect, createMemo, createSignal, on, onCleanup } from 'solid-js';
import { sendOp } from './control';
import { createPathCompleter } from './pathcomplete';
import type { PathRow } from './pathcomplete';

/// One entry box as the server states it ('fc:onview').
export interface OnViewParam {
  i: number;
  /// The world anchor. Kept by the viewer, which projects it; the pixel
  /// position arrives separately, on every frame it moves.
  x: number;
  y: number;
  z: number;
  /// The box's text, exactly as a desktop user would read it -- units
  /// and locale decimal separator included.
  text: string;
  /// What selectNumber() selected over there: [start, length].
  sel: [number, number];
  /// Whether this box is the one taking the keys.
  focus: boolean;
  /// Whether the value has been fixed by the user rather than driven by
  /// the pointer. The desktop says it in the label colour; so do we.
  set: boolean;

  /// 'datum' for the editor of a value the scene draws already -- a
  /// dimension's number (docs/SketcherPort.md "One editor for a
  /// constraint's value"); absent for a tool's parameter. The rest is
  /// that editor's.
  kind?: 'param' | 'datum';
  /// Which field takes the keys
  field?: 'value' | 'name';
  /// The line holds an expression: its text starts with '='
  expr?: boolean;
  /// The line under it: what the expression gives, or why nothing
  result?: string;
  /// 0 plain, 1 a value, 2 a warning, 3 an error
  level?: number;
  /// -1 no toggle, 0 a reference, 1 driving
  driving?: number;
  /// A value that can be stated in two measures (a radius, a diameter): -1
  /// for none, else which one it is stated in now, and that measure's name
  measure?: number;
  measureName?: string;
  nameShown?: boolean;
  name?: string;
  nameSel?: [number, number];
  /// The object an expression is written in, whose properties complete
  /// without a prefix
  obj?: string;
}

/// Where one box goes this frame ('fc:onviewlayout'), in CSS pixels.
export interface OnViewPlace {
  i: number;
  x: number;
  y: number;
  visible: boolean;
  /// Where the point one unit away from the anchor lands: the direction
  /// a value's editor grows its other rows in
  ax?: number;
  ay?: number;
}

/// Modifier bits as the 'E' input frame carries them (main.cpp inputMods).
function mods(e: KeyboardEvent): number {
  return (e.shiftKey ? 1 : 0) | (e.ctrlKey ? 2 : 0) | (e.altKey ? 4 : 0);
}

export function OnViewParams(props: {
  params: () => OnViewParam[];
  places: () => OnViewPlace[];
}) {
  return (
    <div class="fc-onview">
      {/* Index, not For: For is keyed by item REFERENCE, and every push
          from the server is a fresh array of fresh objects, so it would
          tear down and rebuild every input each time -- taking the DOM
          focus with it, mid-word, several times a second while a tool is
          being driven. The set is positional (the server's `i` is the
          box's place in the tool's own set), which is exactly what Index
          keys on. */}
      <Index each={props.params()}>
        {(param) => {
          const place = () =>
              props.places().find((p) => p.i === param().i);
          return (
            <Show when={place()?.visible !== false}>
              <Show when={param().kind === 'datum'}
                    fallback={<OnViewBox param={param} place={place} />}>
                <OnViewDatumEditor param={param} place={place} />
              </Show>
            </Show>
          );
        }}
      </Index>
    </div>
  );
}

function OnViewBox(props: {
  param: () => OnViewParam;
  place: () => OnViewPlace | undefined;
}) {
  const [el, setEl] = createSignal<HTMLInputElement>();

  // The server decides which box has the keys; the DOM follows it. The
  // other way round -- focusing on click and telling the server after --
  // would let the two disagree for a round trip, and a keystroke landing
  // in that gap would go to the wrong tool parameter.
  createEffect(() => {
    const input = el();
    const param = props.param();
    if (!input) return;
    if (param.focus && document.activeElement !== input) {
      input.focus({ preventScroll: true });
    }
    // Both the text and the selection are the server's: this is a
    // display of a value being edited elsewhere, not an editor.
    if (input.value !== param.text) input.value = param.text;
    if (param.focus && Array.isArray(param.sel)) {
      const [start, length] = param.sel;
      try { input.setSelectionRange(start, start + length); }
      catch (e) { /* a value shorter than the range the server sent */ }
    }
  });

  const onKey = (e: KeyboardEvent, down: boolean) => {
    // Every key goes up, none is acted on here. Which of them the entry
    // box keeps and which fall through to the sketch is the server's
    // decision (DrawSketchKeyboardManager), so a client that filtered
    // would be a second copy of that rule, drifting.
    const sent = window.fcviewerSendKey?.(
        down, e.key, e.key.length === 1 ? e.key : '', mods(e));
    // Default-prevented whether or not it was sent: the box must not
    // edit its own text, because the text it shows is the server's and a
    // local edit would be overwritten by the next push -- visibly, as a
    // flicker, and wrongly in between.
    e.preventDefault();
    if (!sent && down) console.warn('fcviewer-ui: key not forwarded', e.key);
  };

  return (
    <input
      ref={setEl}
      class="fc-onview-box"
      classList={{ 'fc-onview-set': props.param().set }}
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
        sendOp('onViewFocus', { index: props.param().i }).catch(() => {});
      }}
    />
  );
}

// ---- a value's editor ------------------------------------------------------

/// The identifier path that ends at the caret: what a completion replaces.
/// Letters, digits, '_', '.', and the '<<label>>' and 'doc#' forms.
function pathAtCaret(text: string, caret: number): { start: number; token: string } {
  let start = caret;
  while (start > 1 && /[A-Za-z0-9_.#<>]/.test(text[start - 1])) --start;
  return { start, token: text.slice(start, caret) };
}

/// The editor of a value the scene draws already: a dimension's number,
/// its expression, its reference toggle and its name. Like the boxes above
/// it is a display: the text, the selection, which field has the keys and
/// what a key does are the server's (DatumValueEditor). Two things are
/// this side's. Where it sits, projected every frame, with its other rows
/// on the side away from what the dimension measures. And the completion
/// list of an expression, which runs on names this client holds already
/// (pathcomplete.ts, the omni box's), so that no keystroke waits for a
/// round trip to offer a name; taking one sends the replacement up.
function OnViewDatumEditor(props: {
  param: () => OnViewParam;
  place: () => OnViewPlace | undefined;
}) {
  const [line, setLine] = createSignal<HTMLInputElement>();
  const [nameEl, setNameEl] = createSignal<HTMLInputElement>();
  const [root, setRoot] = createSignal<HTMLDivElement>();
  const [row, setRow] = createSignal<HTMLDivElement>();
  const [hi, setHi] = createSignal(0);
  /// The token the list was dismissed for, until the text moves on
  const [dismissed, setDismissed] = createSignal<string | null>(null);
  const paths = createPathCompleter((e, what) =>
      console.warn('fcviewer-ui: completion', what, e?.code ?? e));
  onCleanup(() => paths.reset());

  const caret = () => {
    const sel = props.param().sel;
    return Array.isArray(sel) ? sel[0] + sel[1] : props.param().text.length;
  };

  // The completion list: an expression, the line has the keys, a name
  // being typed at the caret
  const completion = createMemo(() => {
    const param = props.param();
    if (!param.expr || param.field === 'name') return null;
    const at = pathAtCaret(param.text, caret());
    if (!at.token || at.token === dismissed()) return null;
    paths.ensureObjects();
    const objs = paths.objects();
    if (!objs) return null;
    // An expression's path: the object's own properties need no prefix, and
    // a property's name is followed by what it holds, as on the desktop
    const rows = paths.rows(at.token, [], 12, { self: param.obj }).rows;
    const shown = rows.filter((r) => r.complete !== undefined && r.complete !== at.token)
                      .slice(0, 12);
    return shown.length ? { start: at.start, token: at.token, rows: shown } : null;
  });
  createEffect(() => {
    const c = completion();
    if (!c || hi() >= c.rows.length) setHi(0);
  });
  // Another token is another list: the lit row is its first again. Through
  // a memo, as below: the list and the parameter are new objects at every
  // push, and only a value that changed is a reason.
  const token = createMemo(() => completion()?.token);
  createEffect(on(token, () => setHi(0), { defer: true }));
  // The editor names constraints, and it moves from one to the next: what
  // the sketch's Constraints holds is asked again for each
  const edited = createMemo(() => props.param().i);
  createEffect(on(edited, () => paths.forgetProperties(), { defer: true }));

  const take = (r: PathRow) => {
    const c = completion();
    if (!c || r.complete === undefined) return;
    sendOp('onViewAction', {
      index: props.param().i, action: 'replace',
      start: c.start, length: c.token.length, text: r.complete,
    }).catch(() => {});
  };

  // Text, selection and focus are the server's, for each field
  createEffect(() => {
    const param = props.param();
    const value = line();
    const name = nameEl();
    if (value && value.value !== param.text) value.value = param.text;
    if (name && name.value !== (param.name ?? '')) name.value = param.name ?? '';
    const target = param.field === 'name' ? name : value;
    const sel = param.field === 'name' ? param.nameSel : param.sel;
    if (target && document.activeElement !== target) target.focus({ preventScroll: true });
    if (target && Array.isArray(sel)) {
      try { target.setSelectionRange(sel[0], sel[0] + sel[1]); }
      catch (e) { /* shorter than the range */ }
    }
  });

  // Where it sits: the line over the number, the other rows away from
  // the geometry, flipped when they would leave the view, then clamped.
  const [pos, setPos] = createSignal({ x: 0, y: 0, above: false });
  createEffect(() => {
    const place = props.place();
    const el = root();
    const lineRow = row();
    props.param();
    completion();
    if (!place || !el || !lineRow) return;
    const dx = (place.ax ?? place.x) - place.x;
    const dy = (place.ay ?? place.y) - place.y;
    const len = Math.hypot(dx, dy);
    let above = len > 0 && dy < -0.3 * len;
    const host = el.offsetParent as HTMLElement | null;
    const W = host?.clientWidth ?? window.innerWidth;
    const H = host?.clientHeight ?? window.innerHeight;
    const layOut = (up: boolean) => {
      // measured in the order the rows will have
      el.classList.toggle('fc-onview-datum-above', up);
      const w = el.offsetWidth;
      const h = el.offsetHeight;
      const cx = lineRow.offsetLeft + lineRow.offsetWidth / 2;
      const cy = lineRow.offsetTop + lineRow.offsetHeight / 2;
      return { x: place.x - cx, y: place.y - cy, w, h };
    };
    let p = layOut(above);
    if (above && p.y < 0 && p.y + p.h <= H) { above = false; p = layOut(false); }
    else if (!above && p.y + p.h > H && p.y >= 0) { above = true; p = layOut(true); }
    setPos({
      x: Math.min(Math.max(p.x, 0), Math.max(0, W - p.w)),
      y: Math.min(Math.max(p.y, 0), Math.max(0, H - p.h)),
      above,
    });
  });

  const onKey = (e: KeyboardEvent, down: boolean) => {
    // The completion list is this side's, and so are its keys
    const c = completion();
    if (c && (e.key === 'ArrowDown' || e.key === 'ArrowUp' || e.key === 'Enter'
              || e.key === 'Tab' || e.key === 'Escape')) {
      e.preventDefault();
      if (!down) return;
      if (e.key === 'ArrowDown') setHi((hi() + 1) % c.rows.length);
      else if (e.key === 'ArrowUp') setHi((hi() + c.rows.length - 1) % c.rows.length);
      else if (e.key === 'Escape') setDismissed(c.token);
      else take(c.rows[hi()]);
      return;
    }
    if (down) setDismissed(null);
    // Everything else goes up, none of it acted on here (the boxes'
    // rule above): Ctrl+Shift+D, F2, Tab and Enter included.
    const sent = window.fcviewerSendKey?.(
        down, e.key, e.key.length === 1 ? e.key : '', mods(e));
    e.preventDefault();
    if (!sent && down) console.warn('fcviewer-ui: key not forwarded', e.key);
  };

  const field = (which: 'value' | 'name') => (e: PointerEvent) => {
    e.stopPropagation();
    sendOp('onViewAction', { index: props.param().i, action: 'field', text: which })
      .catch(() => {});
  };

  return (
    <div ref={setRoot}
         class="fc-onview-datum"
         classList={{ 'fc-onview-datum-above': pos().above }}
         style={{ left: `${pos().x}px`, top: `${pos().y}px` }}
         onPointerDown={(e) => e.stopPropagation()}>
      <div ref={setRow} class="fc-onview-datum-line">
        <Show when={(props.param().driving ?? -1) >= 0}>
          <button type="button"
                  class="fc-onview-datum-toggle"
                  classList={{ 'fc-onview-datum-reference': props.param().driving === 0 }}
                  title="Driving or reference (Ctrl+Shift+D)"
                  tabIndex={-1}
                  onPointerDown={(e) => {
                    e.stopPropagation();
                    e.preventDefault();
                    sendOp('onViewAction', { index: props.param().i, action: 'toggle' })
                      .catch(() => {});
                  }}>
            {props.param().driving === 0 ? 'ref' : 'drv'}
          </button>
        </Show>
        <Show when={(props.param().measure ?? -1) >= 0}>
          <button type="button"
                  class="fc-onview-datum-toggle fc-onview-datum-measure"
                  title={`${props.param().measureName ?? ''}: switch measure (Ctrl+Shift+R)`}
                  tabIndex={-1}
                  onPointerDown={(e) => {
                    e.stopPropagation();
                    e.preventDefault();
                    sendOp('onViewAction', { index: props.param().i, action: 'measure' })
                      .catch(() => {});
                  }}>
            {props.param().measureName ?? ''}
          </button>
        </Show>
        <input ref={setLine}
               class="fc-onview-box fc-onview-datum-value"
               classList={{ 'fc-onview-set': props.param().set,
                            'fc-onview-datum-measured': props.param().driving === 0 }}
               type="text"
               inputmode={props.param().expr ? 'text' : 'decimal'}
               autocomplete="off"
               spellcheck={false}
               onPointerDown={field('value')}
               onKeyDown={(e) => onKey(e, true)}
               onKeyUp={(e) => onKey(e, false)} />
      </div>
      <Show when={completion()}>
        {(c) => (
          <div class="fc-onview-datum-complete">
            <For each={c().rows}>
              {(r, k) => (
                <div class="fc-onview-datum-row"
                     classList={{ 'fc-onview-datum-hi': k() === hi() }}
                     onPointerDown={(e) => { e.stopPropagation(); e.preventDefault(); take(r); }}>
                  <span>{r.title}</span>
                  <Show when={r.desc}><span class="fc-onview-datum-desc">{r.desc}</span></Show>
                </div>
              )}
            </For>
          </div>
        )}
      </Show>
      <Show when={props.param().result}>
        <div class="fc-onview-datum-result"
             classList={{ 'fc-onview-datum-log': props.param().level === 1,
                          'fc-onview-datum-warning': props.param().level === 2,
                          'fc-onview-datum-error': props.param().level === 3 }}>
          {props.param().result}
        </div>
      </Show>
      <Show when={props.param().nameShown}>
        <input ref={setNameEl}
               class="fc-onview-datum-name"
               type="text"
               placeholder="Name"
               autocomplete="off"
               spellcheck={false}
               onPointerDown={field('name')}
               onKeyDown={(e) => onKey(e, true)}
               onKeyUp={(e) => onKey(e, false)} />
      </Show>
    </div>
  );
}
