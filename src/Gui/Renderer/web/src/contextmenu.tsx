// The scene's right-click menu (docs/ThinClient.md sec 8.11b): the
// desktop's 3D-view context menu, built by the server for what the click's
// ray hits and drawn here at the click. The viewer says where (main.cpp
// openContextMenu, the fc:contextmenu event); the server says what, and
// whether this connection may run each entry -- one it may not is drawn
// disabled with the reason, as the tool bars draw theirs. The camera
// entries are this browser's own and run here.
import { For, Show, createSignal, onCleanup } from 'solid-js';
import { sendOp } from './control';

interface Entry {
  id?: number;
  text?: string;
  tip?: string;
  shortcut?: string;
  icon?: string;
  checkable?: boolean;
  checked?: boolean;
  enabled?: boolean;
  allowed?: boolean;
  reason?: string;
  kind?: string;
  local?: string;
  /// A "Pick geometry" entry: what it selects, painted while pointed at
  pick?: { obj: string; sub: string };
  separator?: boolean;
  items?: Entry[];
}

interface Menu {
  token: number;
  x: number;
  y: number;
  /// The submenus walked into, the top level first
  path: Entry[][];
  titles: string[];
}

/// The viewer's own camera actions (main.cpp fcviewer_navi_action)
const LOCAL: Record<string, number> = {
  isometric: 0, dimetric: 1, trimetric: 2, fitAll: 3,
  top: 5, bottom: 6, front: 7, rear: 8, right: 9, left: 10,
};

/// `img:<sha1>` ids as data URLs, once per page: content-addressed
const images = new Map<string, Promise<string | null>>();
function imageUrl(name: string): Promise<string | null> {
  let p = images.get(name);
  if (!p) {
    p = sendOp('widgets.image', { name })
      .then((r) => (typeof r?.data === 'string' ? `data:image/png;base64,${r.data}` : null))
      .catch((err) => {
        if (err?.code === 'Offline' || err?.code === 'Timeout') images.delete(name);
        return null;
      });
    images.set(name, p);
  }
  return p;
}

function Icon(props: { name?: string }) {
  const [src, setSrc] = createSignal<string | null>(null);
  if (props.name) imageUrl(props.name).then(setSrc);
  return (
    <span class="fc-ctx-icon">
      <Show when={src()}><img src={src()!} alt="" /></Show>
    </span>
  );
}

export function SceneContextMenu(props: { viewOnly: () => boolean }) {
  const [menu, setMenu] = createSignal<Menu | null>(null);
  const [error, setError] = createSignal<string | null>(null);
  const [busy, setBusy] = createSignal(false);
  let root: HTMLDivElement | undefined;
  // The ask in flight: a second right click before the answer wins
  let asked = 0;
  // A pick entry's preview, painted by the viewer while pointed at
  let hovering = false;
  const hover = (it: Entry) => {
    if (!it.pick) return;
    hovering = true;
    window.fcviewerHoverNamed?.(it.pick.obj, it.pick.sub);
  };
  const unhover = () => {
    if (!hovering) return;
    hovering = false;
    window.fcviewerHoverNamed?.('', '');
  };

  const close = (tell = true) => {
    const m = menu();
    unhover();
    if (m) window.fcviewerHoldHover?.(false);
    setMenu(null);
    setError(null);
    if (m && tell) sendOp('contextMenu.close', { menu: m.token }).catch(() => {});
  };

  const onOpen = (e: Event) => {
    const d = (e as CustomEvent).detail as { x: number; y: number; ray: number[] } | null;
    if (!d) return;
    close();
    const ask = ++asked;
    sendOp('contextMenu', { ray: d.ray })
      .then((r) => {
        if (ask !== asked) return;
        const items = (r.items as Entry[]) ?? [];
        if (!items.length) return;
        // The menu has the pointer now, as a desktop popup has the mouse:
        // the scene preselects nothing under it
        window.fcviewerHoldHover?.(true);
        setMenu({ token: r.menu as number, x: d.x, y: d.y, path: [items], titles: [] });
      })
      .catch(() => {});
  };
  const onDown = (e: PointerEvent) => {
    if (menu() && !(root && root.contains(e.target as Node))) close();
  };
  const onKey = (e: KeyboardEvent) => {
    if (e.key === 'Escape' && menu()) close();
  };
  window.addEventListener('fc:contextmenu', onOpen);
  document.addEventListener('pointerdown', onDown, true);
  window.addEventListener('keydown', onKey);
  onCleanup(() => {
    window.removeEventListener('fc:contextmenu', onOpen);
    document.removeEventListener('pointerdown', onDown, true);
    window.removeEventListener('keydown', onKey);
  });

  const level = () => {
    const m = menu();
    return m ? m.path[m.path.length - 1] : [];
  };
  const usable = (it: Entry) => it.enabled !== false && (it.allowed !== false || !!it.local);

  const choose = (it: Entry, ev?: MouseEvent) => {
    const m = menu();
    if (!m || busy()) return;
    setError(null);
    if (it.items) {
      unhover();
      setMenu({ ...m, path: [...m.path, it.items], titles: [...m.titles, it.text ?? ''] });
      return;
    }
    if (!usable(it)) return;
    if (it.kind === 'local' && it.local && it.local in LOCAL) {
      window.fcviewerNaviAction?.(LOCAL[it.local]);
      close();
      return;
    }
    setBusy(true);
    // Ctrl adds a pick to the selection, as on the desktop
    const extend = !!(ev && (ev.ctrlKey || ev.metaKey));
    sendOp('contextMenu.trigger', { menu: m.token, item: it.id, extend })
      .then(() => {
        // The server selected it; the viewer paints its own selection
        if (it.kind === 'pick' && it.pick)
          window.fcviewerSelectNamed?.(it.pick.obj, it.pick.sub, extend);
        close(false);
      })
      .catch((err) => setError(err?.message || err?.code || 'Failed'))
      .finally(() => setBusy(false));
  };
  const back = () => {
    const m = menu();
    if (m && m.path.length > 1)
      setMenu({ ...m, path: m.path.slice(0, -1), titles: m.titles.slice(0, -1) });
  };

  // Kept on screen; the list scrolls past the window's height
  const style = () => {
    const m = menu()!;
    const w = 260;
    const h = Math.min(window.innerHeight - 16, level().length * 36 + 60);
    const x = Math.max(8, Math.min(m.x, window.innerWidth - w - 8));
    const y = Math.max(8, Math.min(m.y, window.innerHeight - h - 8));
    return { left: `${x}px`, top: `${y}px` };
  };

  return (
    <Show when={menu()}>
      <div class="fc-ctxmenu" ref={root} style={style()}>
        <div class="fc-menu" role="menu" aria-label="Context menu">
          <Show when={menu()!.titles.length}>
            <button class="fc-menu-item fc-ctx-back" onClick={back}>
              <span class="fc-menu-tick">{'\u2039'}</span>
              {menu()!.titles[menu()!.titles.length - 1]}
            </button>
          </Show>
          <For each={level()}>
            {(it) => (
              <Show when={!it.separator} fallback={<div class="fc-tb-menu-sep" />}>
                <button
                  class="fc-menu-item fc-ctx-item"
                  classList={{ 'fc-ctx-disabled': !it.items && !usable(it) }}
                  role={it.checkable ? 'menuitemcheckbox' : 'menuitem'}
                  aria-checked={it.checkable ? !!it.checked : undefined}
                  aria-disabled={!it.items && !usable(it)}
                  title={(!it.items && it.allowed === false && !it.local
                          ? it.reason : it.tip?.replace(/<[^>]*>/g, ' ').trim()) || undefined}
                  onClick={(ev) => choose(it, ev)}
                  onPointerEnter={() => hover(it)}
                  onPointerLeave={unhover}
                >
                  <span class="fc-menu-tick">{it.checkable && it.checked ? '\u2713' : ''}</span>
                  <Icon name={it.icon} />
                  <span class="fc-ctx-text">{it.text}</span>
                  <Show when={it.items}>
                    <span class="fc-ctx-more">{'\u203a'}</span>
                  </Show>
                  <Show when={!it.items && it.shortcut}>
                    <span class="fc-ctx-shortcut">{it.shortcut}</span>
                  </Show>
                </button>
              </Show>
            )}
          </For>
          <Show when={error()}>
            <div class="fc-ctx-error">{error()}</div>
          </Show>
        </div>
      </div>
    </Show>
  );
}
