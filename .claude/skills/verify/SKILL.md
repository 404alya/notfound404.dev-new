---
name: verify
description: Verification checklist for this site generator, to run before saying a change is done. Use after any change to main.c, js/, css/, config/, articles/ or assets/, or when asked to verify, test, check, screenshot or review the built site: both build modes, gcc, ASan, output diffs, prod URL and JSON-LD checks, nginx -t, broken-translation build, Playwright browser checks (console errors, layout shift after the cover lifts, heading anchors, articles list per locale, globals, offline), and the visual screenshot checklist. Has helper scripts in scripts/.
---

# Verification checklist (run before saying "done")

Run steps 1–5 for **both** build modes. Build into `out/`, copy it aside
(`cp -a out /tmp/out-dev`, `/tmp/out-prod`), and finish with a dev build so
`out/` is left in dev mode.

## Helper scripts

They cover steps 1–4 and the automatable half of step 5. Run them from the
project root; `$S` below is a scratch directory. They don't replace the manual
items (interactions, logo, back/forward, screenshots), and they print each
check, so report what they print.

```sh
.claude/skills/verify/scripts/build-check.sh $S/verify       # steps 1-3; leaves $S/verify/out-dev, out-prod; out/ ends in dev
python3 .claude/skills/verify/scripts/check-output.py $S/verify/out-dev $S/verify/out-prod \
    [--before-dev OLD_DEV_OUT] [--before-prod OLD_PROD_OUT]   # step 4 (all but nginx -t)
.claude/skills/verify/scripts/broken-translation.sh          # step 4, translations (builds patched copies of main.c in a temp dir)

# step 5: serve both builds, then drive them with Playwright
python3 -m http.server 8080 --bind 127.0.0.1 --directory $S/verify/out-dev &
python3 .claude/skills/verify/scripts/serve.py $S/verify/out-prod 8081 &   # prod routing stand-in
(mkdir -p $S/pw && cd $S/pw && bun init -y && bun add playwright)        # once
PLAYWRIGHT_DIR=$S/pw bun .claude/skills/verify/scripts/browser-check.mjs \
    --dev http://localhost:8080/ --prod http://localhost:8081/ \
    [--save-globals FILE] [--globals-baseline FILE]
```

- For "only the intended pages changed" and "dev output unchanged", build the
  version from before the change first (e.g. `git stash`, run
  `build-check.sh $S/before`, `git stash pop`) and pass its `out-dev` /
  `out-prod` as `--before-dev` / `--before-prod`.
- For "no new globals", run `browser-check.mjs --save-globals $S/globals.json`
  on the build from before the change and `--globals-baseline
  $S/globals.json` after it. The pages currently add no names to `window`.
- `browser-check.mjs` accepts a heading below 96 px only when the scroll root
  is at its end (the last headings of a short page can't scroll further up).
- `serve.py` follows `config/nginx.conf`'s routing; it is the "stand-in" of
  step 5, not a replacement for `nginx -t`.

## 1. Build

`./build.sh ./deps/htmc/htm.c main.c` and
`./build.sh ./deps/htmc/htm.c main.c --prod` → no warnings, all pages
written (`out/nginx.conf` only in prod).

## 2. gcc

`gcc -Wall -Wextra ./deps/htmc/htm.c main.c -o /tmp/g.o` → no warnings.

## 3. ASan

See "Memory checks" in `CLAUDE.md`, in both modes → exit 0, no reports.

## 4. Output

If the change shouldn't affect markup, `diff -r` the old and new
`out/` and confirm only the intended pages changed.

- Dev output must not depend on mode plumbing: `diff -r` a dev build
  against one from before a mode-only change → identical, and no
  `out/nginx.conf`.
- Prod: no relative URL in canonical / `og:` / hreflang / JSON-LD:
  `grep -rhoE '(href|content)="[^"]*"' out --include='*.html' | grep -E 'canonical|og:'`
  and the JSON-LD `"@id"` / `"url"` values all start with
  `https://notfound404.dev/`. The only relative `src`/`href` left in prod
  are the template literals in `js/skills.js` (see "Base path" in the
  `htmc-c-rules` skill):
  `grep -rhoE '(src|href)="[^"]*"' out --include='*.html' | grep -vE '="(/|https?:|#|mailto:)'`.
- Prod: `nginx -t` on `out/nginx.conf`. It needs an `http` context and,
  since the certificate lines are commented out, test certificates. Copy it,
  uncomment `ssl_certificate*` pointing at a throwaway self-signed pair, set
  `root`, and wrap it: `events {} http { include /tmp/test-site.conf; }` →
  `nginx -t -c /tmp/test-nginx.conf`. nginx isn't installed on this machine
  (`pacman -S nginx`, or run the check in a container).
- Every `application/ld+json` block parses (`python3 -m json.tool` or
  `json.loads`) and each page has exactly one.
- Article bodies: undoing the heading transform gives the body file back
  byte for byte; `<html lang>` matches each article's `lang`.
- After touching translations, also build once with a deliberately broken
  translation (unknown slug, same lang) and confirm `die_msg()` stops it.

## 5. Browser

Serve the dev `out/` with `python3 -m http.server`; serve prod
from the site root with nginx, or a stand-in that follows the config's
routing: `try_files $uri $uri.html`, `${uri}index.html` for trailing
slashes, `404.html` with status 404 at any depth, `/nginx.conf` → 404.
Drive with Playwright and Chromium; see "Playwright notes" below.

- no `pageerror` or console errors on any page (Chrome logs the 404
  page's own status when it is served for a missing URL; that one is
  expected);
- in prod, also `/`, `/articles`, `/article/<slug>` and a deep missing URL
  (`/a/b/c`): the 404 page there loads its fonts and assets;
- interactive pieces work: theme toggle (cross-fades over 400 ms; instant
  with reduced motion) and saved preference, mobile menu,
  nav hide/show on scroll, progress bar, skills tooltip (hover, desktop),
  skill drawer (tap with touch emulation: opens from the bottom, closes on the
  backdrop, the × button, Escape and a swipe down; a short pull springs back;
  never opens on desktop), skill icons scale to 0.9 while pressed, category filter,
  shrink button, presence open/close (when it's on the page), repos list
  (when it's on a page), gameproject2 slider (both arrows and the
  left/right keys cycle every slide and wrap; a hidden video is paused and
  the shown one plays; the box height never changes), code highlighting,
  internal link fade-out/fade-in, and back/forward;
- article headings: opening `article/<slug>.html#<id>` directly lands the
  heading ~96 px from the top (under the navbar) after the cover lifts;
  clicking a heading updates the hash with no fade; back/forward between
  heading entries restore the scroll position; language links fade like
  other internal links;
- home headings: the same four checks for `index.html#<id>` with every id
  (`who-am-i`, `experience`, `knowledge-center`, and `gameproject2` in dev only) at both
  viewports, landing at 96 px every time (repeat the loads: this used to
  be intermittent). Heading links inherit colour and weight, have
  `cursor: pointer`, and show the `#` on hover without changing the
  heading's height;
- logo: on every page the navbar logo draws on after the cover lifts and
  ends complete, with the same box before and after (none of it animates
  under reduced motion); following a link mid-draw keeps it drawing
  during the fade-out; the 404 page's logo does the same, also at a
  deep missing URL in prod; in prod the navbar logo links to `/`;
- "Who am I?" (desktop nav and mobile menu): on `index.html` *and* on `/`
  it scrolls to the heading with no fade and no reload, also when clicked
  again with the same hash, and back restores the scroll. The mobile menu
  closes. From `articles.html` and `article/*.html` it fades, then lands at
  96 px;
- nav links are Pixelify at 16 px (mobile) / 18 px (desktop); the nav box
  ends 64 px from the top and only the links group moves compared with
  before;
- articles list, loaded with Playwright's `locale` set to: `tr-TR` → the
  Turkish card of a translated group is shown and the English one hidden;
  `en-US` → the English card; `de-DE` → the English fallback; JS disabled →
  the English card. In every case each article without translations is
  still visible, every card is still in the HTML, there are no console
  errors, and zero `layout-shift` after the cover lifts at both viewports;
- no new globals on `window`;
- zero `layout-shift` entries timestamped after the cover lifts, at desktop
  (1280×900) and mobile (390×844);
- with external requests blocked, the pages still render and highlight
  (only the API data is missing).

## 6. Visual check

Take screenshots in light and dark themes at both viewports
and confirm the intended design:

- light-mode code blocks are readable;
- the 404 page is styled, with the white logo above "404 NOT FOUND :)"
  (mid-draw and finished screenshots);
- the nav hides while scrolled down;
- text follows the text-size rule ("Text sizes" in the `frontend-tailwind`
  skill), and the skill tooltip and
  presence panel are wide enough to fit it;
- the Experience section sits above "Knowledge Center";
- prod: the home page has no gameproject2 section ("Knowledge Center" is
  the last one);
- dev: the "gameproject2" section below "Knowledge Center": the yellow
  "In development" pill at the right end of the heading row (lighter in
  the light theme), three paragraphs describing the game (loneliness, the
  gun, radar and flashlight), and a 16:9 slider of videos and images (a
  looping video first) with round previous/next arrows over its edges and
  a rotating yellow border (static under `prefers-reduced-motion`); slides
  switch at once, with no fade;
- the articles list is ordered newest to oldest;
- Turkish articles (`<html lang="tr">`) render their Pixelify headings
  with the font's Turkish glyph variants;
- a `#` appears after a heading on hover (article headings and the home
  page's section headings), and articles with translations
  show the language buttons under the title;
- the navbar's "Articles" and "Who am I?" links are in Pixelify (the
  links group is centred). "Who am I?" goes to the home page's "Who am I?"
  heading;
- AI translation notice on AI-translated articles (a muted italic line
  under the language buttons);
- every article shows its publication date (muted, small) right under the
  title, in the page's language;
- article-body links are white in the dark theme
  (`dark:prose-a:text-white` in `article_prose_class`; heading links keep
  the heading colour);
- the page-transition cover is `#202020` in the dark theme;
- the articles list shows one card per translation group (the visitor's
  language, else English);
- the home hero's pink glow is animated (`hero_shader()`): colour flows
  slowly through it (towards peach and lavender tints), a soft sheen band
  crosses now and then, and every 8-15 s thin iridescent ridges (gold to
  cyan) run along the wave crests; phases and timings are random per load.
  Its static frame (reduced motion, or no WebGL) is the CSS gradient.
  Screenshots of the animated hero differ from frame to frame; compare
  palette and placement, or use `reducedMotion: 'reduce'`.

Investigate anything unexpected.

## 7. Report

Report results truthfully. If a check was skipped or failed, say so and
include the output.

## Playwright notes

The Chromium browsers are cached in
`~/.cache/ms-playwright`. Install the package in a scratch directory with
`bun add playwright`. Launch with `chromium.launch({ channel: 'chromium' })`:
the default headless shell drops the alpha channel of VP9 WebM videos, so the
Experience logo shows as a solid box there even though real browsers render it
correctly. Muted autoplay only starts once the video is scrolled into view
(this applies to the Experience logo and the gameproject2 video alike).
Wait for the cover to lift and scroll the slider into view before checking
that its active video plays.

The default headless Chromium renders WebGL with SwiftShader and a software
compositor, which reads the hero canvas back every frame and logs
"GPU stall due to ReadPixels" console *warnings* (not errors) from the driver.
To test the hero shader on the real GPU, launch with
`args: ['--enable-gpu', '--ignore-gpu-blocklist', '--use-gl=angle', '--use-angle=gl']`.
