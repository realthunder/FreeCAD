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

import { draggable, fitOnScreen, loadPos, posStyle, type Pos }
  from './panel';
import { ConsoleSession, type PushStatus, type Stream } from './sandbox/session';
import { setFromGuest } from './widgets/complete.ts';
import { CompleteButton, CompletionList, createCompletion } from './widgets/completion.tsx';

const POS_KEY = 'fcviewer.console.pos';
const HISTORY_KEY = 'fcviewer.console.history';
const HISTORY_MAX = 500;
/// Output kept, in characters.  A print loop must not grow the DOM forever.
const OUTPUT_MAX = 200000;

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


  /// Completion, over the guest's rlcompleter (sandbox/console.py): the
  /// same controller and list the panel's expression dialog runs
  /// (docs/Sandbox.md 7.26).  The guest answers only while idle; a
  /// running statement owns it.
  const c = createCompletion({
    source: () => {
      const s = session;
      if (!s || state() !== 'idle') return null;
      return (text, pos) => s.complete(text.slice(0, pos))
        .then((a) => setFromGuest(text, pos, a.items, a.start));
    },
    editor: {
      text: line,
      caret: () => inputRef?.selectionStart ?? line().length,
      apply: (text, pos) => {
        setLine(text);
        if (inputRef) {
          inputRef.value = text;
          inputRef.focus();
          inputRef.setSelectionRange(pos, pos);
        }
      },
    },
  });

  const onKey = (e: KeyboardEvent) => {
    const ctrl = e.ctrlKey || e.metaKey;
    // The list first: while one is up it owns Escape, Up/Down, Enter and
    // Tab; with none up, Tab and Ctrl+Space are the explicit ask.
    if (c.onKey(e)) return;

    if (e.key === 'Enter') {
      e.preventDefault();
      if (state() === 'idle') submit();
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
        <CompletionList c={c} class="fc-console-complete-list" />
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
            onInput={(e) => { setLine(e.currentTarget.value); c.onInput(e); }}
            onKeyDown={onKey}
            onPaste={onPaste}
          />
          <CompleteButton c={c} class="fc-console-btn" disabled={() => state() !== 'idle'} />
        </div>

      </div>
    </Show>
  );
}
