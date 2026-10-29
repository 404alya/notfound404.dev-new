// The automatable part of step 5 of the verify checklist, with Playwright.
//
// Usage (see SKILL.md for serving the builds and installing Playwright):
//   PLAYWRIGHT_DIR=<dir with node_modules/playwright> \
//     bun browser-check.mjs --dev http://localhost:8080/ [--prod http://localhost:8081/]
//                           [--save-globals FILE] [--globals-baseline FILE]
//
// Per mode, at desktop (1280x900) and mobile (390x844):
//   - every page loads with no pageerror / console error (the 404 page's own
//     status at a missing URL is expected; errors from other origins are
//     listed as "external", not failures), and the cover lifts;
//   - zero layout-shift entries after the cover lifts;
//   - globals on window beyond an empty same-origin page's (listed; compared with
//     --globals-baseline if given, saved with --save-globals);
//   - headings: <page>#<id> lands the heading ~96 px from the top after the
//     cover lifts, or the page is scrolled to its end (home ids loaded 3
//     times each);
//   - prod: /, /articles, /article/<slug>, /nginx.conf (404) and a deep
//     missing URL (404 status, no failed /assets/ request, fonts loaded);
//   - articles list for locales tr-TR, en-US, de-DE and with JS disabled:
//     which card of each translation group is visible;
//   - external requests blocked: pages still lift and code is highlighted.
// Everything else in step 5 (interactions, logo, back/forward) stays manual.

import { createRequire } from 'node:module';
import { readFileSync, writeFileSync } from 'node:fs';

const require = createRequire(`${process.env.PLAYWRIGHT_DIR ?? process.cwd()}/`);
const { chromium } = require('playwright');

const args = Object.fromEntries(
  process.argv.slice(2).reduce((acc, a, i, all) => (a.startsWith('--') ? [...acc, [a.slice(2), all[i + 1]]] : acc), []),
);
if (!args.dev && !args.prod) {
  console.error('usage: browser-check.mjs --dev URL [--prod URL] [--save-globals FILE] [--globals-baseline FILE]');
  process.exit(2);
}

const VIEWPORTS = { desktop: { width: 1280, height: 900 }, mobile: { width: 390, height: 844 } };
const HEADING_TOP = 96;
const failures = [];
const fail = (msg) => { failures.push(msg); console.log('FAIL ', msg); };
const ok = (msg) => console.log('ok   ', msg);
const info = (msg) => console.log('info ', msg);
const allGlobals = new Set();

// Records layout shifts and the time #page-transition gets `lifted`, under a
// symbol so it adds no name to window.
const INIT = () => {
  const s = (window[Symbol.for('verify')] = { shifts: [], lift: null });
  new PerformanceObserver((list) => {
    for (const e of list.getEntries()) s.shifts.push({ t: e.startTime, v: e.value, input: e.hadRecentInput });
  }).observe({ type: 'layout-shift', buffered: true });
  new MutationObserver(() => {
    const cover = document.getElementById('page-transition');
    if (s.lift === null && cover?.classList.contains('lifted')) s.lift = performance.now();
  }).observe(document, { subtree: true, attributes: true, attributeFilter: ['class'], childList: true });
};

const browser = await chromium.launch({ channel: 'chromium' });

async function newContext(opts = {}) {
  const ctx = await browser.newContext(opts);
  await ctx.addInitScript(INIT);
  return ctx;
}

// Loads url, waits for the lift plus the fade and a settle time, returns
// what happened.
async function load(ctx, url, { expect404 = false } = {}) {
  const page = await ctx.newPage();
  const errors = [], external = [], failedAssets = [];
  const origin = new URL(url).origin;
  page.on('pageerror', (e) => errors.push(`pageerror: ${e.message}`));
  page.on('console', (m) => {
    if (m.type() !== 'error') return;
    const where = m.location()?.url ?? '';
    if (expect404 && /status of 404/.test(m.text()) && (where === '' || where === url)) return;
    (where && !where.startsWith(origin) ? external : errors).push(m.text());
  });
  page.on('response', (r) => {
    if (r.url().includes('/assets/') && r.status() >= 400) failedAssets.push(`${r.status()} ${r.url()}`);
  });
  const response = await page.goto(url, { waitUntil: 'load' });
  let lifted = true;
  try {
    await page.waitForFunction(() => window[Symbol.for('verify')].lift !== null, null, { timeout: 8000 });
  } catch {
    lifted = false;
  }
  const fade = Number(await page.getAttribute('#page-transition', 'data-fade-ms').catch(() => null)) || 300;
  await page.waitForTimeout(fade + 1200);
  const state = await page.evaluate(() => window[Symbol.for('verify')]);
  const shiftsAfter = state.shifts.filter((s) => state.lift !== null && s.t > state.lift && !s.input);
  return { page, response, errors, external, failedAssets, lifted, shiftsAfter };
}

// An empty page on the site's origin (a secure context, like the pages), so
// only names the site's scripts add are left over.
async function blankGlobals(ctx, base) {
  const url = new URL('__verify_blank__', base).href;
  await ctx.route(url, (route) => route.fulfill({ contentType: 'text/html', body: '<!doctype html><title>blank</title>' }));
  const page = await ctx.newPage();
  await page.goto(url);
  const keys = new Set(await page.evaluate(() => Object.keys(window)));
  await page.close();
  await ctx.unroute(url);
  return keys;
}

async function articleUrls(base, prod) {
  const ctx = await newContext();
  const page = await ctx.newPage();
  await page.goto(new URL(prod ? 'articles' : 'articles.html', base).href);
  const hrefs = await page.$$eval('a[href*="article/"]', (as) => as.map((a) => a.getAttribute('href')));
  await ctx.close();
  const slugs = [...new Set(hrefs.map((h) => h.match(/article\/([^/.#?]+)/)?.[1]).filter(Boolean))];
  return slugs.map((s) => new URL(prod ? `article/${s}` : `article/${s}.html`, base).href);
}

async function checkPages(mode, base) {
  const prod = mode === 'prod';
  const articles = await articleUrls(base, prod);
  const pages = prod
    ? ['', 'index.html', 'articles', 'articles.html', '404.html'].map((p) => new URL(p, base).href).concat(articles)
    : ['index.html', 'articles.html', '404.html'].map((p) => new URL(p, base).href).concat(articles);

  for (const [vpName, viewport] of Object.entries(VIEWPORTS)) {
    const ctx = await newContext({ viewport });
    const blank = await blankGlobals(ctx, base);
    for (const url of pages) {
      const r = await load(ctx, url, { expect404: url.endsWith('404.html') });
      const tag = `${mode} ${vpName} ${url.replace(base, '/')}`;
      const globals = (await r.page.evaluate(() => Object.keys(window))).filter((k) => !blank.has(k));
      globals.forEach((g) => allGlobals.add(g));
      if (!r.lifted) fail(`${tag}: cover never lifted`);
      if (r.errors.length) fail(`${tag}: console errors: ${JSON.stringify(r.errors)}`);
      if (r.external.length) info(`${tag}: external errors (API): ${JSON.stringify(r.external)}`);
      if (r.shiftsAfter.length) fail(`${tag}: ${r.shiftsAfter.length} layout shift(s) after the lift: ${JSON.stringify(r.shiftsAfter)}`);
      if (r.failedAssets.length) fail(`${tag}: failed assets: ${r.failedAssets}`);
      if (r.lifted && !r.errors.length && !r.shiftsAfter.length) ok(`${tag}: no errors, no shift after the lift`);
      await r.page.close();
    }

    if (prod) {
      for (const p of ['a/b/c', 'article/x/y']) {
        const r = await load(ctx, new URL(p, base).href, { expect404: true });
        const fonts = await r.page.evaluate(async () => {
          await document.fonts.ready;
          return [...document.fonts].filter((f) => f.status === 'loaded').map((f) => f.family);
        });
        const tag = `prod ${vpName} /${p}`;
        if (r.response.status() !== 404) fail(`${tag}: status ${r.response.status()}, expected 404`);
        if (r.failedAssets.length) fail(`${tag}: failed assets: ${r.failedAssets}`);
        if (!fonts.length) fail(`${tag}: no font loaded`);
        if (r.response.status() === 404 && !r.failedAssets.length && fonts.length)
          ok(`${tag}: 404 page with its assets and fonts (${[...new Set(fonts)]})`);
        await r.page.close();
      }
      const conf = await ctx.request.get(new URL('nginx.conf', base).href);
      (conf.status() === 404 ? ok : fail)(`prod ${vpName} /nginx.conf: status ${conf.status()}`);
    }

    // Headings land ~96 px from the top.
    const home = new URL(prod ? '' : 'index.html', base).href;
    const targets = [[home, 3]].concat(articles.map((a) => [a, 1]));
    for (const [url, repeats] of targets) {
      const probe = await (await ctx.newPage());
      await probe.goto(url);
      const ids = await probe.$$eval('.prose h2[id], .prose h3[id], .prose h4[id], .prose h5[id], .prose h6[id]', (hs) => hs.map((h) => h.id));
      await probe.close();
      const picked = url === home ? ids : ids.slice(0, 2).concat(ids.slice(-1));
      const off = [];
      for (const id of picked) {
        for (let i = 0; i < repeats; i++) {
          const r = await load(ctx, `${url}#${id}`);
          // A heading near the end can't scroll up to 96 px: then the scroll
          // root must be at its bottom with the heading below 96 px.
          const { top, atEnd } = await r.page.evaluate((id) => {
            const root = document.querySelector('[data-scroll-root]') ?? document.scrollingElement;
            return {
              top: document.getElementById(id).getBoundingClientRect().top,
              atEnd: root.scrollTop + root.clientHeight >= root.scrollHeight - 2,
            };
          }, id);
          const fine = Math.abs(top - HEADING_TOP) <= 4 || (atEnd && top > HEADING_TOP);
          if (!fine) off.push(`#${id}: ${Math.round(top)} px`);
          await r.page.close();
        }
      }
      const tag = `${mode} ${vpName} ${url.replace(base, '/')} headings`;
      off.length ? fail(`${tag}: not at ${HEADING_TOP} px: ${off}`) : ok(`${tag}: ${picked.length} id(s) at ${HEADING_TOP} px`);
    }
    await ctx.close();
  }

  // Articles list per locale, and without JS.
  const list = new URL(prod ? 'articles' : 'articles.html', base).href;
  const cases = [
    ['tr-TR', { locale: 'tr-TR' }, (c) => c.lang === 'tr'],
    ['en-US', { locale: 'en-US' }, (c) => c.lang === 'en'],
    ['de-DE', { locale: 'de-DE' }, (c) => c.fallback],
    ['no JS', { javaScriptEnabled: false }, (c) => c.fallback],
  ];
  for (const [name, opts, expected] of cases) {
    const ctx = await newContext(opts);
    const page = await ctx.newPage();
    await page.goto(list);
    await page.waitForTimeout(1500);
    const cards = [];
    // A card is a child of #articles-box; its link sits in the h2 title.
    for (const card of await page.locator('#articles-box > *').all()) {
      cards.push({
        group: await card.getAttribute('data-group'),
        lang: await card.getAttribute('data-lang'),
        fallback: (await card.getAttribute('data-fallback')) !== null,
        visible: await card.isVisible(),
        href: await card.locator('h2 > a').getAttribute('href'),
      });
    }
    const h1s = await page.locator('h1').count();
    if (h1s !== 1) fail(`${mode} articles ${name}: ${h1s} h1 elements, expected 1`);
    const unique = [...new Map(cards.map((c) => [c.href ?? c.group + c.lang, c])).values()];
    const wrong = unique.filter((c) => (c.group ? c.visible !== expected(c) : !c.visible));
    const shown = unique.filter((c) => c.visible).map((c) => c.href);
    wrong.length
      ? fail(`${mode} articles ${name}: wrong visibility: ${JSON.stringify(wrong)}`)
      : ok(`${mode} articles ${name}: ${unique.length} cards in the HTML, shown: ${shown}`);
    await ctx.close();
  }

  // External requests blocked.
  const ctx = await newContext();
  await ctx.route('**/*', (route) => {
    const u = new URL(route.request().url());
    return ['localhost', '127.0.0.1'].includes(u.hostname) ? route.continue() : route.abort();
  });
  for (const url of [new URL(prod ? '' : 'index.html', base).href, ...articles]) {
    const r = await load(ctx, url);
    const code = await r.page.evaluate(() => [document.querySelectorAll('pre code').length, document.querySelectorAll('pre code.hljs').length]);
    const tag = `${mode} offline ${url.replace(base, '/')}`;
    if (!r.lifted) fail(`${tag}: cover never lifted`);
    else if (code[0] !== code[1]) fail(`${tag}: ${code[1]}/${code[0]} code blocks highlighted`);
    else ok(`${tag}: renders, ${code[0]} code block(s) highlighted`);
    await r.page.close();
  }
  await ctx.close();
}

for (const mode of ['dev', 'prod']) if (args[mode]) await checkPages(mode, args[mode].replace(/\/?$/, '/'));
await browser.close();

const globals = [...allGlobals].sort();
info(`globals beyond an empty page: ${JSON.stringify(globals)}`);
if (args['save-globals']) writeFileSync(args['save-globals'], JSON.stringify(globals, null, 2));
if (args['globals-baseline']) {
  const before = new Set(JSON.parse(readFileSync(args['globals-baseline'], 'utf8')));
  const added = globals.filter((g) => !before.has(g));
  added.length ? fail(`new globals on window: ${added}`) : ok('no new globals on window');
}
console.log(`\n${failures.length} failure(s)`);
process.exit(failures.length ? 1 : 0);
