// A mirrored text field, and the expression dialog behind its fx button
// (docs/Sandbox.md 7.23).
//
// The field itself stays a field: it shows the host's value and writes it
// back, and it never becomes an expression editor. Expression entry is a
// DIALOG, as it is on the desktop -- an editor with room to type in, the
// completion list, and a live result under it -- because editing an
// expression in a spin box has nowhere to put the result, which is half of
// what makes the desktop's dialog usable.
//
// The dialog is OURS, drawn in the page. Raising the host's
// DlgExpressionInput would be modal on the desktop and freeze whoever is
// sitting at it (8.11: one shared session), and a phone is the worst place
// to render a mirrored Qt modal.

import { For, Show, createSignal, onCleanup, onMount } from 'solid-js';
import type { Accessor, JSX } from 'solid-js';
import { Portal } from 'solid-js/web';

import { NARROW, onKeyboardInset } from '../panel.ts';
import type { PanelClient } from './client.ts';
import { filterSet, inputModeFor, splice, stillApplies, triggerFor } from './complete.ts';
import type { CompletionSet, ExpressionPreview } from './complete.ts';
import type { WidgetModel } from './protocol.ts';

/// How long a word run waits before asking for completions. A dot asks at
/// once -- the user has just said "a name follows" -- while letters wait,
/// or every keystroke would be a round trip on a phone's network.
const DEBOUNCE_MS = 140;

/// The desktop's expression dialog previews on a 300ms timer; the same
/// pause here, for the same reason: it evaluates on the host.
const PREVIEW_MS = 300;

/// The classes whose value is a number, and whose phone keyboard is
/// therefore the decimal one.
const NUMERIC = new Set([
  'QuantitySpinBoxModel', 'InputFieldModel', 'QSpinBoxModel',
  'QDoubleSpinBoxModel', 'DoubleSpinBoxModel',
]);

function str(model: WidgetModel, key: string): string {
  const value = model.state[key];
  return typeof value === 'string' ? value : '';
}

export function Field(props: {
  w: WidgetModel;
  title: string | undefined;
  disabled: Accessor<boolean>;
  viewOnly: Accessor<boolean>;
  client: () => PanelClient | null;
  onValue: (raw: string) => void;
  /// Ask the CARD to open the expression dialog. Not opened here: this
  /// subtree is re-created whenever a store frame arrives (the panel
  /// keys each field on its model), so a dialog owned by a field is
  /// wiped mid-typing by any unrelated host update -- which a phone run
  /// caught, coming back with an empty editor.
  onExpression: (id: string, binding: string, expression: string) => void;
}): JSX.Element {
  const w = props.w;
  const numeric = NUMERIC.has(w.model);
  const bound = () => str(w, 'binding') !== '';
  const hasExpression = () => str(w, 'expression') !== '';

  return (
    <div class="fc-panel-fieldwrap">
      <input
        class="fc-panel-field"
        title={props.title}
        disabled={props.disabled()}
        value={str(w, 'text') || String(w.state.rawValue ?? '')}
        inputmode={inputModeFor('', numeric)}
        spellcheck={false}
        autocomplete="off"
        autocapitalize="off"
        autocorrect="off"
        enterkeyhint="done"
        onChange={(e) => props.onValue(e.currentTarget.value)}
      />
      <Show when={bound() && !props.viewOnly()}>
        <button
          class={hasExpression() ? 'fc-panel-fx fc-panel-fx-on' : 'fc-panel-fx'}
          title={hasExpression()
            ? `Bound to an expression: ${str(w, 'expression')}`
            : 'Enter an expression'}
          onClick={() => props.onExpression(w.id, str(w, 'binding'), str(w, 'expression'))}
        >
          fx
        </button>
      </Show>
    </div>
  );
}

/// The expression dialog: the editor, the completion list, the live result,
/// and OK / Discard -- the parts DlgExpressionInput has.
///
/// Rendered by the CARD, not by the field, and given plain values rather
/// than a model: what it is editing cannot change under it while someone
/// types, whatever the host pushes in the meantime.
export function ExpressionDialog(props: {
  id: string;
  binding: string;
  expression: string;
  client: () => PanelClient | null;
  onClose: () => void;
}): JSX.Element {
  const [text, setText] = createSignal(props.expression);
  const [items, setItems] = createSignal<string[]>([]);
  const [active, setActive] = createSignal(0);
  const [preview, setPreview] = createSignal<ExpressionPreview>(
    { result: '', severity: 'ok', message: '' });
  const [failed, setFailed] = createSignal('');
  const [inset, setInset] = createSignal(0);
  const [narrow, setNarrow] = createSignal(window.innerWidth <= NARROW);
  let set: CompletionSet | null = null;
  let askTimer: number | undefined;
  let previewTimer: number | undefined;
  let edit: HTMLTextAreaElement | undefined;

  const onResize = () => setNarrow(window.innerWidth <= NARROW);
  window.addEventListener('resize', onResize);
  const stopInset = onKeyboardInset(setInset);
  onCleanup(() => {
    window.removeEventListener('resize', onResize);
    stopInset();
    clearTimeout(askTimer);
    clearTimeout(previewTimer);
  });

  // Open with the caret in the editor: on a phone that is also what
  // raises the keyboard, and a tap saved matters more there.
  //
  // onMount, NOT an effect: an effect that reads text() re-runs on every
  // keystroke and puts the caret back at the end, so an edit in the
  // middle of an expression would be impossible to make.
  onMount(() => {
    if (!edit) return;
    edit.focus();
    const at = edit.value.length;
    edit.setSelectionRange(at, at);
  });

  const close = () => {
    setItems([]);
    setActive(0);
  };

  const ask = async (value: string, pos: number) => {
    const client = props.client();
    if (!client) return;
    try {
      const answered = await client.complete(props.id, value, pos);
      if (text() !== value) return;   // the text moved on: a stale answer
      set = answered;
      setItems(filterSet(answered, value, pos));
      setActive(0);
    }
    catch {
      close();
    }
  };

  const refreshPreview = async (value: string) => {
    const client = props.client();
    if (!client) return;
    try {
      const answer = await client.previewExpression(props.id, value);
      if (text() === value) setPreview(answer);
    }
    catch (e: any) {
      setPreview({ result: '', severity: 'error', message: String(e?.message ?? e?.code ?? '') });
    }
  };

  const onInput = (e: InputEvent & { currentTarget: HTMLTextAreaElement }) => {
    const value = e.currentTarget.value;
    const pos = e.currentTarget.selectionStart ?? value.length;
    setText(value);
    setFailed('');
    clearTimeout(askTimer);
    clearTimeout(previewTimer);
    previewTimer = window.setTimeout(() => void refreshPreview(value), PREVIEW_MS);

    // Between round trips the answered set is narrowed locally, which is
    // what keeps a phone to one request per dotted segment.
    if (set && stillApplies(set, value, pos)) {
      const narrowed = filterSet(set, value, pos);
      setItems(narrowed);
      setActive(0);
      if (narrowed.length) return;
      set = null;
    }
    else {
      set = null;
      close();
    }

    const why = triggerFor(value, pos);
    if (!why) return;
    if (why === 'dot') void ask(value, pos);
    else askTimer = window.setTimeout(() => void ask(value, pos), DEBOUNCE_MS);
  };

  const pick = (item: string) => {
    if (!set) return;
    const pos = edit?.selectionStart ?? text().length;
    const next = splice(set, item, text(), pos);
    setText(next.text);
    close();
    set = null;
    if (edit) {
      edit.value = next.text;
      edit.focus();
      edit.setSelectionRange(next.pos, next.pos);
    }
    clearTimeout(previewTimer);
    previewTimer = window.setTimeout(() => void refreshPreview(next.text), PREVIEW_MS);
  };

  const commit = async (value: string) => {
    const client = props.client();
    if (!client) return;
    try {
      await client.setExpression(props.id, value);
      props.onClose();
    }
    catch (e: any) {
      setFailed(String(e?.message ?? e?.code ?? 'the host refused it'));
    }
  };

  const onKey = (e: KeyboardEvent) => {
    const open = items().length > 0;
    if (e.key === 'Escape') {
      // The list first, the dialog second: Escape with a list up means
      // "not that", not "throw away what I typed".
      if (open) close();
      else props.onClose();
      return;
    }
    if (e.key === ' ' && e.ctrlKey) {
      e.preventDefault();
      void ask(text(), edit?.selectionStart ?? text().length);
      return;
    }
    if (!open) return;
    if (e.key === 'ArrowDown') {
      e.preventDefault();
      setActive((n) => (n + 1) % items().length);
    }
    else if (e.key === 'ArrowUp') {
      e.preventDefault();
      setActive((n) => (n - 1 + items().length) % items().length);
    }
    else if (e.key === 'Enter' || e.key === 'Tab') {
      // Enter takes the highlighted completion while the list is open.
      // With no list it is a newline: expressions can be multi-line, and
      // OK is how one is committed -- a phone has no Tab either way.
      e.preventDefault();
      pick(items()[active()]);
    }
  };

  /// The pointer is taken on pointerdown, not click: a tap that lets the
  /// editor blur closes the keyboard, the visual viewport grows back, and
  /// the chip moves out from under the finger before the click lands.
  const hold = (e: PointerEvent, item: string) => {
    e.preventDefault();
    pick(item);
  };

  const detail = (item: string) => {
    const at = set?.items.indexOf(item) ?? -1;
    return at >= 0 ? (set?.details[at] ?? '') : '';
  };

  const resultClass = () => {
    const s = preview().severity;
    return s === 'error' ? 'fc-expr-result fc-expr-bad'
      : s === 'warning' ? 'fc-expr-result fc-expr-warn' : 'fc-expr-result';
  };

  return (
    <div class="fc-expr-backdrop" onPointerDown={(e) => {
      if (e.target === e.currentTarget) props.onClose();
    }}>
      <div class="fc-expr">
        <div class="fc-expr-head">
          <span>Expression</span>
          <span class="fc-expr-bound">{props.binding}</span>
        </div>
        <textarea
          ref={edit}
          class="fc-expr-edit"
          rows={3}
          value={text()}
          placeholder="e.g. SketchPad.Constraints.Width * 2"
          inputmode="text"
          spellcheck={false}
          autocomplete="off"
          autocapitalize="off"
          autocorrect="off"
          onInput={onInput}
          onKeyDown={onKey}
        />

        <Show when={items().length > 0 && !narrow()}>
          <div class="fc-expr-suggest">
            <For each={items().slice(0, 12)}>
              {(item, i) => (
                <div
                  class={i() === active() ? 'fc-panel-sugg fc-panel-sugg-on' : 'fc-panel-sugg'}
                  title={detail(item)}
                  onPointerDown={(e) => hold(e, item)}
                >
                  {item}
                </div>
              )}
            </For>
          </div>
        </Show>

        <div class={resultClass()}>
          {preview().message || (preview().result ? `= ${preview().result}` : '')}
        </div>
        <Show when={failed()}>
          <div class="fc-panel-error">{failed()}</div>
        </Show>

        <div class="fc-expr-buttons">
          <Show when={props.expression}>
            <button class="fc-panel-btn" title="Remove the expression and keep the value"
                    onClick={() => void commit('')}>
              Discard
            </button>
          </Show>
          <button class="fc-panel-btn" onClick={props.onClose}>Cancel</button>
          <button class="fc-panel-btn fc-panel-ok"
                  disabled={text().trim() === '' || preview().severity === 'error'}
                  onClick={() => void commit(text())}>
            OK
          </button>
        </div>
      </div>

      {/* On a phone the list is a strip above the keyboard, where a thumb
          already is -- the dialog itself is behind the keyboard. */}
      <Show when={items().length > 0 && narrow()}>
        <div class="fc-panel-chips" style={{ bottom: `${inset()}px` }}>
          <For each={items().slice(0, 20)}>
            {(item, i) => (
              <button
                class={i() === active() ? 'fc-panel-chip fc-panel-chip-on' : 'fc-panel-chip'}
                title={detail(item)}
                onPointerDown={(e) => hold(e, item)}
              >
                {item.slice(item.lastIndexOf('.') + 1)}
              </button>
            )}
          </For>
        </div>
      </Show>
    </div>
  );
}
