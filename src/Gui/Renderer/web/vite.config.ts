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
      input: resolve(__dirname, 'src/main.tsx'),
      output: {
        format: 'es',
        entryFileNames: 'inspector.js',
        assetFileNames: 'inspector[extname]',
      },
    },
  },
});
