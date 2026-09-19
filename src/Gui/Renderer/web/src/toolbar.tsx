// The desktop's tool bars, streamed (docs/ThinClient.md 8.11 item 4).
//
// The server mirrors the live QToolBar / QAction set into widget models
// (docs/Sandbox.md 7.18) and pushes them over the control lane; this is
// the client that subscribes and draws them as DOM. It holds no command
// knowledge of its own: the bars, their order, a button's icon, text,
// enabled and checked state are all the desktop's, and change when the
// desktop's do -- a workbench switch, entering a sketch, a selection.
//
// A click is the `command` op with the model's command identity, NOT the
// model's own trigger: the op runs the tool in THIS client's view and is
// allowlisted on the server, which is what makes an enabled button here
// a promise the server keeps. Until dialogs mirror (8.11 item 5) only the
// commands that open none are enabled; every other button is shown, and
// says why it is not.
import { For, Show, createEffect, createMemo, createSignal, on, onCleanup,
         untrack } from 'solid-js';
import { createStore, reconcile } from 'solid-js/store';
import { isBrowserSafeCommand, onConnection, onPush, runTool, sendOp,
         stateCamera } from './control';

const REF = 'IPY_MODEL_';
const unref = (r: unknown): string | null =>
  typeof r === 'string' && r.startsWith(REF) ? r.slice(REF.length) : null;

interface LayoutItem { action?: string; widget?: string; separator?: boolean }

/// One mirrored object: a QToolBarModel, a QActionModel, or the
/// `toolbars` order object, as the stream last stated it.
interface WidgetObj {
  model: string;
  state: Record<string, any>;
  layout?: { items: LayoutItem[] };
}

/// Areas that are not the tool bar strip's to draw: the status bar and
/// the menu bar corners are chrome of a window this client has not got.
const skipArea = (area: unknown) =>
  area === 'statusbar' || (typeof area === 'string' && area.startsWith('menubar'));

const REFUSED_TIP = 'Runs from the desktop until dialogs are mirrored';

/// Whether this client is in an edit session ('fc:edit'). A button's
/// q_enabled is the DESKTOP's answer, taken against the desktop's active
/// document: for a sketch a browser edits in a document the desktop is
/// not looking at, every sketch tool reads disabled there while it runs
/// here. So a tool the server runs for this client is enabled while this
/// client edits, whatever the desktop says.
const [editing, setEditing] = createSignal(!!window.fcviewerEditing);
window.addEventListener('fc:edit', () => setEditing(!!window.fcviewerEditing));

/// Whether a click on a model can do anything for this client.
const runnable = (safe: boolean, st: Record<string, any>) =>
  safe && (st.q_enabled !== false || editing());

// ---- Icons ----------------------------------------------------------------

/// By (theme, name, pixel size): a name is the desktop's icon name, which
/// a theme maps to different bytes, and a PNG is fetched at the size it
/// is drawn. Promises, so two buttons asking at once make one request.
const iconCache = new Map<string, Promise<string | null>>();

function iconUrl(theme: string, name: string, px: number): Promise<string | null> {
  const key = `${theme}\n${name}\n${px}`;
  let p = iconCache.get(key);
  if (!p) {
    p = sendOp('widgets.icon', { name, size: px })
      .then((r) => {
        if (r.format === 'svg' && typeof r.data === 'string')
          return 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(r.data);
        if (r.format === 'png' && typeof r.data === 'string')
          return 'data:image/png;base64,' + r.data;
        return null;
      })
      .catch((err) => {
        // Offline is not an answer about the icon: forget it, ask again.
        if (err?.code === 'Offline' || err?.code === 'Timeout') iconCache.delete(key);
        return null;
      });
    iconCache.set(key, p);
  }
  return p;
}

/// Qt's mnemonic marker out of a label: "&Line" -> "Line", "&&" -> "&".
const plainText = (t: unknown) =>
  typeof t === 'string' ? t.replace(/&(&?)/g, '$1') : '';

// ---- The strip ------------------------------------------------------------

export function ToolbarStrip(props: {
  /// The launcher's switch.
  enabled: () => boolean;
  /// A view-only connection gets no tool bars: the server refuses the
  /// subscription, and there is nothing on them it could run.
  viewOnly: () => boolean;
  /// A host connection (docs/ShareAccess.md sec 2.2): not held to the
  /// browser allowlist, so nothing is drawn refused.
  host: () => boolean;
  /// The phone layout: one scrolling row along the bottom.
  narrow: () => boolean;
  /// The strip's height while it is shown on the top edge, 0 otherwise,
  /// so the panels anchored there can clear it.
  onTopInset?: (px: number) => void;
}) {
  const [objs, setObjs] = createStore<Record<string, WidgetObj>>({});
  const [theme, setTheme] = createSignal('');
  const [error, setError] = createSignal<string | null>(null);
  let errorTimer = 0;

  // Where the subscription stands on THIS connection. `held` is the
  // server's side of it: a subscription outlives a flip to view-only
  // (the unsubscribe op is itself refused then), and subscribing again
  // over one pushes no snapshot -- so a held one is dropped first.
  let live = false;
  let pending = false;
  let held = false;
  let retry = 0;

  const want = () => props.enabled() && !props.viewOnly();

  const flash = (text: string) => {
    setError(text);
    clearTimeout(errorTimer);
    errorTimer = window.setTimeout(() => setError(null), 4000);
  };

  const subscribe = async () => {
    pending = true;
    setObjs(reconcile({}));
    try {
      if (held) {
        await sendOp('widgets.unsubscribe', { toolbars: true });
        held = false;
      }
      const r = await sendOp('widgets.subscribe', { toolbars: true });
      held = true;
      live = true;
      const t = typeof r.theme === 'string' ? r.theme : '';
      if (t !== untrack(theme)) {
        iconCache.clear();
        setTheme(t);
      }
    }
    catch (err: any) {
      live = false;
      setObjs(reconcile({}));
      // Down or not up yet: the connection event re-runs this. The timer
      // is for a viewer too old to send that event.
      if (err?.code === 'Offline' || err?.code === 'Timeout') {
        clearTimeout(retry);
        retry = window.setTimeout(sync, 3000);
      }
      else if (err?.code !== 'ViewOnly') {
        console.warn('fcviewer-ui: tool bars not subscribed ' + JSON.stringify(err));
      }
    }
    finally {
      pending = false;
    }
    if (!untrack(want)) sync();
  };

  const unsubscribe = () => {
    live = false;
    setObjs(reconcile({}));
    if (held && !untrack(props.viewOnly)) {
      held = false;
      sendOp('widgets.unsubscribe', { toolbars: true }).catch(() => { /* the connection is gone */ });
    }
  };

  function sync() {
    if (pending) return;
    if (untrack(want)) {
      if (!live) void subscribe();
    }
    else if (live) {
      unsubscribe();
    }
  }

  createEffect(on(want, () => sync()));

  onCleanup(onConnection((up) => {
    // A new connection has no subscription, whatever the old one held.
    live = false;
    held = false;
    if (up) sync();
    else setObjs(reconcile({}));
  }));

  onCleanup(onPush('widgets', (m) => {
    if (!live && !pending) return;
    const id = m.id;
    if (typeof id !== 'string') return;
    if (m.method === 'open') {
      setObjs(id, { model: m.model, state: m.state ?? {}, layout: m.layout });
    }
    else if (m.method === 'update') {
      if (!objs[id]) return;
      const { layoutSpec, ...diff } = m.content ?? {};
      if (layoutSpec) setObjs(id, 'layout', layoutSpec);
      if (Object.keys(diff).length) setObjs(id, 'state', diff);
    }
    else if (m.method === 'close') {
      setObjs(id, undefined!);
    }
  }));

  onCleanup(() => {
    clearTimeout(retry);
    clearTimeout(errorTimer);
  });

  // The bars to draw, in the desktop's order -- which is by area and
  // then by position, so the top area's bars come first already. The
  // other areas follow them on the one strip.
  const bars = createMemo(() => {
    const items = objs['toolbars']?.layout?.items ?? [];
    const ids: string[] = [];
    for (const item of items) {
      const id = unref(item.widget);
      const bar = id ? objs[id] : undefined;
      if (!id || !bar || bar.state.q_visible === false || skipArea(bar.state.q_area))
        continue;
      ids.push(id);
    }
    const top = ids.filter((id) => objs[id].state.q_area === 'top');
    return [...top, ...ids.filter((id) => objs[id].state.q_area !== 'top')];
  });

  const shown = () => want() && bars().length > 0;

  let strip: HTMLDivElement | undefined;
  createEffect(() => {
    const report = props.onTopInset;
    if (!report) return;
    if (!shown() || props.narrow() || !strip) {
      report(0);
      return;
    }
    const el = strip;
    const ro = new ResizeObserver(() => report(Math.ceil(el.getBoundingClientRect().bottom)));
    ro.observe(el);
    onCleanup(() => { ro.disconnect(); report(0); });
  });

  const run = (name: string, index: number) => {
    // The server runs a tool in the view this client stated; under the
    // default uplink policy a page nobody has clicked in has stated none.
    stateCamera();
    runTool(name, index > 0 ? index : undefined).catch((err: any) => {
      const why = err?.code === 'CommandRefused' ? REFUSED_TIP
        : err?.code === 'NoView' ? 'The view is not ready yet'
        : err?.code === 'ViewOnly' ? 'View only'
        : err?.message || err?.code || 'failed';
      flash(`${name}: ${why}`);
    });
  };

  return (
    <Show when={shown()}>
      <div class={`fc-toolbars ${props.narrow() ? 'fc-toolbars-narrow' : ''}`}
           ref={strip} onPointerDown={(e) => e.stopPropagation()}>
        <For each={bars()}>
          {(id) => (
            <Bar id={id} objs={objs} setObjs={setObjs} theme={theme}
                 narrow={props.narrow} host={props.host} run={run} />
          )}
        </For>
        <Show when={error()}>
          <div class="fc-tb-error" role="status">{error()}</div>
        </Show>
      </div>
    </Show>
  );
}

interface ButtonCtx {
  objs: Record<string, WidgetObj>;
  setObjs: (...args: any[]) => void;
  theme: () => string;
  narrow: () => boolean;
  host: () => boolean;
  run: (name: string, index: number) => void;
}

function Bar(props: ButtonCtx & { id: string }) {
  const bar = () => props.objs[props.id];
  const size = () => {
    const s = Number(bar()?.state.q_iconSize) || 24;
    return props.narrow() ? Math.max(24, s) : Math.min(Math.max(16, s), 32);
  };
  return (
    <div class="fc-tb-bar" role="toolbar"
         aria-label={plainText(bar()?.state.q_windowTitle) || props.id}>
      <For each={bar()?.layout?.items ?? []}>
        {(item) => {
          if (item.separator) return <span class="fc-tb-sep" />;
          const id = unref(item.action);
          // Embedded widgets (the workbench chooser and the like) are
          // not this item's.
          if (!id) return null;
          return <ActionButton {...props} id={id} size={size} />;
        }}
      </For>
    </div>
  );
}

/// What a click on a model runs: the command name the `command` op takes,
/// the 1-based member index (0 = the command itself), and the command
/// that will actually execute -- which is what the allowlist judges.
function identity(objs: Record<string, WidgetObj>, st: Record<string, any>) {
  const name = typeof st.q_command === 'string' ? st.q_command : '';
  const members: unknown[] = Array.isArray(st.q_members) ? st.q_members : [];
  if (members.length && st.q_dropDown !== false) {
    // A group's face runs its default member, as the desktop's does.
    const def = Number(st.q_defaultAction);
    const mid = def >= 0 ? unref(members[def]) : null;
    const m = mid ? objs[mid]?.state : undefined;
    return { name, index: m ? def + 1 : 0, runs: m?.q_memberCommand ?? '' };
  }
  const index = Number(st.q_commandIndex) || 0;
  return { name, index, runs: index > 0 ? (st.q_memberCommand ?? '') : name };
}

function ActionButton(props: ButtonCtx & { id: string; size: () => number }) {
  const st = () => props.objs[props.id]?.state;
  const isGroup = () => {
    const s = st();
    return !!s && Array.isArray(s.q_members) && s.q_members.length > 0
      && s.q_dropDown !== false;
  };
  const who = () => identity(props.objs, st() ?? {});
  const safe = () => !!who().name && (props.host() || isBrowserSafeCommand(who().runs));
  const [open, setOpen] = createSignal(false);
  let root: HTMLDivElement | undefined;

  const click = () => {
    const s = st();
    if (!s) return;
    const w = who();
    // No echo of our own q_checked comes back from a write under this
    // client's origin, so a checkable button flips here; the stream's
    // next update confirms or corrects it.
    if (s.q_checkable && !isGroup())
      props.setObjs(props.id, 'state', 'q_checked', !s.q_checked);
    props.run(w.name, w.index);
  };

  return (
    <Show when={st() && st()!.q_visible !== false}>
      <Show when={!st()!.q_separator} fallback={<span class="fc-tb-sep" />}>
        <div class="fc-tb-item" ref={root}>
          <button
            class="fc-tb-btn"
            classList={{ 'fc-tb-on': !!st()!.q_checked && !isGroup(),
                         'fc-tb-refused': !safe() }}
            disabled={!runnable(safe(), st()!)}
            title={tip(st()!, safe())}
            aria-label={plainText(st()!.q_text)}
            aria-pressed={st()!.q_checkable ? !!st()!.q_checked : undefined}
            data-command={who().runs || who().name}
            onClick={click}
          >
            <Face name={st()!.q_icon} text={st()!.q_text} size={props.size}
                  theme={props.theme} />
          </button>
          <Show when={isGroup()}>
            <button class="fc-tb-caret" aria-haspopup="menu" aria-expanded={open()}
                    title={plainText(st()!.q_text)}
                    onClick={() => setOpen(!open())}>
              <svg width="8" height="8" viewBox="0 0 8 8" aria-hidden="true">
                <path d="M1 2.5l3 3 3-3" fill="none" stroke="currentColor"
                      stroke-width="1.4" />
              </svg>
            </button>
            <Show when={open()}>
              <GroupMenu {...props} anchor={() => root!} close={() => setOpen(false)} />
            </Show>
          </Show>
        </div>
      </Show>
    </Show>
  );
}

function tip(st: Record<string, any>, safe: boolean) {
  let t = plainText(st.q_toolTip) || plainText(st.q_text);
  if (st.q_shortcut) t += ` (${st.q_shortcut})`;
  return safe ? t : `${t}\n\n${REFUSED_TIP}`;
}

/// An icon by the desktop's name, or the label when there is none -- an
/// action with no icon shows its text on the desktop too.
function Face(props: { name: unknown; text: unknown; size: () => number;
                       theme: () => string }) {
  const [src, setSrc] = createSignal<string | null>(null);
  const [failed, setFailed] = createSignal(false);
  createEffect(() => {
    const name = typeof props.name === 'string' ? props.name : '';
    const theme = props.theme();
    const px = Math.round(props.size() * (window.devicePixelRatio || 1));
    setSrc(null);
    setFailed(false);
    if (!name) { setFailed(true); return; }
    let current = true;
    onCleanup(() => { current = false; });
    iconUrl(theme, name, px).then((url) => {
      if (!current) return;
      if (url) setSrc(url);
      else setFailed(true);
    });
  });
  return (
    <Show when={src()} fallback={
      <Show when={failed()} fallback={
        <span class="fc-tb-blank" style={{ width: `${props.size()}px`,
                                           height: `${props.size()}px` }} />
      }>
        <span class="fc-tb-text">{plainText(props.text)}</span>
      </Show>
    }>
      <img class="fc-tb-icon" src={src()!} width={props.size()} height={props.size()}
           alt="" draggable={false} />
    </Show>
  );
}

/// A group's members, from the live member models. Fixed-positioned off
/// the button, because the narrow row scrolls and would clip a child.
function GroupMenu(props: ButtonCtx & { id: string; size: () => number;
                                        anchor: () => HTMLElement;
                                        close: () => void }) {
  const st = () => props.objs[props.id]?.state ?? {};
  const members = () => (Array.isArray(st().q_members) ? st().q_members : [])
    .map((r: unknown) => unref(r)).filter((x: string | null): x is string => !!x);
  let menu: HTMLDivElement | undefined;

  const r = props.anchor().getBoundingClientRect();
  const pos = props.narrow()
    ? { left: `${Math.max(4, r.left)}px`, bottom: `${window.innerHeight - r.top + 4}px` }
    : { left: `${Math.max(4, r.left)}px`, top: `${r.bottom + 4}px` };

  const onDown = (e: PointerEvent) => {
    const t = e.target as Node;
    if (!menu?.contains(t) && !props.anchor().contains(t)) props.close();
  };
  const onKey = (e: KeyboardEvent) => { if (e.key === 'Escape') props.close(); };
  document.addEventListener('pointerdown', onDown, true);
  window.addEventListener('keydown', onKey);
  onCleanup(() => {
    document.removeEventListener('pointerdown', onDown, true);
    window.removeEventListener('keydown', onKey);
  });

  return (
    <div class="fc-tb-menu" role="menu" ref={menu} style={pos}>
      <For each={members()}>
        {(mid) => {
          const m = () => props.objs[mid]?.state;
          return (
            <Show when={m() && m()!.q_visible !== false}>
              <Show when={!m()!.q_separator} fallback={<div class="fc-tb-menu-sep" />}>
                {(() => {
                  const safe = () => props.host()
                    || isBrowserSafeCommand(m()!.q_memberCommand ?? '');
                  return (
                    <button
                      class="fc-tb-menu-item"
                      role={m()!.q_checkable ? 'menuitemcheckbox' : 'menuitem'}
                      aria-checked={m()!.q_checkable ? !!m()!.q_checked : undefined}
                      disabled={!runnable(safe(), m()!)}
                      title={tip(m()!, safe())}
                      data-command={m()!.q_memberCommand || m()!.q_command}
                      onClick={() => {
                        props.run(m()!.q_command, Number(m()!.q_commandIndex) || 0);
                        props.close();
                      }}
                    >
                      <span class="fc-menu-tick">{m()!.q_checked ? '\u2713' : ''}</span>
                      <Face name={m()!.q_icon} text="" size={() => 20}
                            theme={props.theme} />
                      <span>{plainText(m()!.q_text)}</span>
                    </button>
                  );
                })()}
              </Show>
            </Show>
          );
        }}
      </For>
    </div>
  );
}
