// SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
import { defineConfig } from "vite";

// Dev server proxies /api to the FastAPI backend (ADR-0022), so the SPA and the
// API are same-origin in development — no CORS, tokens flow straight through.
export default defineConfig({
  server: {
    port: 5173,
    proxy: {
      "/api": {
        target: process.env.ERP_API_URL ?? "http://localhost:8021",
        changeOrigin: true,
      },
    },
  },
  build: { outDir: "dist" },
});
