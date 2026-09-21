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

import { onConnection } from '../control.ts';
import { PanelClient } from './client.ts';
import { ExpressionDialog, Field } from './field.tsx';
import { planLayout } from './layout.ts';
import type { LayoutPlan, PlannedItem } from './layout.ts';
import { CHECK_OFF, CHECK_ON, ITEM_ENABLED, itemEditOp, itemExpandOp, selectionWrite }
  from './protocol.ts';
import type { ItemCell, ItemRow, WidgetModel } from './protocol.ts';

const POS_KEY = 'fcviewer.taskpanel.pos';

/// Qt's Qt::Checked. The bag carries the tri-state as an int.
const CHECKED = 2;

/// A Qt label may carry RICH TEXT. Sketcher's selection hint is a whole
/// `<html><head/><body><p>&quot;Ctrl&quot;: multiple selection</p>...`
/// document, and drawing that as characters is what the first live run
/// put on screen. Two reasons it cannot simply be left alone: it reads as
/// markup soup, and as one `nowrap` line its min-content was ~900px, which
/// dragged the whole card out to 950px and left every list sized against a
/// width the card does not have.
///
/// Parsed, never injected. The string is the desktop's, so it is turned
/// into TEXT and the tags only decide where the newlines go; nothing here
/// puts host markup into the page's DOM.
function qtText(raw: string): string {
  if (!/^\s*<(?:!doctype|html|body|p|span|div|b|i|ul|table)\b/i.test(raw.trim())) return raw;
  try {
    const parsed = new DOMParser().parseFromString(raw, 'text/html');
    parsed.querySelectorAll('br').forEach((br) => br.replaceWith('\n'));
    parsed.querySelectorAll('p, div, li, tr, h1, h2, h3').forEach((el) => el.append('\n'));
    const text = (parsed.body.textContent || '').replace(/[ \t]+\n/g, '\n')
      .replace(/\n{2,}/g, '\n').trim();
    return text || raw;
  }
  catch {
    return raw;
  }
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

  /// EVERY read of a model goes through one of these, and every one of them
  /// tracks `version`.
  ///
  /// The store mutates its models IN PLACE -- an `update` patches `state`,
  /// an item op patches `items` -- so a read of `w.state.x` creates no
  /// dependency in Solid's graph, and a value computed in a component's
  /// body runs once and never again. The card therefore drew the snapshot
  /// it was built from and nothing after it: the client applied every
  /// frame, the wake fired, and the screen did not move. Measured rather
  /// than reasoned (docs/Sandbox.md 7.22): a second tab, watching while
  /// another wrote, RECEIVED both frames of the write -- `q_rawValue` then
  /// `q_text` -- and went on showing the old value.
  ///
  /// So a value that changes must be read inside a JSX expression (which
  /// the compiler makes a tracked getter), never hoisted into a `const`
  /// above the return.
  const st = (m: WidgetModel): Record<string, unknown> => {
    version();
    return m.state as Record<string, unknown>;
  };
  const str = (m: WidgetModel, key: string): string => {
    const value = st(m)[key];
    return typeof value === 'string' ? value : '';
  };
  const bool = (m: WidgetModel, key: string): boolean => st(m)[key] === true;
  const num = (m: WidgetModel, key: string): number => {
    const value = st(m)[key];
    return typeof value === 'number' ? value : 0;
  };
  /// A rebuilt layout rides an `update`, so the plan is re-read too.
  const lay = (m: WidgetModel) => {
    version();
    return m.layout;
  };
  const rows = (m: WidgetModel): ItemRow[] => {
    version();
    return m.items ?? [];
  };

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
  // the rule the sheet panel already keeps. The moment "now" arrives is
  // the connection event, so that is what re-asks (docs/Sandbox.md 7.27):
  // a timer polled 500 ms behind a page that was busy booting the viewer,
  // and this was the "first op unanswered" of 7.26 -- the op was never
  // sent. The same event covers a reconnect, whose new connection holds
  // no subscription whatever the old one had, the way the tool bar card
  // already re-asks. The slow timer stays for a viewer too old to send
  // the event, and for a reply lost on the way.
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
    let inflight = false;
    let retry = 0;
    onCleanup(() => { cancelled = true; clearTimeout(retry); });
    const ask = () => {
      if (cancelled || !props.open() || inflight) return;
      clearTimeout(retry);
      inflight = true;
      c.subscribe()
        .then(() => { if (!cancelled) setFailed(''); })
        .catch((err) => {
          if (cancelled) return;
          const code = String(err?.code ?? err);
          if (code === 'Offline' || code === 'Timeout') {
            setFailed('Waiting for the viewer...');
            retry = window.setTimeout(ask, code === 'Offline' ? 3000 : 500);
            return;
          }
          setFailed(`The panel stream is unavailable (${code})`);
        })
        .finally(() => { inflight = false; });
    };
    onCleanup(onConnection((up) => { if (up) ask(); }));
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
          <Show when={st(w).visible !== false}>
            {renderModel(w)}
          </Show>
        )}
      </Show>
    );
  };

  const renderModel = (w: WidgetModel): JSX.Element => {
    const disabled = () => props.viewOnly() || st(w).enabled === false;
    const title = () => str(w, 'toolTip') || undefined;

    switch (w.model) {
      case 'QLabelModel': {
        // Which of the two a label is can CHANGE while the panel is up (a
        // slot sets a pixmap on a label that held text), so the choice is
        // made per render rather than once on the way in.
        const picture = () => str(w, 'pixmap');
        const raw = () => str(w, 'text');
        const shown = () => qtText(raw());
        // A hint that was rich text, or that carries its own newlines, is
        // a paragraph and wraps; a plain form label stays on one line.
        return (
          <Show when={picture().startsWith('img:')}
                fallback={
                  <div class="fc-panel-label"
                       classList={{ 'fc-panel-rich':
                                    shown() !== raw() || shown().includes('\n') }}
                       title={title()}>{shown()}</div>
                }>
            <Picture client={ensure} name={picture()} />
          </Show>
        );
      }
      case 'QPushButtonModel':
      case 'QToolButtonModel':
        return (
          <button class="fc-panel-btn" title={title()} disabled={disabled()}
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
          <label class="fc-panel-check" title={title()}>
            <input type={w.model === 'QCheckBoxModel' ? 'checkbox' : 'radio'}
                   checked={bool(w, 'checked') || num(w, 'checkState') === CHECKED}
                   disabled={disabled()}
                   onChange={(e) => write(w.id, { checked: e.currentTarget.checked })} />
            <span>{str(w, 'text')}</span>
          </label>
        );
      case 'QComboBoxModel': {
        const items = (): unknown[] => {
          const value = st(w).items;
          return Array.isArray(value) ? (value as unknown[]) : [];
        };
        return (
          <select class="fc-panel-field" title={title()} disabled={disabled()}
                  onChange={(e) => write(w.id, { currentIndex: e.currentTarget.selectedIndex })}>
            <For each={items()}>
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
          <Field w={w} title={title()} rev={version} disabled={disabled}
                 viewOnly={props.viewOnly} client={() => client}
                 onValue={(raw: string) => write(w.id, valueWrite(w, raw))}
                 onExpression={(id: string, binding: string, expression: string) =>
                   setExpr({ id, binding, expression })} />
        );
      case 'QGroupBoxModel': {
        // Gui::TaskView::TaskBox is the panel's own box: same shape, and
        // its title is the header the desktop draws.
        const heading = () => str(w, 'title') || str(w, 'windowTitle');
        return (
          <section class="fc-panel-box">
            <Show when={heading()}><div class="fc-panel-box-head">{heading()}</div></Show>
            <Plan plan={planLayout(lay(w))} />
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
      case 'QListViewModel':
      case 'QTableViewModel':
        return <ItemsView w={w} />;
      default:
        // A container, or a class with no view yet: its layout still
        // renders, so an unfamiliar widget costs its own box and not the
        // panel.
        return <Plan plan={planLayout(lay(w))} />;
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

  /// An item view's rows (W2): the header, the nesting, the checks, the
  /// selection. Qt's own rules are kept rather than guessed at:
  ///
  ///   - a cell gets a check box only when the host sent `check` -- an
  ///     invalid QVariant there means "no check box", not an empty one;
  ///   - a row is dead when its flags clear Qt::ItemIsEnabled;
  ///   - children show only while the row is expanded, as a QTreeWidget
  ///     does, and a row arrives collapsed unless the host says otherwise;
  ///   - selection is STATE. The backend drives the desktop's real
  ///     selectionModel from `selection`/`currentId` and sends them back
  ///     when the desktop user selects, so a click here WRITES them. An
  ///     `itemClicked` would fire the panel's handlers with the selection
  ///     still where it was;
  ///   - a check and an expand go back as item OPS, never as events. Only
  ///     an op reaches the desktop's real widget -- see `ItemOp`.
  ///
  /// Colours (`fg`/`bg`) are deliberately not drawn: the host sends them as
  /// QVariantLists and guessing the packing would paint the wrong thing.
  const ItemsView = (p: { w: WidgetModel }): JSX.Element => {
    const columns = (): string[] => {
      const value = st(p.w).columns;
      return Array.isArray(value) ? (value as unknown[]).map(String) : [];
    };
    const colCount = () => Math.max(columns().length, num(p.w, 'columnCount'), 1);
    /// A LIST has no header. Qt's QListWidget and QListView draw none at
    /// all, but the model still carries `columns` -- those are
    /// QStandardItemModel's default labels, "1", "2", ... -- so trusting
    /// `columns` alone puts a column headed "1" over Sketcher's constraint
    /// list, which is exactly what the first live run drew.
    const headed = () => p.w.model !== 'QListWidgetModel'
      && p.w.model !== 'QListViewModel'
      && st(p.w).headerHidden !== true
      && columns().length > 0;
    const expandable = () => st(p.w).itemsExpandable !== false;
    const selected = (): number[] => {
      const value = st(p.w).selection;
      return Array.isArray(value) ? (value as unknown[]).map(Number) : [];
    };

    /// Header and rows share one grid, or the columns would not line up:
    /// a leading track for the twisty, then one per column.
    const gridStyle = (): JSX.CSSProperties => ({
      display: 'grid',
      'grid-template-columns': `14px repeat(${colCount()}, minmax(0, 1fr))`,
    });

    const cellStyle = (cell: ItemCell): JSX.CSSProperties => {
      const style: JSX.CSSProperties = {};
      if (cell.bold) style['font-weight'] = '600';
      // Qt::AlignRight, Qt::AlignHCenter
      if (cell.align && cell.align & 2) style['text-align'] = 'right';
      else if (cell.align && cell.align & 4) style['text-align'] = 'center';
      return style;
    };

    const pick = (row: ItemRow, column: number) => {
      if (props.viewOnly()) return;
      write(p.w.id, selectionWrite([row.id], row.id, column));
    };

    const toggleCheck = (row: ItemRow, column: number, on: boolean) => {
      if (props.viewOnly()) return;
      void client?.custom(p.w.id,
                          itemEditOp(row.id, column, { check: on ? CHECK_ON : CHECK_OFF }));
    };

    const toggleExpand = (row: ItemRow) => {
      // Applied here as well as sent. The host echoes the row op back, so
      // this is not the only thing that would move the arrow -- it is what
      // moves it NOW, rather than a round trip later.
      const open = row.expanded !== true;
      row.expanded = open;
      setVersion((n) => n + 1);
      void client?.custom(p.w.id, itemExpandOp(row.id, open));
    };

    const Level = (q: { rows: ItemRow[] }): JSX.Element => (
      <For each={q.rows.filter((row) => !row.hidden)}>
        {(row) => {
          const dead = () => row.flags !== undefined && !(row.flags & ITEM_ENABLED);
          const kids = () => row.children ?? [];
          const open = () => row.expanded === true;
          const on = () => selected().includes(row.id) || num(p.w, 'currentId') === row.id;
          return (
            <>
              <div class="fc-panel-row" style={gridStyle()}
                   classList={{ 'fc-panel-row-on': on(), 'fc-panel-row-off': dead() }}>
                <Show when={expandable() && kids().length > 0}
                      fallback={<span class="fc-panel-twisty" />}>
                  <button class="fc-panel-twisty" title={open() ? 'Collapse' : 'Expand'}
                          onClick={() => toggleExpand(row)}>{open() ? '-' : '+'}</button>
                </Show>
                <For each={row.cells.length ? row.cells : [{} as ItemCell]}>
                  {(cell, column) => (
                    <span style={cellStyle(cell)} title={cell.toolTip}
                          onClick={() => pick(row, column())}>
                      <Show when={cell.check !== undefined}>
                        <input type="checkbox" checked={cell.check === CHECK_ON}
                               disabled={props.viewOnly() || dead()}
                               onChange={(e) =>
                                 toggleCheck(row, column(), e.currentTarget.checked)} />
                      </Show>
                      {cell.text ?? ''}
                    </span>
                  )}
                </For>
              </div>
              <Show when={kids().length > 0 && open()}>
                <div class="fc-panel-kids"><Level rows={kids()} /></div>
              </Show>
            </>
          );
        }}
      </For>
    );

    return (
      <div class="fc-panel-rows">
        <Show when={headed()}>
          <div class="fc-panel-row fc-panel-head-row" style={gridStyle()}>
            <span class="fc-panel-twisty" />
            <For each={columns()}>{(label) => <span>{label}</span>}</For>
          </div>
        </Show>
        <Level rows={rows(p.w)} />
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

/// A custom-painted leaf: the host sent an `img:<sha1>`, fetched once.
///
/// The id is content-addressed, so a repaint that changed nothing keeps it
/// and a repaint that changed something sends a NEW one -- which is why
/// this follows `name` rather than fetching once at creation: the model is
/// patched in place, and a picture whose fetch was hoisted out of the
/// reactive graph would show the first frame for as long as the panel was
/// up.
function Picture(props: { client: () => PanelClient; name: string }): JSX.Element {
  const [url, setUrl] = createSignal('');
  createEffect(() => {
    const name = props.name;
    let current = true;
    onCleanup(() => { current = false; });
    void props.client().image(name).then((src) => { if (current) setUrl(src); })
      .catch(() => {});
  });
  return (
    <Show when={url()} keyed>
      {(src: string) => <img class="fc-panel-pic" src={src} alt="" />}
    </Show>
  );
}

export { NARROW };
