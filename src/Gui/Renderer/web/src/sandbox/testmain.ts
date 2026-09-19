// Entry point of sandbox-test.html: run the acceptance harness and show it.
// A harness that only prints to the console cannot be read on a phone, which
// is one of the tiers this page exists to check.

import { runAcceptance } from './acceptance.js';

const out = document.createElement('pre');
out.style.cssText = 'font: 12px/1.45 ui-monospace, monospace; padding: 12px; white-space: pre-wrap';
document.body.appendChild(out);

const lines: string[] = [];
const sink = (line: string) => {
  lines.push(line);
  out.textContent = lines.join('\n');
};

const params = new URLSearchParams(location.search);
const base = params.get('fcx') ?? './fcx';

sink(`sandbox image acceptance -- ${base}`);
sink(navigator.userAgent);
sink('');

runAcceptance(base, sink).then(async (report) => {
  document.title = report.pass ? 'sandbox OK' : 'sandbox FAILED';
  // When a harness drives this page headless it wants the verdict as data,
  // not as scraped text.
  (window as any).fcxReport = report;
  if (params.has('post')) {
    try {
      await fetch(params.get('post')!, { method: 'POST', body: JSON.stringify(report) });
    } catch { /* the page still shows the report */ }
  }
});
