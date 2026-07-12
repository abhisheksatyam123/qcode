import { defineConfig } from 'vite'

export default defineConfig({
  root: 'src',
  build: {
    outDir: '../dist',
    emptyOutDir: true,
    rollupOptions: {
      input: {
        main: 'src/index.html'
      }
    }
  },
  server: {
    port: 5173,
    proxy: {
      '/api': 'http://localhost:9080',
      '/ws': {
        target: 'ws://localhost:9080',
        ws: true
      }
    }
  }
})
