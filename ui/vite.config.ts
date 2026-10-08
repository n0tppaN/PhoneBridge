import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'
import path from 'node:path'

// The production build goes straight into the Windows host project, which serves it inside WebView2.
// base './' = relative asset paths, so it works from any folder / virtual host.
export default defineConfig({
  base: './',
  plugins: [react(), tailwindcss()],
  resolve: { alias: { '@': path.resolve(__dirname, './src') } },
  build: {
    outDir: path.resolve(__dirname, '../windows/app/PhoneBridge/wwwroot'),
    emptyOutDir: true,
    sourcemap: false,
  },
  server: { host: '127.0.0.1', port: 5173, strictPort: true },
})
