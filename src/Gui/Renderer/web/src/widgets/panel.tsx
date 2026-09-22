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
// Scope is W1 to W4: the task panel root, the form leaves the corpus ranks,
// the item views as plain rows, and the dialog roots (`dialog:<n>`) as
// modal layers over the card.

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
import { iconKind, localeTag, mouseArgs, parseLocaleNumber, pictureAt, wheelArgs }
  from './images.ts';
import { CHECK_OFF, CHECK_ON, CHOOSER_DIRECTORY, ITEM_ENABLED, acceptFromFilter,
         cssColor, dialogClickOp, dialogRejectOp, fileSelectedOp, isDarkColor,
         isDialogRoot, itemEditOp, itemExpandOp, selectionWrite } from './protocol.ts';
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

/// Qt's mnemonic, removed: `&Yes` is a Yes with an underlined Y, and `&&`
/// is one literal ampersand. Nothing in the browser tier drives an access
/// key, so the marker is dropped rather than drawn -- a dialog button
/// reading "&Yes" is the bug this exists to prevent. One pass, so the
/// escape cannot eat the character after it.
function qtLabel(raw: string): string {
  return raw.replace(/&(.)/g, '$1');
}

/// The bag key a class really declares for its number, which is not one key
/// for all of them: a quantity field is a line edit underneath and carries
/// `rawValue` beside its `text` (8.4's typing case writes exactly that),
/// while a plain spin box declares `value` and no `text` at all
/// (`Fw::QSpinBox`, `Fw::QDoubleSpinBox`). Writing `rawValue` to one of
/// those sets a property it does not have, which the host accepts and the
/// desktop ignores -- W1 wrote one key for both and only the quantity
/// fields the corpus happens to be full of ever moved.
const RAW_VALUE = new Set(['QuantitySpinBoxModel', 'InputFieldModel']);
const PLAIN_VALUE = new Set(['QSpinBoxModel', 'QDoubleSpinBoxModel', 'DoubleSpinBoxModel']);

/// What a field writes back. Anything unparseable goes back as text, so a
/// half-typed expression is not silently turned into 0.
function valueWrite(model: WidgetModel, raw: string,
                    locale: string | null): Record<string, unknown> {
  const key = RAW_VALUE.has(model.model) ? 'rawValue'
    : PLAIN_VALUE.has(model.model) ? 'value' : '';
  if (key !== '' && raw.trim() !== '') {
    // Read back the way it is shown (W3): where the host's locale groups
    // with a point and decimates with a comma, a plain `Number()` of what
    // the field displays is NaN.
    const value = parseLocaleNumber(raw, locale);
    if (Number.isFinite(value)) return { [key]: value };
  }
  return { text: raw };
}

export function TaskPanelCard(props: {
  open: Accessor<boolean>;
  onClose: () => void;
  viewOnly: Accessor<boolean>;
  /// Open the chrome's search box. The card carries its OWN entry to it
  /// (the user's call 2026-09-22): with a card up on a handset the
  /// launcher is hidden and a phone has no `/` key, so from a mirrored
  /// panel search could not be reached at all -- found in the first
  /// handset session (docs/Sandbox.md 7.22).
  onSearch?: () => void;
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
  /// The scene socket is down.
  ///
  /// Separate from `failed`, which is about a subscribe that did not go
  /// through. A panel whose socket drops KEEPS its content -- it was true
  /// a moment ago, and throwing it away on a blip would be worse -- but
  /// it must not go on looking live, which is exactly what W5 measured it
  /// doing: the host was killed and restarted under an open card, the
  /// page's own `fc:connection` fired false and then true, and the card
  /// said nothing either time.
  const [offline, setOffline] = createSignal(false);
  /// The field whose expression is being edited, if any. Held HERE and
  /// not in the field, because a field's subtree is re-created on every
  /// store frame -- a dialog owned by one loses what is being typed the
  /// moment anything else in the panel changes, which is what a phone
  /// run showed: an editor that came back empty.
  const [expr, setExpr] =
    createSignal<{ id: string; binding: string; expression: string } | null>(null);
  /// The path each file chooser's own client last picked, by model id.
  ///
  /// Held HERE for the reason the expression dialog above is: a leaf's
  /// subtree is re-created whenever a store frame arrives, so state kept
  /// inside the view is wiped by any unrelated host update. Measured, not
  /// reasoned -- the first version of this kept the signal in
  /// `FileChooserView` and the drive still came back with an empty box
  /// beside a host label that had the path in it.
  ///
  /// It is needed at all because the host does not echo a change back to
  /// the connection that caused it, so the one client that picked the
  /// file is the only one not told the path (W4b).
  const [picked, setPicked] = createSignal<Record<string, string>>({});
  let client: PanelClient | null = null;
  let panelRef: HTMLDivElement | undefined;

  // The client is made on first open and let go on close: the host stops
  // the mirror with its last subscriber, so a closed card costs the
  // desktop nothing.
  const ensure = () => {
    if (client) return client;
    client = new PanelClient(() => setVersion((n) => n + 1));
    // The stream's figures where a drive can read them, the way the
    // console exposes its bridge's (console.tsx). This is the browser half
    // of 8.4's numbers, which until W5 had only ever been measured on the
    // host's side of the same wire.
    // The boot state rides along with the figures: the LOCALE the host
    // reported is otherwise invisible to a drive -- it is client state,
    // not DOM -- and W3 could only ever gate it, because the host says `C`
    // under the gate and under the harness alike.
    (window as unknown as Record<string, unknown>).fcxPanelStats =
      () => (client
        ? { ...client.stats, locale: client.boot.locale, theme: client.boot.theme }
        : null);
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
    onCleanup(onConnection((up) => {
      setOffline(!up);
      if (up) ask();
    }));
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

  // First paint (W5): the card has drawn panel content. Taken in a rAF
  // callback rather than in the effect body, so it lands AFTER the browser
  // painted this update -- what a person waited for is the pixels, not the
  // moment the data was ready.
  createEffect(() => {
    if (!rootId()) return;
    const c = client;
    if (!c || c.stats.paintedMs) return;
    c.markContent();
    if (typeof requestAnimationFrame === 'function') {
      requestAnimationFrame(() => c.markPainted());
    }
    else c.markPainted();
  });

  /// The dialog roots, in the order the desktop shows them (W4).
  ///
  /// The `panel` list container's layout IS that order -- the host re-sends
  /// it on every dialog open and close, which the corpus shows going from
  /// empty to one root to two and back -- so it is read from there and
  /// rides the same `update` as every other layout. `dialogIds` covers the
  /// window between a root's open and the list being re-laid.
  const dialogIds = createMemo(() => {
    version();
    const listed = planLayout(client?.store.get('panel')?.layout).items
      .map((it) => (it.kind === 'widget' ? it.id : undefined))
      .filter((id): id is string => !!id && isDialogRoot(id));
    for (const id of client?.dialogIds ?? []) {
      if (client?.store.has(id) && !listed.includes(id)) listed.push(id);
    }
    return listed;
  });

  /// The root a widget belongs to. Every mirrored widget carries its parent
  /// (the host sends one on every open), so this is a walk up rather than
  /// something every view has to thread down. Only a button box ever asks:
  /// a dialog root and the panel root are ANSWERED differently.
  const ownerRoot = (w: WidgetModel): string => {
    let at: WidgetModel | undefined = w;
    const seen = new Set<string>();
    while (at && !seen.has(at.id)) {
      seen.add(at.id);
      if (isDialogRoot(at.id) || at.id.startsWith('panel:')) return at.id;
      at = at.parent ? client?.store.get(at.parent) : undefined;
    }
    return rootId() ?? w.id;
  };

  const write = (id: string, values: Record<string, unknown>) => {
    if (props.viewOnly()) return;
    void client?.write(id, values).catch(() => {});
  };

  /// The host's locale, as the web spells it -- null for `C`, which means
  /// unformatted rather than English (W3). It arrives in the subscribe
  /// reply, so it is read through the version signal like any other state.
  const locale = (): string | null => {
    version();
    return localeTag(client?.boot.locale ?? 'C');
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

  /// A mirrored `Gui::FileChooser` (docs/Sandbox.md 7.22, W4b): the path
  /// the host holds, and a picker that is the BROWSER's own.
  ///
  /// The ruling of 2026-09-22 fixes the shape -- "never expose host file
  /// system to browser, but implement browser side file chooser to
  /// upload file to host" -- so nothing here lists, browses or reads the
  /// serving machine. The chooser's own `...` button is not even a model
  /// to click: the mirror sends a chooser as a LEAF and never walks into
  /// it (Gui/Fw/FwPanelMirror.cpp `isContainer`), which is what keeps the
  /// host's native file dialog off the desktop user's screen.
  ///
  /// The file goes up, the host names where it landed, and that path is
  /// announced as a PICK rather than written as a value -- see
  /// `fileSelectedOp` for why the difference decides whether the panel's
  /// slot runs at all.
  const FileChooserView = (p: { w: WidgetModel }): JSX.Element => {
    const w = p.w;
    const [busy, setBusy] = createSignal(false);
    const [failedUpload, setFailedUpload] = createSignal('');
    let picker: HTMLInputElement | undefined;
    const disabled = () => props.viewOnly() || st(w).enabled === false;
    // Qt's Directory mode asks for a folder ON THE HOST, which no browser
    // picker can answer -- and which is precisely what may not be
    // browsed. The button says so instead of pretending.
    const directory = () => num(w, 'mode') === CHOOSER_DIRECTORY;
    /// The host's own button text, unless it is Qt's bare `...`.
    ///
    /// On the desktop that ellipsis sits beside a line edit the user is
    /// already typing in, and it is conventional. Here it is the ONLY way
    /// into the picker -- there is no host dialog to fall back to -- so a
    /// panel that never set a label of its own gets a readable one. A
    /// panel that DID set one keeps it: that text is the desktop's.
    const browseLabel = () => {
      const text = str(w, 'buttonText');
      return text && text !== '...' ? text : 'Browse...';
    };
    const pick = (path: string) => {
      if (props.viewOnly() || !path) return;
      setPicked((all) => ({ ...all, [w.id]: path }));
      void client?.custom(w.id, fileSelectedOp(path)).catch(() => {});
    };
    const send = async (file: File | undefined): Promise<void> => {
      const c = client;
      if (!file || !c) return;
      setBusy(true);
      setFailedUpload('');
      try {
        const path = await c.upload(file.name, await file.arrayBuffer());
        if (!path) throw new Error('the host wrote nothing');
        pick(path);
      }
      catch (err) {
        const reason = (err as { message?: string })?.message ?? String(err);
        setFailedUpload(`Could not send ${file.name}: ${reason}`);
      }
      finally {
        setBusy(false);
        // Cleared, or choosing the SAME file again fires no change event
        // and the second attempt looks like a dead button.
        if (picker) picker.value = '';
      }
    };
    return (
      <div class="fc-panel-fieldwrap">
        <input class="fc-panel-field" title={str(w, 'toolTip') || undefined}
               disabled={disabled()} value={str(w, 'fileName') || picked()[w.id] || ''}
               spellcheck={false} autocomplete="off" autocapitalize="off"
               onChange={(e) => pick(e.currentTarget.value)} />
        <input type="file" class="fc-panel-filepick" ref={picker} tabindex={-1}
               accept={acceptFromFilter(str(w, 'filter')) || undefined}
               onChange={(e) => void send(e.currentTarget.files?.[0])} />
        <Show when={!props.viewOnly()}>
          <button class="fc-panel-btn fc-panel-browse"
                  disabled={disabled() || busy() || directory()}
                  title={directory()
                    ? 'This panel is asking for a folder on the machine running FreeCAD,'
                      + ' which a browser cannot choose'
                    : 'Choose a file on this device and send it to FreeCAD'}
                  onClick={() => picker?.click()}>
            {busy() ? 'Sending...' : browseLabel()}
          </button>
        </Show>
        <Show when={failedUpload()}>
          <div class="fc-panel-error">{failedUpload()}</div>
        </Show>
      </div>
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
            <Picture client={ensure} id={w.id} name={picture()}
                     viewOnly={props.viewOnly} />
          </Show>
        );
      }
      case 'QPushButtonModel':
      case 'QToolButtonModel': {
        const icon = () => str(w, 'icon');
        const label = () => str(w, 'text');
        return (
          <button class="fc-panel-btn" title={title()} disabled={disabled()}
                  onClick={() => void client?.custom(w.id, { event: 'click' })}>
            <Show when={iconKind(icon()) !== 'none'}>
              {/* An icon that cannot be drawn must not leave a blank
                  button: the placeholder comes back if the fetch fails,
                  but only where there is no label to show instead. */}
              <Icon client={ensure} name={icon()} size={16}
                    fallback={label() === '' ? <span>...</span> : undefined} />
            </Show>
            {/* W1 drew '...' for an icon-only button, because using the
                tool tip as a label put Pad's whole sentence -- "Temporary
                clear link references for new selection" -- into the row
                and ran it off the card. Now the icon IS the label, and the
                placeholder is left only for a button that has neither. */}
            <Show when={label() !== '' || iconKind(icon()) === 'none'}>
              <span>{label() || '...'}</span>
            </Show>
          </button>
        );
      }
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
                 viewOnly={props.viewOnly} client={() => client} locale={locale()}
                 onValue={(raw: string) => write(w.id, valueWrite(w, raw, locale()))}
                 onExpression={(id: string, binding: string, expression: string) =>
                   setExpr({ id, binding, expression })} />
        );
      case 'FileChooserModel':
        return <FileChooserView w={w} />;
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
      case 'QDialogButtonBoxModel': {
        // A DIALOG's box answers with the standard button flag through the
        // root: the host turns `clicked [flag]` into the window's
        // `done(button)`, which is exactly what a panel slot blocked in
        // `QMessageBox::exec()` gets back (7.19 M3). So the box's REAL
        // buttons are drawn -- Yes/No, Save/Discard/Cancel, whatever it was
        // built with -- because the flag each one carries IS the answer.
        //
        // The TASK PANEL's box keeps W1's accept/reject on the root: that
        // is the path the W1 and W2 screen proofs exercise, and nothing in
        // W4 needs it changed.
        const owner = () => ownerRoot(w);
        const buttons = () => planLayout(lay(w)).items
          .map((it) => (it.kind === 'widget' && it.id ? model(it.id) : undefined))
          .filter((b): b is WidgetModel => !!b && b.model === 'QPushButtonModel');
        return (
          <Show when={isDialogRoot(owner())}
                fallback={
                  <div class="fc-panel-buttons">
                    <button class="fc-panel-btn fc-panel-ok" disabled={props.viewOnly()}
                            onClick={() => void client?.custom(rootId() ?? w.id,
                                                               { event: 'accept' })}>
                      OK
                    </button>
                    <button class="fc-panel-btn"
                            onClick={() => void client?.custom(rootId() ?? w.id,
                                                               { event: 'reject' })}>
                      Cancel
                    </button>
                  </div>
                }>
            <div class="fc-panel-buttons">
              <For each={buttons()}>
                {(b: WidgetModel) => (
                  <button class="fc-panel-btn"
                          classList={{ 'fc-panel-ok': bool(b, 'default') }}
                          disabled={props.viewOnly() || st(b).enabled === false}
                          title={str(b, 'toolTip') || undefined}
                          onClick={() => {
                            const flag = num(b, 'standardButton');
                            // A custom button added to a box carries no
                            // standard flag, so there is nothing for the
                            // mirror to turn into `done(button)`: the
                            // button's own click is the honest op there.
                            void client?.custom(flag ? owner() : b.id,
                                                flag ? dialogClickOp(flag)
                                                     : { event: 'click' });
                          }}>
                    {qtLabel(str(b, 'text'))}
                  </button>
                )}
              </For>
            </div>
          </Show>
        );
      }
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

  /// A dialog root (`dialog:<n>`) as a layer over the card (W4).
  ///
  /// M3 mirrors ANY top-level QDialog the desktop raises -- a panel slot's
  /// QMessageBox, a non-native QFileDialog, a workbench's own -- so this
  /// draws the window the host walked rather than a message box in
  /// particular: a title bar from `windowTitle`, and a body that is the
  /// window's real layout through the same `View` every panel widget goes
  /// through. A QMessageBox comes out right because what arrives IS Qt's
  /// own grid, the icon label and text label and button box in it.
  ///
  /// There is no close button, deliberately. A dialog goes when the HOST
  /// says it does: the client asks -- a standard button, or Escape as
  /// `reject` -- and the root's close is the answer coming back. Dropping
  /// the layer on our own would leave the desktop user's slot still
  /// blocked in `exec()` with no window on either screen.
  const DialogLayer = (p: { id: string; depth: number }): JSX.Element => {
    const m = () => model(p.id);
    const topmost = () => {
      const up = dialogIds();
      return up.length === 0 || up[up.length - 1] === p.id;
    };
    // Escape answers the topmost dialog only, and is taken in the CAPTURE
    // phase: the omni box and the tool-bar menus both close on a stray
    // Escape from the document, and a dialog is in front of them.
    const onKey = (e: KeyboardEvent) => {
      if (e.key !== 'Escape' || !topmost()) return;
      e.stopPropagation();
      if (!props.viewOnly()) void client?.custom(p.id, dialogRejectOp());
    };
    document.addEventListener('keydown', onKey, true);
    onCleanup(() => document.removeEventListener('keydown', onKey, true));

    return (
      <Show when={m()} keyed>
        {(w: WidgetModel) => (
          <div class="fc-dlg-backdrop"
               classList={{ 'fc-dlg-modeless': !bool(w, 'modal') }}
               style={{ 'z-index': String(22 + p.depth) }}>
            <div class="fc-dlg" role="dialog" aria-modal={bool(w, 'modal')}>
              <div class="fc-dlg-head">{str(w, 'windowTitle') || 'Dialog'}</div>
              <div class="fc-dlg-body"><View id={p.id} /></div>
            </div>
          </div>
        )}
      </Show>
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
  /// Colours (`fg`/`bg`) ARE drawn, as of W5: the host packs a QColor as
  /// four floats 0..1 (`colorList`, FwQtView.cpp), and `cssColor` is that
  /// packing read off the host rather than the guess W2 refused to make.
  /// A panel colours a cell to mean something -- PartDesign's pick list
  /// paints an invalid feature red -- so dropping the colour drops the
  /// meaning.
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
      const bg = cssColor(cell.bg);
      const fg = cssColor(cell.fg);
      if (bg) {
        style['background'] = bg;
        // The wash belongs to the CELL, not the row: padded and rounded so
        // it reads as one rather than as a stripe the text sits on.
        style['border-radius'] = '3px';
        style['padding'] = '0 4px';
      }
      if (fg) style['color'] = fg;
      // A desktop palette is a LIGHT one and this card is dark, so a
      // background arriving on its own would leave the card's pale text on
      // a pale wash. Black or white by the wash's own luma is the one
      // choice that cannot come out unreadable.
      else if (bg) style['color'] = isDarkColor(cell.bg) ? '#f2f3f5' : '#16181c';
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
                      {/* A cell's decoration: Sketcher sends one per
                          element row, the same id on every row of a kind,
                          so the cache turns a list of them into one
                          fetch. */}
                      <Show when={iconKind(cell.icon) !== 'none'}>
                        <Icon client={ensure} name={cell.icon as string} size={14} />
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
          <Show when={props.onSearch}>
            <button class="fc-panel-search" title="Search  /"
                    onClick={() => props.onSearch?.()}>/</button>
          </Show>
          <button class="fc-panel-close" title="Close" onClick={props.onClose}>x</button>
        </div>
        {/* The socket is down: the content stays (it was true a moment
            ago) and the card says so, rather than going on looking live
            -- which is what it did through a measured host kill until
            W5. It clears itself: the connection event sets it both ways,
            and the re-subscribe behind it puts the panel back. */}
        <Show when={offline()}>
          <div class="fc-panel-offline">
            The viewer is offline -- this panel may be out of date.
          </div>
        </Show>
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
        {/* The dialog roots (W4), each its own layer, portalled for the
            reason above and stacked in the host's show order. A card that
            has no panel still draws them: a dialog is the desktop's, and
            it does not need a task panel to be up. */}
        <For each={dialogIds()}>
          {(id: string, i: () => number) => (
            <Portal>
              <DialogLayer id={id} depth={i()} />
            </Portal>
          )}
        </For>
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
function Picture(props: { client: () => PanelClient; id: string; name: string;
                         viewOnly: () => boolean }): JSX.Element {
  const [url, setUrl] = createSignal('');
  createEffect(() => {
    const name = props.name;
    let current = true;
    onCleanup(() => { current = false; });
    void props.client().image(name).then((src) => { if (current) setUrl(src ?? ''); })
      .catch(() => {});
  });

  /// A picture is display until here: there is no widget in the page to
  /// click, so the pointer goes back and the host replays it into the real
  /// widget as the desktop's own would have arrived (7.19 M3,
  /// `PanelMirror::replayMouse`). It is an `event`, and unlike an item
  /// view's that route really does reach the widget -- `commCustom` hands
  /// an event to `Widget::request`, which the mirror has connected.
  const send = (event: string, args: unknown[]) => {
    if (props.viewOnly()) return;
    void props.client().custom(props.id, { event, args }).catch(() => {});
  };
  /// The pointer in the PICTURE's pixels: the element is laid out at
  /// whatever width the card gives it, and the host knows only the image
  /// it sent.
  const at = (e: { clientX: number; clientY: number }, img: HTMLImageElement) =>
    pictureAt(img.getBoundingClientRect(),
              { width: img.naturalWidth, height: img.naturalHeight },
              e.clientX, e.clientY);

  return (
    <Show when={url()} keyed>
      {(src: string) => (
        <img class="fc-panel-pic" src={src} alt="" draggable={false}
             onPointerDown={(e) => {
               e.currentTarget.setPointerCapture(e.pointerId);
               const p = at(e, e.currentTarget);
               send('mouse', mouseArgs('press', p.x, p.y, e.button, e.buttons, e));
             }}
             onPointerUp={(e) => {
               const p = at(e, e.currentTarget);
               send('mouse', mouseArgs('release', p.x, p.y, e.button, e.buttons, e));
             }}
             onPointerMove={(e) => {
               // Only while a button is down. A bare hover would put one
               // message on the wire per pointer sample for a widget that
               // is usually only dragged, and the desktop's own hover is
               // not what a remote pointer is for.
               if (e.buttons === 0) return;
               const p = at(e, e.currentTarget);
               send('mouse', mouseArgs('move', p.x, p.y, e.button, e.buttons, e));
             }}
             onDblClick={(e) => {
               const p = at(e, e.currentTarget);
               send('mouse', mouseArgs('dblclick', p.x, p.y, e.button, e.buttons, e));
             }}
             onWheel={(e) => {
               const p = at(e, e.currentTarget);
               send('wheel', wheelArgs(p.x, p.y, e.deltaX, e.deltaY, e.buttons, e));
             }} />
      )}
    </Show>
  );
}

/// A picture the bag NAMES rather than carries: an `img:` id filed by the
/// host, or one of FreeCAD's own icon names. Both go through the client's
/// one cache, so the same icon on twenty rows is fetched once.
function Icon(props: { client: () => PanelClient; name: string; size?: number;
                      /// Drawn when the host cannot answer for this icon
                      /// (an evicted image, a name no theme has). Nothing
                      /// is drawn while the fetch is in flight: a
                      /// placeholder that flashed on every panel open
                      /// would be worse than a moment's gap.
                      fallback?: JSX.Element }): JSX.Element {
  const [url, setUrl] = createSignal<string | null>(null);
  const [failed, setFailed] = createSignal(false);
  const px = () => props.size ?? 16;
  createEffect(() => {
    const name = props.name;
    const size = px();
    let current = true;
    onCleanup(() => { current = false; });
    setUrl(null);
    setFailed(false);
    void props.client().picture(name, size)
      .then((src) => {
        if (!current) return;
        if (src) setUrl(src);
        else setFailed(true);
      })
      .catch(() => { if (current) setFailed(true); });
  });
  return (
    <Show when={url()} keyed fallback={<Show when={failed()}>{props.fallback}</Show>}>
      {(src: string) => (
        <img class="fc-panel-icon" src={src} alt="" draggable={false}
             width={px()} height={px()} />
      )}
    </Show>
  );
}

export { NARROW };
