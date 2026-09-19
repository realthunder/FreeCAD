// Builds the DOM UI layer straight into the served WASM directory:
// build/wasm/web/inspector.{js,css}, loaded by shell.html next to the
// emscripten bundle (wasm-viewer.sh serves the whole tree no-store).
import { defineConfig } from 'vite';
import solid from 'vite-plugin-solid';
import { resolve } from 'path';

export default defineConfig({
  plugins: [solid()],
  build: {
    outDir: resolve(__dirname, '../../../../build/wasm/web'),
    emptyOutDir: true,
    cssCodeSplit: false,
    sourcemap: true,
    rollupOptions: {
      // The viewer chrome plus three harness pages: the sandbox acceptance
      // page (public/sandbox-test.html, docs/ExpressionImage.md "The
      // browser tier"), the spreadsheet panel harness
      // (public/sheet-test.html, docs/SpreadsheetRemote.md sec 5) and the
      // console guest's boot gate (public/console-test.html,
      // docs/Sandbox.md 7.20 C1), its bridge gate (public/bridge-test.html,
      // C2), the console panel's gate (public/console-panel-test.html, C4),
      // the drive a gate injects into the served viewer page
      // (viewerconsole.js, C4) and the latency bench
      // (public/latency-test.html, C5).  The
      // chrome keeps its historical inspector.js name because shell.html
      // loads it by that name.
      input: {
        inspector: resolve(__dirname, 'src/main.tsx'),
        sandboxtest: resolve(__dirname, 'src/sandbox/testmain.ts'),
        sheetharness: resolve(__dirname, 'src/sheetharness.ts'),
        consoletest: resolve(__dirname, 'src/sandbox/consolemain.ts'),
        bridgetest: resolve(__dirname, 'src/sandbox/bridgemain.ts'),
        consolepaneltest: resolve(__dirname, 'src/sandbox/consolepanelmain.ts'),
        viewerconsole: resolve(__dirname, 'src/sandbox/viewerconsolemain.ts'),
        latencytest: resolve(__dirname, 'src/sandbox/latencymain.ts'),
      },
      output: {
        format: 'es',
        entryFileNames: '[name].js',
        chunkFileNames: '[name].js',
        assetFileNames: 'inspector[extname]',
      },
    },
  },
});
