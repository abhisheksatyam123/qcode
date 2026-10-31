import { defineConfig, type Plugin } from 'vite'
import { cpSync, existsSync, readdirSync } from 'node:fs'
import { resolve } from 'node:path'

const srcDir = resolve(__dirname, 'src')
const outDir = resolve(__dirname, 'dist')

// index.html loads the vendored libraries with classic <script> tags (they
// define globals such as `marked` and `hljs`), and the self-hosted fonts and
// dashboard page are fetched by URL. Vite does not bundle any of these, so
// copy them verbatim so dist/ is a complete drop-in for src/.
function copyStaticAssets(): Plugin {
  return {
    name: 'qcode-copy-static-assets',
    apply: 'build',
    closeBundle() {
      for (const name of readdirSync(srcDir)) {
        if (name.startsWith('vendor-') || name === 'dashboard.html') {
          cpSync(resolve(srcDir, name), resolve(outDir, name))
        }
      }
      const fonts = resolve(srcDir, 'fonts/ui')
      if (existsSync(fonts)) cpSync(fonts, resolve(outDir, 'fonts/ui'), { recursive: true })
    }
  }
}

export default defineConfig({
  root: 'src',
  plugins: [copyStaticAssets()],
  build: {
    outDir: '../dist',
    emptyOutDir: true,
    rollupOptions: {
      input: {
        main: resolve(srcDir, 'index.html')
      }
    }
  },
  server: {
    port: 5173,
    // Every backend route the UI calls; streaming responses pass through.
    proxy: Object.fromEntries(
      ['/api', '/session', '/sessions', '/providers', '/tasks', '/rename']
        .map((prefix) => [prefix, { target: 'http://localhost:9080', changeOrigin: true }])
    )
  }
})
