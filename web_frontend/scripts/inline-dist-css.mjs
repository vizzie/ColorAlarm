import { readFileSync, writeFileSync, existsSync, unlinkSync } from "node:fs";
import { join } from "node:path";

const distDir = join(process.cwd(), "dist");
const htmlPath = join(distDir, "index.html");
const cssPath = join(distDir, "style.css");

if (!existsSync(htmlPath) || !existsSync(cssPath)) {
  process.exit(0);
}

const html = readFileSync(htmlPath, "utf8");
const css = readFileSync(cssPath, "utf8");

const linkRegex = /<link[^>]*rel=["']stylesheet["'][^>]*href=["'][^"']*style\.css["'][^>]*>\s*/i;
if (!linkRegex.test(html)) {
  process.exit(0);
}

const inlinedHtml = html.replace(linkRegex, `<style>${css}</style>\n`);
writeFileSync(htmlPath, inlinedHtml, "utf8");
unlinkSync(cssPath);
