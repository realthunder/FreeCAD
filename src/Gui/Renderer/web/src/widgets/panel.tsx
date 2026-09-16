// The desktop's task panel, in the page (docs/Sandbox.md 7.22, G7, W1):
// the mirrored models rendered as DOM, and a write going back.
//
// A FLOATING card, ruled 2026-09-16 -- so it reuses the chrome's own panel
// behaviour (../panel.ts: drag with pointer capture, a remembered position,
// the bottom sheet under 640px) and the look of the cards beside it rather
// than imitating Qt. Chrome-flavoured was the other ruling: a near-miss of a
// Qt panel reads as broken, and the desktop's stylesheet does not travel.
//
// Everything below the socket is pure and gated in node (protocol.ts,
// layout.ts, `npm run gate`); this file is the part that cannot be, so it
// stays thin: it picks a view per model and places it by the plan, and
// holds no protocol knowledge of its own.
//
// Scope is W1: the task panel root, the form leaves the corpus ranks, the
// item views as plain rows. Dialog roots (`dialog:<n>`) are W4 and are
// deliberately not drawn here -- the client sees them, nothing renders them
// yet.

import { For, Show, createEffect, createMemo, createSignal, onCleanup } from 'solid-js';
import type { Accessor, JSX } from 'solid-js';

import { NARROW, draggable, fitOnScreen, loadPos, posStyle } from '../panel.ts';
import type { Pos } from '../panel.ts';
import { Portal } from 'solid-js/web';

import { PanelClient } from './client.ts';
import { ExpressionDialog, Field } from './field.tsx';
import { planLayout } from './layout.ts';
import type { LayoutPlan, PlannedItem } from './layout.ts';
import type { ItemRow, WidgetModel } from './protocol.ts';

const POS_KEY = 'fcviewer.taskpanel.pos';

/// Qt's Qt::Checked. The bag carries the tri-state as an int.
const CHECKED = 2;

function str(model: WidgetModel, key: string): string {
  const value = model.state[key];
  return typeof value === 'string' ? value : '';
}

function bool(model: WidgetModel, key: string): boolean {
  return model.state[key] === true;
}

function num(model: WidgetModel, key: string): number {
  const value = model.state[key];
  return typeof value === 'number' ? value : 0;
}

/// What a field writes back. A quantity and a spin box carry the number in
/// `rawValue` (8.4's typing case writes exactly that); a plain edit carries
/// `text`. Anything unparseable goes back as text, so a half-typed
/// expression is not silently turned into 0.
function valueWrite(model: WidgetModel, raw: string): Record<string, unknown> {
  const numeric = Number(raw);
  const wantsNumber = model.model === 'QuantitySpinBoxModel'
    || model.model === 'InputFieldModel'
    || model.model === 'QSpinBoxModel'
    || model.model === 'QDoubleSpinBoxModel'
    || model.model === 'DoubleSpinBoxModel';
  if (wantsNumber && raw.trim() !== '' && Number.isFinite(numeric)) {
    return { rawValue: numeric };
  }
  return { text: raw };
}

export function TaskPanelCard(props: {
  open: Accessor<boolean>;
  onClose: () => void;
  viewOnly: Accessor<boolean>;
}): JSX.Element {
  const [version, setVersion] = createSignal(0);
  const [pos, setPos] = createSignal<Pos | null>(loadPos(POS_KEY));
  const [failed, setFailed] = createSignal('');
  /// The field whose expression is being edited, if any. Held HERE and
  /// not in the field, because a field's subtree is re-created on every
  /// store frame -- a dialog owned by one loses what is being typed the
  /// moment anything else in the panel changes, which is what a phone
  /// run showed: an editor that came back empty.
  const [expr, setExpr] =
    createSignal<{ id: string; binding: string; expression: string } | null>(null);
  let client: PanelClient | null = null;
  let panelRef: HTMLDivElement | undefined;

  // The client is made on first open and let go on close: the host stops
  // the mirror with its last subscriber, so a closed card costs the
  // desktop nothing.
  const ensure = () => {
    if (client) return client;
    client = new PanelClient(() => setVersion((n) => n + 1));
    return client;
  };

  // The card can open BEFORE the socket is up: `?panel` opens it during page
  // load, and the launcher's entry can be clicked while the viewer is still
  // coming up. control.ts refuses an op on a down socket with 'Offline'
  // rather than queueing it, so a single subscribe at open time leaves a
  // dead card that only a reload clears -- which is what the first run
  // against a live serve showed. An 'Offline' here is "not yet", not "no",
  // the rule the sheet panel already keeps.
  createEffect(() => {
    if (!props.open()) {
      if (client) {
        void client.dispose();
        client = null;
        setVersion((n) => n + 1);
      }
      return;
    }
    const c = ensure();
    let cancelled = false;
    onCleanup(() => { cancelled = true; });
    const ask = () => {
      if (cancelled || !props.open()) return;
      c.subscribe()
        .then(() => { if (!cancelled) setFailed(''); })
        .catch((err) => {
          if (cancelled) return;
          const code = String(err?.code ?? err);
          if (code === 'Offline' || code === 'Timeout') {
            setFailed('Waiting for the viewer...');
            setTimeout(ask, 500);
            return;
          }
          setFailed(`The panel stream is unavailable (${code})`);
        });
    };
    ask();
  });

  onCleanup(() => {
    void client?.dispose();
    client = null;
  });

  const model = (id: string | undefined): WidgetModel | undefined => {
    version();
    return id ? client?.store.get(id) : undefined;
  };

  const rootId = createMemo(() => {
    version();
    return client?.panelId ?? null;
  });

  const write = (id: string, values: Record<string, unknown>) => {
    if (props.viewOnly()) return;
    void client?.write(id, values).catch(() => {});
  };

  /// One model, as the view its class asks for. `model` picks the view and
  /// `qtClass` refines it -- a picture is a QLabelModel whose qtClass is
  /// QSvgWidget, which is the one place the two disagree.
  const View = (p: { id: string }): JSX.Element => {
    const m = () => model(p.id);
    return (
      <Show when={m()} keyed>
        {(w: WidgetModel) => (
          <Show when={w.state.visible !== false}>
            {renderModel(w)}
          </Show>
        )}
      </Show>
    );
  };

  const renderModel = (w: WidgetModel): JSX.Element => {
    const disabled = () => props.viewOnly() || w.state.enabled === false;
    const title = str(w, 'toolTip') || undefined;

    switch (w.model) {
      case 'QLabelModel': {
        const picture = str(w, 'pixmap');
        if (picture.startsWith('img:')) return <Picture client={ensure} name={picture} />;
        return <div class="fc-panel-label" title={title}>{str(w, 'text')}</div>;
      }
      case 'QPushButtonModel':
      case 'QToolButtonModel':
        return (
          <button class="fc-panel-btn" title={title} disabled={disabled()}
                  onClick={() => void client?.custom(w.id, { event: 'click' })}>
            {/* An icon-only button's tooltip is a sentence -- Pad's is
                "Temporary clear link references for new selection" -- and
                using it as the label overruns the row it sits in, which is
                what the first run against a live panel drew. The icon is
                W3; until then the label is a placeholder and the sentence
                stays where it belongs, on the title. */}
            {str(w, 'text') || '...'}
          </button>
        );
      case 'QCheckBoxModel':
      case 'QRadioButtonModel':
        return (
          <label class="fc-panel-check" title={title}>
            <input type={w.model === 'QCheckBoxModel' ? 'checkbox' : 'radio'}
                   checked={bool(w, 'checked') || num(w, 'checkState') === CHECKED}
                   disabled={disabled()}
                   onChange={(e) => write(w.id, { checked: e.currentTarget.checked })} />
            <span>{str(w, 'text')}</span>
          </label>
        );
      case 'QComboBoxModel': {
        const items = Array.isArray(w.state.items) ? (w.state.items as unknown[]) : [];
        return (
          <select class="fc-panel-field" title={title} disabled={disabled()}
                  onChange={(e) => write(w.id, { currentIndex: e.currentTarget.selectedIndex })}>
            <For each={items}>
              {(item, index) => (
                <option selected={index() === num(w, 'currentIndex')}>{String(item)}</option>
              )}
            </For>
          </select>
        );
      }
      case 'QLineEditModel':
      case 'InputFieldModel':
      case 'QuantitySpinBoxModel':
      case 'QSpinBoxModel':
      case 'QDoubleSpinBoxModel':
      case 'DoubleSpinBoxModel':
        // A field carries its own completion and, when it is bound, its
        // own expression editor (docs/Sandbox.md 7.23).
        return (
          <Field w={w} title={title} disabled={disabled} viewOnly={props.viewOnly}
                 client={() => client}
                 onValue={(raw: string) => write(w.id, valueWrite(w, raw))}
                 onExpression={(id: string, binding: string, expression: string) =>
                   setExpr({ id, binding, expression })} />
        );
      case 'QGroupBoxModel': {
        // Gui::TaskView::TaskBox is the panel's own box: same shape, and
        // its title is the header the desktop draws.
        const heading = str(w, 'title') || str(w, 'windowTitle');
        return (
          <section class="fc-panel-box">
            <Show when={heading}><div class="fc-panel-box-head">{heading}</div></Show>
            <Plan plan={planLayout(w.layout)} />
          </section>
        );
      }
      case 'QDialogButtonBoxModel':
        return (
          <div class="fc-panel-buttons">
            <button class="fc-panel-btn fc-panel-ok" disabled={props.viewOnly()}
                    onClick={() => void client?.custom(rootId() ?? w.id, { event: 'accept' })}>
              OK
            </button>
            <button class="fc-panel-btn"
                    onClick={() => void client?.custom(rootId() ?? w.id, { event: 'reject' })}>
              Cancel
            </button>
          </div>
        );
      case 'QListWidgetModel':
      case 'QTreeWidgetModel':
      case 'QTreeViewModel':
      case 'QTableWidgetModel':
        return <Rows rows={w.items ?? []} />;
      default:
        // A container, or a class with no view yet: its layout still
        // renders, so an unfamiliar widget costs its own box and not the
        // panel.
        return <Plan plan={planLayout(w.layout)} />;
    }
  };

  /// A planned layout: a stack is flex, a grid and a form are CSS grid.
  /// The plan already defaulted the spans, so nothing here can produce a
  /// NaN track.
  const Plan = (p: { plan: LayoutPlan }): JSX.Element => {
    const grid = () => p.plan.kind !== 'stack';
    const style = (): JSX.CSSProperties => {
      const margins = p.plan.margins;
      const base: JSX.CSSProperties = {
        gap: `${p.plan.spacing ?? 6}px`,
        padding: margins
          ? `${margins[1]}px ${margins[2]}px ${margins[3]}px ${margins[0]}px`
          : undefined,
      };
      if (!grid()) {
        base.display = 'flex';
        base['flex-direction'] = p.plan.direction === 'row' ? 'row' : 'column';
        return base;
      }
      base.display = 'grid';
      base['grid-template-columns'] = `repeat(${Math.max(1, p.plan.columns ?? 1)}, auto)`;
      return base;
    };
    return (
      <div class={grid() ? 'fc-panel-grid' : 'fc-panel-stack'} style={style()}>
        <For each={p.plan.items}>{(item) => <Item item={item} grid={grid()} />}</For>
      </div>
    );
  };

  const Item = (p: { item: PlannedItem; grid: boolean }): JSX.Element => {
    const place = (): JSX.CSSProperties => {
      const it = p.item;
      if (!p.grid || it.row === undefined || it.column === undefined) {
        return it.value ? { flex: String(it.value) } : {};
      }
      return {
        'grid-row': `${it.row + 1} / span ${it.rowSpan ?? 1}`,
        'grid-column': `${it.column + 1} / span ${it.columnSpan ?? 1}`,
      };
    };
    return (
      <div class="fc-panel-cell" style={place()}>
        <Show when={p.item.kind === 'widget' && p.item.id}>
          <View id={p.item.id as string} />
        </Show>
        <Show when={p.item.kind === 'layout' && p.item.layout}>
          <Plan plan={p.item.layout as LayoutPlan} />
        </Show>
        <Show when={p.item.kind === 'separator'}><hr class="fc-panel-sep" /></Show>
        <Show when={p.item.kind === 'spacer' || p.item.kind === 'stretch'}>
          <div class="fc-panel-spacer" />
        </Show>
      </div>
    );
  };

  const title = () => {
    const root = model(rootId() ?? undefined);
    return root ? str(root, 'windowTitle') || 'Task panel' : 'Task panel';
  };

  return (
    <Show when={props.open()}>
      <div class="fc-panel" ref={panelRef} style={posStyle(pos())}>
        <div class="fc-panel-head"
             ref={(el: HTMLDivElement) => {
               draggable(el, () => panelRef as HTMLDivElement, setPos, POS_KEY);
               if (panelRef) fitOnScreen(panelRef, pos, setPos);
             }}>
          <div class="fc-panel-title">{title()}</div>
          <button class="fc-panel-close" title="Close" onClick={props.onClose}>x</button>
        </div>
        <div class="fc-panel-body">
          <Show when={failed()}>
            <div class="fc-panel-empty">{failed()}</div>
          </Show>
          <Show when={!failed() && !rootId()}>
            <div class="fc-panel-empty">No task panel is open on the desktop.</div>
          </Show>
          <Show when={rootId()} keyed>
            {(id: string) => <View id={id} />}
          </Show>
        </div>
        {/* Portalled to the body: .fc-panel carries a backdrop-filter,
            and a filtered ancestor is the containing block for
            position:fixed descendants -- rendered in place, the dialog
            is trapped inside the card with no full-screen dim and no
            way to click outside it. Where it sits in this tree does not
            matter to the DOM, only that it is outside the field
            subtrees the store re-creates. */}
        <Show when={expr()} keyed>
          {(e: { id: string; binding: string; expression: string }) => (
            <Portal>
              <ExpressionDialog id={e.id} binding={e.binding} expression={e.expression}
                                client={() => client} onClose={() => setExpr(null)} />
            </Portal>
          )}
        </Show>
      </div>
    </Show>
  );
}

/// An item view's rows, flat: W1 shows what the desktop holds, W2 makes the
/// checks and the nesting work.
function Rows(props: { rows: ItemRow[] }): JSX.Element {
  return (
    <div class="fc-panel-rows">
      <For each={props.rows}>
        {(row) => (
          <>
            <div class="fc-panel-row">
              <For each={row.cells}>{(cell) => <span>{cell.text ?? ''}</span>}</For>
            </div>
            <Show when={row.children?.length}>
              <div class="fc-panel-kids"><Rows rows={row.children as ItemRow[]} /></div>
            </Show>
          </>
        )}
      </For>
    </div>
  );
}

/// A custom-painted leaf: the host sent an `img:<sha1>`, fetched once.
function Picture(props: { client: () => PanelClient; name: string }): JSX.Element {
  const [url, setUrl] = createSignal('');
  void props.client().image(props.name).then(setUrl).catch(() => {});
  return (
    <Show when={url()} keyed>
      {(src: string) => <img class="fc-panel-pic" src={src} alt="" />}
    </Show>
  );
}

export { NARROW };
