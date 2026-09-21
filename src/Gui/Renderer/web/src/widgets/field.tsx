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

import { Show, createSignal, onCleanup, onMount } from 'solid-js';
import type { Accessor, JSX } from 'solid-js';
import { Portal } from 'solid-js/web';

import type { PanelClient } from './client.ts';
import { inputModeFor } from './complete.ts';
import type { ExpressionPreview } from './complete.ts';
import { CompleteButton, CompletionList, createCompletion } from './completion.tsx';
import type { WidgetModel } from './protocol.ts';

/// The desktop's expression dialog previews on a 300ms timer; the same
/// pause here, for the same reason: it evaluates on the host.
const PREVIEW_MS = 300;

/// The classes whose value is a number, and whose phone keyboard is
/// therefore the decimal one.
const NUMERIC = new Set([
  'QuantitySpinBoxModel', 'InputFieldModel', 'QSpinBoxModel',
  'QDoubleSpinBoxModel', 'DoubleSpinBoxModel',
]);

export function Field(props: {
  w: WidgetModel;
  title: string | undefined;
  /// The card's frame counter. Read by every state access below, because
  /// the store patches a model IN PLACE: without it the field shows what
  /// the host held when the panel was built and never what it holds now --
  /// the origin echo of a corrected write included, which is the one thing
  /// the host's own C++ was added for (docs/Sandbox.md 7.22).
  rev: Accessor<number>;
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
  const get = (key: string): string => {
    props.rev();
    const value = w.state[key];
    return typeof value === 'string' ? value : '';
  };
  /// The host's text, or the raw number when it sends none (a plain
  /// QDoubleSpinBox carries `value` and no `text`).
  const shown = () => {
    props.rev();
    return get('text') || String(w.state.rawValue ?? '');
  };
  const bound = () => get('binding') !== '';
  const hasExpression = () => get('expression') !== '';

  return (
    <div class="fc-panel-fieldwrap">
      <input
        class="fc-panel-field"
        title={props.title}
        disabled={props.disabled()}
        value={shown()}
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
            ? `Bound to an expression: ${get('expression')}`
            : 'Enter an expression'}
          onClick={() => props.onExpression(w.id, get('binding'), get('expression'))}
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
  const [preview, setPreview] = createSignal<ExpressionPreview>(
    { result: '', severity: 'ok', message: '' });
  const [failed, setFailed] = createSignal('');
  let previewTimer: number | undefined;
  let edit: HTMLTextAreaElement | undefined;

  onCleanup(() => clearTimeout(previewTimer));

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

  const schedulePreview = (value: string) => {
    clearTimeout(previewTimer);
    previewTimer = window.setTimeout(() => void refreshPreview(value), PREVIEW_MS);
  };

  /// Completion over the host's `widgets.complete` (docs/Sandbox.md
  /// 7.23), through the one controller and list (7.26).
  const c = createCompletion({
    source: () => {
      const client = props.client();
      return client ? (value, pos) => client.complete(props.id, value, pos) : null;
    },
    editor: {
      text,
      caret: () => edit?.selectionStart ?? text().length,
      apply: (value, pos) => {
        setText(value);
        if (edit) {
          edit.value = value;
          edit.focus();
          edit.setSelectionRange(pos, pos);
        }
      },
    },
    onPicked: schedulePreview,
  });

  const onInput = (e: InputEvent & { currentTarget: HTMLTextAreaElement }) => {
    setText(e.currentTarget.value);
    setFailed('');
    schedulePreview(e.currentTarget.value);
    c.onInput(e);
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
    // The list first, the dialog second: Escape with a list up means
    // "not that", not "throw away what I typed"; Enter with one up takes
    // the highlighted completion, with none it is a newline --
    // expressions can be multi-line, and OK is how one is committed.
    if (c.onKey(e)) return;
    if (e.key === 'Escape') props.onClose();
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
        <div class="fc-expr-editrow">
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
        <CompleteButton c={c} class="fc-panel-btn" />
        </div>

        <CompletionList c={c} class="fc-expr-complete" />

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

    </div>
  );
}
