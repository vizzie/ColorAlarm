import { defineConfig } from "vite";

export default defineConfig({
  server: {
    port: 5173,
    proxy: {
      "/api": {
        target: "http://localhost:3001",
        changeOrigin: true
      }
    }
  },
  build: {
    assetsDir: ".",
    cssCodeSplit: false,
    rollupOptions: {
      output: {
        entryFileNames: "app.js",
        chunkFileNames: "app.js",
        assetFileNames: (assetInfo) => {
          const name = assetInfo.names && assetInfo.names.length > 0
            ? assetInfo.names[0]
            : assetInfo.name ?? "";
          if (name.endsWith(".css")) {
            return "style.css";
          }
          return "[name][extname]";
        }
      }
    }
  }
});
