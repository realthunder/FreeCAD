# Expression sandbox Phase 0 measurement rigs

Companions to docs/ExpressionSandboxPhase0.md (numbers measured
2026-08-30 on the dev box). All ASCII, all bounded.

- extract_expressions.py <roots...> -- walks .FCStd files, dumps every
  stored expression (engine bindings + sheet formulas) to corpus.jsonl
  next to the script.
- analyze_corpus.py -- classifies corpus.jsonl: member reads, calls,
  pseudo-properties, foreign-doc refs (the deliverable (a)/(b) data).
- native_bench.py -- run under FreeCADCmd (wrap with .conda/run.sh +
  .conda/limited.sh): per-class evalExpression cost + synthetic sheet
  recomputes. Set BENCH_OUT=<path.json> for machine-readable output.
- scanner_bench.py -- run under FreeCADCmd: evaluates all stored
  expressions of ~/works/sw/models/scanner.FCStd in place.
- native_py_bench.py / wasm_bench.mjs -- twin CPython value-layer
  workloads, native python3 vs pyodide under node ("npm install
  pyodide" beside wasm_bench.mjs first).
