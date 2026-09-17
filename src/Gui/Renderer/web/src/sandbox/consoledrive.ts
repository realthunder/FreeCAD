// Using the console panel the way a person does, for a gate: text into the
// input, Enter, Tab, keys, through DOM events, and reading what the output
// shows.  Shared by the panel's own gate page (consolepanelmain.ts) and the
// drive injected into the served viewer page (viewerconsolemain.ts).

export interface Check { name: string; pass: boolean; detail: string }

export function makeReport() {
  const report = {
    ok: false,
    bootMs: 0,
    stats: null as unknown,
    checks: [] as Check[],
    error: '',
  };
  const check = (name: string, pass: boolean, detail: unknown) => {
    const d = typeof detail === 'string' ? detail : JSON.stringify(detail);
    report.checks.push({ name, pass, detail: d });
    console.log(`${pass ? 'PASS' : 'FAIL'} ${name} | ${d}`);
  };
  return { report, check };
}

export const panel = () => document.querySelector('.fc-console') as HTMLElement;
export const input = () => panel().querySelector('.fc-console-input') as HTMLInputElement;
export const prompt = () => panel().querySelector('.fc-console-prompt')?.textContent ?? '';
export const output = () => panel().querySelector('.fc-console-out')?.textContent ?? '';
export const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

export async function until(what: string, cond: () => boolean, ms: number) {
  const t0 = performance.now();
  while (!cond()) {
    if (performance.now() - t0 > ms)
      throw new Error(`timed out waiting for ${what} (state ${panel()?.dataset.state})`);
    await sleep(15);
  }
}

export const idle = (ms = 30000) =>
  until('the console to be idle', () => panel()?.dataset.state === 'idle', ms);

export function type(text: string) {
  const el = input();
  el.focus();
  el.value = text;
  el.setSelectionRange(text.length, text.length);
  el.dispatchEvent(new InputEvent('input', { bubbles: true }));
}

export function key(k: string, mods: KeyboardEventInit = {}) {
  input().dispatchEvent(new KeyboardEvent('keydown', { key: k, bubbles: true, cancelable: true, ...mods }));
}

/// Enter one line; what the output gained while it ran.
export async function enter(text: string): Promise<string> {
  const mark = output().length;
  type(text);
  key('Enter');
  await idle();
  return output().slice(mark);
}

/// Wait for the panel to finish booting; throws with the output when it failed.
export async function booted(ms = 240000) {
  await until('the console to boot',
              () => ['idle', 'failed'].includes(panel()?.dataset.state ?? ''), ms);
  if (panel().dataset.state === 'failed')
    throw new Error('boot failed: ' + output());
}
