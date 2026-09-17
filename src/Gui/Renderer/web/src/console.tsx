// The Python console panel (docs/Sandbox.md 7.20, C4): the desktop's Python
// console, in the viewer chrome, running in THIS page -- pyodide booted from
// the serving FreeCAD, reaching the served document over the host bridge as
// this client (C3's principal), under the host's catalog.
//
// The session (sandbox/session.ts) is booted on first open, not at load: the
// runtime is some 13 MB and 1.5 s that a viewer who never opens the console
// should not pay.  Closing the panel keeps it, names and all; a reload is
// the way to a fresh interpreter.
//
// Keys, as a terminal console has them: Enter runs the line, Up/Down walk the
// history (kept per browser), Tab completes, Ctrl+C interrupts a running
// statement or drops the line being typed, Ctrl+L clears the output.  A
// paste of several lines is pushed line by line, as if typed.

import { createEffect, createSignal, For, on, onCleanup, Show } from 'solid-js';
import type { Accessor } from 'solid-js';

import { draggable, fitOnScreen, loadPos, NARROW, onKeyboardInset, posStyle, type Pos }
  from './panel';
import { ConsoleSession, type PushStatus, type Stream } from './sandbox/session';
import { filterSet, setFromGuest, splice, stillApplies, triggerFor }
  from './widgets/complete.ts';
import type { CompletionSet } from './widgets/complete.ts';

const POS_KEY = 'fcviewer.console.pos';
const HISTORY_KEY = 'fcviewer.console.history';
const HISTORY_MAX = 500;
/// Output kept, in characters.  A print loop must not grow the DOM forever.
const OUTPUT_MAX = 200000;
/// A word trigger fires only after typing pauses; the dot fires at once.
/// The guest is in THIS page, so the ask is a call and not a round trip --
/// but a completion over a big namespace still costs, and a menu that
/// flickers on every keystroke is worse than one that waits.
const DEBOUNCE_MS = 160;

type Kind = Stream | 'in' | 'info';
interface Chunk { kind: Kind; text: string }
/// booting: the session is coming up; idle/busy: it is up; failed: it is not.
export type ConsoleState = 'closed' | 'booting' | 'idle' | 'busy' | 'failed';

function loadHistory(): string[] {
  try {
    const h = JSON.parse(localStorage.getItem(HISTORY_KEY) ?? '[]');
    return Array.isArray(h) ? h.filter((x) => typeof x === 'string') : [];
  }
  catch {
    return [];
  }
}

function saveHistory(h: string[]) {
  try { localStorage.setItem(HISTORY_KEY, JSON.stringify(h.slice(-HISTORY_MAX))); }
  catch { /* private mode or quota: a history this session only */ }
}

export function ConsolePanel(props: {
  open: Accessor<boolean>;
  onClose: () => void;
  /// The document the viewer is on; the console follows a switch.
  doc: Accessor<string>;
  viewOnly: Accessor<boolean>;
  /// The serving FreeCAD's origin and the link's token.
  server: string;
  token?: string;
  client?: Accessor<string>;
  /// Ride the wasm viewer's scene socket (the viewer chrome); a page with no
  /// viewer leaves it off and the console connects on its own.
  viewerSocket?: boolean;
}) {
  const [chunks, setChunks] = createSignal<Chunk[]>([]);
  const [state, setState] = createSignal<ConsoleState>('closed');
  const [status, setStatus] = createSignal('');
  const [more, setMore] = createSignal(false);
  const [line, setLine] = createSignal('');
  const [pos, setPos] = createSignal<Pos | null>(loadPos(POS_KEY));
  /// The completion list as it is shown: the answered set narrowed by what
  /// has been typed since (docs/Sandbox.md 7.23, the same controller the
  /// panel's expression fields run).
  const [items, setItems] = createSignal<string[]>([]);
  const [active, setActive] = createSignal(0);
  const [narrow, setNarrow] = createSignal(window.innerWidth <= NARROW);
  /// How much of the viewport the on-screen keyboard covers: the chip strip
  /// sits on top of it, where the thumb already is.
  const [inset, setInset] = createSignal(0);
  /// What the guest last answered, and what it was answered FOR.
  let completionSet: CompletionSet | null = null;
  let askTimer = 0;

  let session: ConsoleSession | null = null;
  let sessionDoc = '';
  let panelRef: HTMLDivElement | undefined;
  let outRef: HTMLDivElement | undefined;
  let inputRef: HTMLInputElement | undefined;

  const history = loadHistory();
  let historyAt = history.length;
  /// The line being typed when the history walk started, restored at its end.
  let draft = '';

  const write = (text: string, kind: Kind) => {
    if (!text) return;
    setChunks((cs) => {
      const last = cs[cs.length - 1];
      let next = last && last.kind === kind
        ? [...cs.slice(0, -1), { kind, text: last.text + text }]
        : [...cs, { kind, text }];
      let total = next.reduce((n, c) => n + c.text.length, 0);
      while (total > OUTPUT_MAX && next.length > 1) {
        total -= next[0].text.length;
        next = next.slice(1);
      }
      return next;
    });
    // Scroll after the DOM has the text; only when already at the bottom,
    // so reading back through the output is not yanked away by a print.
    const el = outRef;
    if (el && el.scrollHeight - el.scrollTop - el.clientHeight < 24)
      queueMicrotask(() => { el.scrollTop = el.scrollHeight; });
  };

  const boot = async () => {
    if (session || state() === 'booting') return;
    if (!ConsoleSession.supported) {
      setState('failed');
      setStatus('this browser cannot run the console yet: it needs JavaScript Promise '
                + 'Integration (Chrome 137, Firefox 139 or later)');
      return;
    }
    setState('booting');
    const t0 = performance.now();
    try {
      sessionDoc = props.doc();
      session = await ConsoleSession.open({
        server: props.server,
        token: props.token,
        doc: sessionDoc || undefined,
        client: (props.client?.() || 'viewer') + ' (console)',
        output: write,
        viewerSocket: props.viewerSocket,
        progress: (what) => setStatus(what + '...'),
      });
      const g = session.guest;
      // The bridge's figures, for a gate page to read (consolepanelmain.ts).
      (window as any).fcxConsoleStats = () => session?.bridge.stats ?? null;
      write(`Python (pyodide ${g.info.version}) in this page, `
            + `${Math.round(performance.now() - t0)} ms to start. `
            + 'FreeCAD reaches the served document as this client, '
            + (session.onViewerSocket ? "on the viewer's connection.\n"
                                      : 'on a connection of its own.\n'), 'info');
      // A switch made while the session was booting.
      if (props.doc() && props.doc() !== sessionDoc) followDoc(props.doc());
      setStatus('');
      setState('idle');
      queueMicrotask(() => inputRef?.focus());
    } catch (e: any) {
      session = null;
      setState('failed');
      setStatus(String(e?.message ?? e));
    }
  };

  const followDoc = (name: string) => {
    if (!session || name === sessionDoc) return;
    session.switchDoc(name);
    sessionDoc = name;
    setMore(false);
    write(`-- now on document ${name}; names bound to the previous one no longer resolve\n`,
          'info');
  };

  createEffect(on(props.open, (open) => { if (open) void boot(); }));
  createEffect(on(props.doc, (d) => { if (d) followDoc(d); }, { defer: true }));
  onCleanup(() => session?.close());

  const remember = (text: string) => {
    if (text.trim() && history[history.length - 1] !== text) {
      history.push(text);
      if (history.length > HISTORY_MAX) history.splice(0, history.length - HISTORY_MAX);
      saveHistory(history);
    }
    historyAt = history.length;
  };

  /// Run lines one after another, as if each was typed and entered.
  const run = async (lines: string[]) => {
    const s = session;
    if (!s || state() !== 'idle') return;
    setState('busy');
    try {
      for (const text of lines) {
        write((more() ? '... ' : '>>> ') + text + '\n', 'in');
        remember(text);
        const r: PushStatus = await s.push(text);
        setMore(r === 'more');
        if (r === 'error' || r === 'syntax') break;
      }
    } finally {
      setState('idle');
      queueMicrotask(() => inputRef?.focus());
    }
  };

  const submit = () => {
    const text = line();
    setLine('');
    void run([text]);
  };

  const closeList = () => {
    setItems([]);
    setActive(0);
  };

  /// Ask the guest, and show what it says as a LIST.
  ///
  /// This replaced a readline-shaped completion (insert the longest common
  /// prefix, print the candidates into the output) -- which, on a handset
  /// 2026-09-17, meant a list appeared only when the line ended in a dot
  /// (the one case where more than one candidate survives), never narrowed
  /// as typing continued, and never came up on its own.
  const ask = async (value: string, at: number) => {
    const s = session;
    if (!s || state() !== 'idle') return;
    try {
      const answered = await s.complete(value.slice(0, at));
      if (line() !== value) return;   // the line moved on: a stale answer
      const built = setFromGuest(value, at, answered.items, answered.start);
      completionSet = built;
      setItems(filterSet(built, value, at));
      setActive(0);
    }
    catch {
      closeList();
    }
  };

  /// Every edit of the input: narrow locally if the answered set still
  /// covers this segment, else decide whether to ask again.
  const onLineInput = (value: string, at: number) => {
    setLine(value);
    clearTimeout(askTimer);

    if (completionSet && stillApplies(completionSet, value, at)) {
      const narrowed = filterSet(completionSet, value, at);
      setItems(narrowed);
      setActive(0);
      if (narrowed.length) return;
      completionSet = null;   // narrowed to nothing: ask again below
    }
    else {
      completionSet = null;
      closeList();
    }

    const why = triggerFor(value, at);
    if (!why) return;
    if (why === 'dot') void ask(value, at);
    else askTimer = window.setTimeout(() => void ask(value, at), DEBOUNCE_MS);
  };

  const pick = (item: string) => {
    if (!completionSet || !item) return;
    const at = inputRef?.selectionStart ?? line().length;
    const next = splice(completionSet, item, line(), at);
    setLine(next.text);
    closeList();
    completionSet = null;
    if (inputRef) {
      inputRef.value = next.text;
      inputRef.focus();
      inputRef.setSelectionRange(next.pos, next.pos);
    }
  };

  /// The pointer is taken on pointerdown, not click: a tap that lets the
  /// input blur closes the keyboard, the visual viewport grows back, and
  /// the chip moves out from under the finger before the click lands.
  const hold = (e: PointerEvent, item: string) => {
    e.preventDefault();
    pick(item);
  };

  const onNarrowResize = () => setNarrow(window.innerWidth <= NARROW);
  window.addEventListener('resize', onNarrowResize);
  const stopInset = onKeyboardInset(setInset);
  onCleanup(() => {
    window.removeEventListener('resize', onNarrowResize);
    stopInset();
    clearTimeout(askTimer);
  });

  const onKey = (e: KeyboardEvent) => {
    const ctrl = e.ctrlKey || e.metaKey;
    const open = items().length > 0;
    const caret = () => inputRef?.selectionStart ?? line().length;

    // While a list is up it owns the keys that move and choose within it:
    // Escape dismisses the list rather than the line, and Up/Down walk the
    // candidates rather than the history -- which is what every console
    // with completion does, and what the panel's fields already do.
    if (open) {
      if (e.key === 'Escape') { e.preventDefault(); closeList(); return; }
      if (e.key === 'ArrowDown') {
        e.preventDefault();
        setActive((n) => (n + 1) % items().length);
        return;
      }
      if (e.key === 'ArrowUp') {
        e.preventDefault();
        setActive((n) => (n - 1 + items().length) % items().length);
        return;
      }
      if (e.key === 'Enter' || e.key === 'Tab') {
        e.preventDefault();
        pick(items()[active()]);
        return;
      }
    }

    if (e.key === 'Enter') {
      e.preventDefault();
      if (state() === 'idle') submit();
    }
    else if (e.key === 'Tab') {
      // No list up: Tab is the explicit ask (the button beside the input
      // is the same ask, for a keyboard that has no Tab key).
      e.preventDefault();
      void ask(line(), caret());
    }
    else if (e.key === ' ' && ctrl) {
      e.preventDefault();
      void ask(line(), caret());
    }
    else if (e.key === 'ArrowUp' || e.key === 'ArrowDown') {
      if (!history.length) return;
      e.preventDefault();
      if (historyAt === history.length) draft = line();
      historyAt = Math.max(0, Math.min(history.length,
                                       historyAt + (e.key === 'ArrowUp' ? -1 : 1)));
      setLine(historyAt === history.length ? draft : history[historyAt]);
    }
    else if (ctrl && e.key === 'c' && !(inputRef && inputRef.selectionStart !== inputRef.selectionEnd)) {
      e.preventDefault();
      if (state() === 'busy') {
        session?.interrupt();
      }
      else {
        write((more() ? '... ' : '>>> ') + line() + '\nKeyboardInterrupt\n', 'err');
        setLine('');
        if (more()) { setMore(false); void session?.reset(); }
      }
    }
    else if (ctrl && e.key === 'l') {
      e.preventDefault();
      setChunks([]);
    }
  };

  const onPaste = (e: ClipboardEvent) => {
    const text = e.clipboardData?.getData('text/plain') ?? '';
    if (!text.includes('\n')) return;
    e.preventDefault();
    const at = inputRef?.selectionStart ?? line().length;
    const lines = (line().slice(0, at) + text + line().slice(at)).replace(/\r\n?/g, '\n').split('\n');
    // A trailing newline in the paste ends the last line; a block pasted
    // without one leaves its last line in the box to be finished.
    const last = lines.pop() ?? '';
    setLine(last);
    void run(lines);
  };

  let headerRef: HTMLDivElement | undefined;
  createEffect(() => {
    if (!props.open() || !headerRef || !panelRef) return;
    draggable(headerRef, () => panelRef!, setPos, POS_KEY);
    fitOnScreen(panelRef, pos, setPos);
  });

  return (
    <Show when={props.open()}>
      <div class="fc-console" ref={panelRef} style={posStyle(pos())} data-state={state()}>
        <div class="fc-console-head" ref={headerRef}>
          <span class="fc-console-title">Python</span>
          <span class="fc-console-doc">{props.doc()}</span>
          <Show when={props.viewOnly()}>
            <span class="fc-sheet-ro" title="the host has disabled editing: statements may read, not write">
              view only
            </span>
          </Show>
          <span class="fc-sheet-spacer" />
          <Show when={state() === 'busy'}>
            <button class="fc-console-btn fc-console-interrupt" title="Interrupt (Ctrl+C)"
                    onClick={() => session?.interrupt()}>
              Interrupt
            </button>
          </Show>
          <button class="fc-console-btn" title="Clear the output (Ctrl+L)"
                  onClick={() => setChunks([])}>
            Clear
          </button>
          <button class="fc-sheet-close" onClick={props.onClose} title="Close">
            &times;
          </button>
        </div>
        <div class="fc-console-out" ref={outRef} onClick={() => {
          if (!window.getSelection()?.toString()) inputRef?.focus();
        }}>
          <For each={chunks()}>
            {(c) => <span class={`fc-console-${c.kind}`}>{c.text}</span>}
          </For>
          <Show when={status()}>
            <div class={state() === 'failed' ? 'fc-console-err' : 'fc-console-info'}>
              {status()}
            </div>
          </Show>
        </div>
        <Show when={items().length > 0 && !narrow()}>
          <div class="fc-console-suggest">
            <For each={items().slice(0, 12)}>
              {(item, i) => (
                <div
                  class={i() === active() ? 'fc-panel-sugg fc-panel-sugg-on' : 'fc-panel-sugg'}
                  onPointerDown={(e) => hold(e, item)}
                >
                  {item}
                </div>
              )}
            </For>
          </div>
        </Show>
        <div class="fc-console-line">
          <span class="fc-console-prompt">{more() ? '...' : '>>>'}</span>
          <input
            ref={inputRef}
            class="fc-console-input"
            spellcheck={false}
            autocomplete="off"
            autocapitalize="off"
            disabled={state() === 'booting' || state() === 'failed'}
            autocorrect="off"
            value={line()}
            onInput={(e) => onLineInput(e.currentTarget.value,
                                        e.currentTarget.selectionStart
                                        ?? e.currentTarget.value.length)}
            onKeyDown={onKey}
            onPaste={onPaste}
          />
          {/* Tab is the shortcut; this is the tap target beside it. A
              handset keyboard has no Tab key at all, so the console's
              completion was unreachable on the device that needs it most
              (docs/Sandbox.md 7.23 ruled the same for the expression
              field: an explicit ask must be a tap target, not a chord).
              pointerdown with preventDefault, not click: the press must
              not blur the input, or the keyboard collapses under the
              caret being completed. */}
          <button
            class="fc-console-btn fc-console-complete"
            title="Complete (Tab)"
            disabled={state() !== 'idle'}
            onPointerDown={(e) => {
              e.preventDefault();
              void ask(line(), inputRef?.selectionStart ?? line().length);
            }}
          >
            &#9662;
          </button>
        </div>

        {/* On a phone the candidates are a strip pinned above the keyboard
            (the input row itself is behind it); on a wide viewport they are
            a list in the panel's own flow -- .fc-console is overflow:hidden,
            so a dropdown positioned over the input row would be clipped. */}
        <Show when={items().length > 0 && narrow()}>
          <div class="fc-panel-chips" style={{ bottom: `${inset()}px` }}>
            <For each={items().slice(0, 20)}>
              {(item, i) => (
                <button
                  class={i() === active() ? 'fc-panel-chip fc-panel-chip-on' : 'fc-panel-chip'}
                  onPointerDown={(e) => hold(e, item)}
                >
                  {item.slice(item.lastIndexOf('.') + 1)}
                </button>
              )}
            </For>
          </div>
        </Show>
      </div>
    </Show>
  );
}
