// The guest, in a worker (docs/Sandbox.md 7.20, C6).  The same BrowserGuest
// the JSPI path boots, with two differences: its host call is a `syncBridge`
// that blocks this thread in Atomics.wait (sabwire.ts) instead of suspending
// the wasm stack, and its calls arrive as messages from the page.
//
// Everything the security model says still holds: the guest is untrusted
// wherever it runs, and every check is on the host at the far end of the
// socket.  What moves into the worker is only the interpreter -- which is why
// a pure CPU loop is now interruptible (the page writes the interrupt buffer
// while the worker runs) and why a runaway statement no longer freezes the
// page.

import { BrowserGuest } from './guest.js';
import { CHUNK, CHUNK_READY, DATA, FAILED, STATE, TOTAL, WAIT_MS, WAITING } from './sabwire.js';

/// Messages this worker sends the page.
type Up =
  | { t: 'op'; bytes: Uint8Array }
  | { t: 'next' }
  | { t: 'end' }
  | { t: 'out'; text: string; stream: 'out' | 'err' }
  | { t: 'booted'; info: any; timing: any; failed: string[]; mem: number }
  | { t: 'bootfail'; error: string }
  | { t: 'result'; id: number; value?: any; error?: string; mem: number };

const post = (m: Up, transfer?: Transferable[]) =>
  (self as any).postMessage(m, transfer ?? []);

let guest: BrowserGuest | null = null;
let ctl: Int32Array | null = null;
let sab: SharedArrayBuffer | null = null;

/// The host call, where this thread is the one that waits.  Returns the reply
/// bytes, or null when nothing answered -- which the guest turns into
/// "host bridge unavailable", as it does under JSPI.
function roundTrip(request: Uint8Array): Uint8Array | null {
  if (!ctl || !sab)
    return null;
  // A copy the page can take: the request sits in wasm memory, which is not
  // shared, and postMessage from here is free -- this thread is not parked yet.
  const bytes = new Uint8Array(request);
  Atomics.store(ctl, STATE, WAITING);
  post({ t: 'op', bytes }, [bytes.buffer]);

  let reply: Uint8Array | null = null;
  let at = 0;
  for (;;) {
    const woke = Atomics.wait(ctl, STATE, WAITING, WAIT_MS);
    const state = Atomics.load(ctl, STATE);
    if (woke === 'timed-out' && state === WAITING)
      return null;
    if (state === FAILED)
      return null;
    const total = Atomics.load(ctl, TOTAL);
    const n = Atomics.load(ctl, CHUNK);
    if (total < 0)
      return null;
    if (!reply)
      reply = new Uint8Array(total);
    if (n > 0) {
      reply.set(new Uint8Array(sab, DATA, n), at);
      at += n;
    }
    if (at >= total)
      return reply;
    // More to come: park again and ask for it.
    Atomics.store(ctl, STATE, WAITING);
    post({ t: 'next' });
  }
}

async function boot(m: any) {
  sab = m.sab;
  ctl = new Int32Array(sab!, 0, 8);
  try {
    guest = await BrowserGuest.boot(m.base, {
      token: m.token,
      bundled: m.bundled,
      syncBridge: { roundTrip, endStatement: () => post({ t: 'end' }) },
      stdout: (text) => post({ t: 'out', text: text + '\n', stream: 'out' }),
      stderr: (text) => post({ t: 'out', text: text + '\n', stream: 'err' }),
    });
    // Once booted, the streams go out as written rather than per line.
    guest.setRawOutput((text, stream) => post({ t: 'out', text, stream }));
    if (m.interrupt)
      guest.setInterruptBuffer(new Int32Array(m.interrupt));
    post({ t: 'booted', info: guest.info, timing: guest.timing,
           failed: guest.failed, mem: guest.memoryMB });
  } catch (e: any) {
    post({ t: 'bootfail', error: String(e?.message ?? e) });
  }
}

self.onmessage = async (e: MessageEvent) => {
  const m = e.data;
  if (m.t === 'boot') {
    await boot(m);
    return;
  }
  if (m.t === 'interrupt' && guest) {
    guest.setInterruptBuffer(new Int32Array(m.buffer));
    return;
  }
  if (m.t === 'call') {
    if (!guest) {
      post({ t: 'result', id: m.id, error: 'the guest is not booted', mem: 0 });
      return;
    }
    try {
      const value = await guest.call(m.request);
      post({ t: 'result', id: m.id, value, mem: guest.memoryMB });
    } catch (err: any) {
      post({ t: 'result', id: m.id, error: String(err?.message ?? err),
             mem: guest.memoryMB });
    }
  }
};
