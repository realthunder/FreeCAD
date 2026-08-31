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
      // The viewer chrome plus two harness pages: the sandbox acceptance
      // page (public/sandbox-test.html, docs/ExpressionImage.md "The
      // browser tier") and the spreadsheet panel harness
      // (public/sheet-test.html, docs/SpreadsheetRemote.md sec 5).  The
      // chrome keeps its historical inspector.js name because shell.html
      // loads it by that name.
      input: {
        inspector: resolve(__dirname, 'src/main.tsx'),
        sandboxtest: resolve(__dirname, 'src/sandbox/testmain.ts'),
        sheetharness: resolve(__dirname, 'src/sheetharness.ts'),
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
