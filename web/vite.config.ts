import { defineConfig } from "vite";

export default defineConfig({
    build: {
        // Target small bundle for embedded device
        target: "es2020",
        outDir: "dist",
        // Inline assets to reduce HTTP requests
        assetsInlineLimit: 100000,
        // Minimize output
        minify: "esbuild",
        rollupOptions: {
            output: {
                // Single JS bundle
                manualChunks: undefined,
            },
        },
    },
    server: {
        // Proxy API requests to real device during development
        proxy: {
            "/api": {
                target: "http://192.168.1.10",
                changeOrigin: true,
            },
        },
    },
});
