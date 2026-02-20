import { readdirSync, readFileSync, writeFileSync, existsSync } from "node:fs";
import { extname, join } from "node:path";
import { gzipSync } from "node:zlib";

const distDir = join(process.cwd(), "dist");

if (!existsSync(distDir)) {
  process.exit(0);
}

for (const entry of readdirSync(distDir, { withFileTypes: true })) {
  if (!entry.isFile()) {
    continue;
  }

  const fileName = entry.name;
  if (fileName.endsWith(".gz")) {
    continue;
  }

  const ext = extname(fileName);
  if (ext !== ".html" && ext !== ".js") {
    continue;
  }

  const srcPath = join(distDir, fileName);
  const gzipPath = `${srcPath}.gz`;
  const src = readFileSync(srcPath);
  const gzipped = gzipSync(src, { level: 9 });
  writeFileSync(gzipPath, gzipped);
}
