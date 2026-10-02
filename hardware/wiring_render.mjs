// Renders the figures of wiring.html to the PNGs the README embeds.
//
// Both themes are shot: GitHub serves one or the other from the <picture>
// element, so a dark-mode reader does not get a white slab.

import { chromium } from 'playwright';
import { mkdir } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';

const page_url = new URL('./wiring.html', import.meta.url);
const out_dir = new URL('./img/', import.meta.url);

await mkdir(out_dir, { recursive: true });

// The montage, and the board it gets soldered onto.
const FIGURES = [
  ['wiring', '#montage .figwrap'],
  ['permaproto', '#permaproto'],
];

// Playwright ships its own Chromium, which is what CI uses. A distribution it
// has no build for -- Ubuntu 26.04, here -- falls back to a system install:
//   WIRING_CHROME=chrome node hardware/wiring_render.mjs
const channel = process.env.WIRING_CHROME;
const browser = await chromium.launch(channel ? { channel } : {});

for (const scheme of ['light', 'dark']) {
  const ctx = await browser.newContext({
    colorScheme: scheme,
    deviceScaleFactor: 2,
    viewport: { width: 1280, height: 1000 },
  });
  const page = await ctx.newPage();
  await page.goto(page_url.href, { waitUntil: 'networkidle' });
  // Without this the shot lands on the fallback font and every label shifts.
  await page.evaluate(() => document.fonts.ready);

  for (const [name, selector] of FIGURES) {
    const out = new URL(`${name}-${scheme}.png`, out_dir);
    await page.locator(selector).screenshot({
      path: fileURLToPath(out),
      scale: 'device',
    });
    console.log(`hardware/img/${name}-${scheme}.png`);
  }
  await ctx.close();
}

await browser.close();
