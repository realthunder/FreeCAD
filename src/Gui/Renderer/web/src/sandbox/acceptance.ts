// The browser half of the sandbox acceptance harness
// (docs/ExpressionSandbox.md sec 10, docs/ExpressionImage.md "The browser
// tier").  Its C++ twin is ExpressionImageAcceptanceTest in
// tests/src/App/ExpressionImageHost.cpp; this one re-runs the same shape of
// checks in a real browser, because that is a different WASI implementation
// (src/Gui/Renderer/web/src/sandbox/wasi.ts) and therefore a different
// confinement boundary that has to be demonstrated, not assumed.
//
// Served as sandbox-test.html.  Nothing here is part of the viewer bundle's
// runtime path; it is a page you open.

import { SandboxImage } from './image.js';

export interface Check {
  what: string;
  src: string;
  ok: boolean;
  detail: string;
}

export interface Report {
  ua: string;
  pass: boolean;
  timing?: Record<string, number>;
  positives: Check[];
  negatives: Check[];
  perEvalUs?: number;
  libOpens?: number;
  errors: string[];
}

const say = (report: Report, line: string, sink: (s: string) => void) => sink(line);

export async function runAcceptance(
  baseUrl: string,
  sink: (line: string) => void = (s) => console.log(s),
): Promise<Report> {
  const report: Report = { ua: navigator.userAgent, pass: false, positives: [], negatives: [], errors: [] };

  try {
    const img = await SandboxImage.load(baseUrl, {
      onStdout: (s) => say(report, '[image stdout] ' + s, sink),
      onStderr: (s) => say(report, '[image stderr] ' + s, sink),
    });
    report.timing = {
      fetchMs: Math.round(img.timing.fetchMs),
      compileInstantiateMs: Math.round(img.timing.compileInstantiateMs),
      initMs: Math.round(img.timing.initMs),
    };
    sink(`loaded: fetch ${report.timing.fetchMs} ms, compile+instantiate ` +
         `${report.timing.compileInstantiateMs} ms, fcx_init ${report.timing.initMs} ms`);

    const positive = (what: string, src: string, want: unknown) => {
      const r = img.evalExpression(src);
      const got = r.ok ? r.val : `${r.exc}: ${r.msg}`;
      const ok = r.ok && (want === undefined || JSON.stringify(got) === JSON.stringify(want));
      report.positives.push({ what, src, ok, detail: JSON.stringify(got) });
      sink(`${ok ? 'PASS   ' : 'FAIL   '} ${what.padEnd(22)} ${src}  ->  ${JSON.stringify(got)}`);
    };

    // The evaluator itself, in the browser.
    positive('arithmetic', '1 + 2 * 3', 7);
    positive('units', '2mm + 1cm', { t: 'quantity', u: [1, 0, 0, 0, 0, 0, 0, 0], v: 12 });
    positive('engine function', 'min(1; 2)', 1);
    positive('_math', '_math.degrees(1)', 57.29577951308232);
    positive('_py builtins', '_py.len(<<abc>>)', 3);
    positive('_coll', '_coll.OrderedDict()', {});
    positive('_re', '_re.sub(<<a>>; <<X>>; <<aaa>>)', 'XXX');

    // The bindings pack: the host pre-resolves identifiers, the image never
    // reaches back.  This is the path the sheet will take.
    {
      const r = img.evalExpression('Box.Length + 5mm', {
        'Box.Length': { t: 'quantity', v: 10, u: [1, 0, 0, 0, 0, 0, 0, 0] },
      });
      const ok = r.ok && (r.val as any)?.t === 'quantity' && (r.val as any)?.v === 15;
      report.positives.push({
        what: 'bindings pack', src: 'Box.Length + 5mm', ok,
        detail: JSON.stringify(r.ok ? r.val : r),
      });
      sink(`${ok ? 'PASS   ' : 'FAIL   '} ${'bindings pack'.padEnd(22)} -> ${JSON.stringify(r.ok ? r.val : r)}`);
    }

    const negative = (what: string, src: string, python = false) => {
      const r = python ? img.evalPython(src) : img.evalExpression(src);
      const blocked = !r.ok;
      report.negatives.push({
        what, src, ok: blocked,
        detail: blocked ? `${(r as any).exc}: ${(r as any).msg}` : 'ALLOWED ' + JSON.stringify((r as any).val),
      });
      sink(`${blocked ? 'BLOCKED' : 'LEAKED '} ${what.padEnd(22)} ${src}`);
    };

    // Confinement.  The first four are stopped by the image's OWN engine
    // (CallableExpression::securityCheck) before WASI is even reached; the
    // Python-level ones below are stopped by there being no such capability
    // in the image at all.
    negative('filesystem', "_py.open(<<'/etc/passwd'>>)");
    negative('spawning', '_py.__import__(<<subprocess>>)');
    negative('network', '_py.__import__(<<socket>>)');
    negative('native code', '_py.__import__(<<ctypes>>)');
    negative('document graph', '_app.getDocument(<<x>>)');
    negative('raw open', "open('/etc/passwd')", true);
    negative('raw socket', "__import__('socket').socket()", true);
    negative('raw subprocess', "__import__('subprocess').run(['ls'])", true);
    negative('raw fs write', "open('/Lib/x','w')", true);

    // The bridge is unattached in the browser: a reach-back must fail
    // cleanly rather than silently returning something.
    {
      const r = img.evalExpression('_self.recompute');
      const ok = !r.ok;
      report.negatives.push({
        what: 'unattached bridge', src: '_self.recompute', ok,
        detail: ok ? `${(r as any).exc}: ${(r as any).msg}` : 'ALLOWED',
      });
      sink(`${ok ? 'BLOCKED' : 'LEAKED '} ${'unattached bridge'.padEnd(22)} _self.recompute`);
    }

    // What one evaluation costs, since the sheet will do many.
    {
      const N = 200;
      const t0 = performance.now();
      for (let i = 0; i < N; i++) img.evalExpression('1 + 2 * 3');
      report.perEvalUs = Math.round(((performance.now() - t0) / N) * 1000);
      sink(`per-eval round trip: ${report.perEvalUs} us`);
    }

    report.libOpens = img.wasi.opens.length;
    sink(`stdlib path_open calls: ${report.libOpens}`);
  } catch (e: any) {
    report.errors.push(String(e?.stack ?? e));
    sink('ERROR ' + (e?.stack ?? e));
  }

  report.pass = report.errors.length === 0
    && report.positives.every((c) => c.ok)
    && report.negatives.every((c) => c.ok);
  sink(report.pass ? 'ALL GREEN' : 'FAILURES PRESENT');
  return report;
}
