# Prompt instructions

Read this before changing anything in this folder. It lists the rules, traps and
checks that keep the generator correct, leak-free and visually consistent.

This project is a C static site generator. `main.c` builds every page of
notfound404.dev with the [htmc](deps/htmc/README.md) library and writes it to
`out/`.

---

## 1. Build and run

Always run from the project root:

```sh
./build.sh ./deps/htmc/htm.c main.c          # development build (the default)
./build.sh ./deps/htmc/htm.c main.c --prod   # production build, also writes out/nginx.conf
```

- `build.sh` passes `--dev` / `--prod` to the generator (`./output.o --dev`,
  `./output.o --prod`) and every other argument to clang. Without a flag the
  build is dev. Any other argument to `output.o` stops it with a usage error.
  See "Build modes" in section 4 for what differs.
- `main.c` includes only `deps/htmc/htmc.h`. `htm.c` must be passed to the
  compiler. Never `#include "htm.c"` in `main.c`: that causes "multiple
  definition" link errors.
- The build must print **zero warnings** under `-Wall -Wextra` (clang via
  `build.sh`, and also gcc).
- Every page listed in section 6 must be written to `out/` at its listed
  path.

## 2. Memory checks (required after every C change)

```sh
clang -Wall -Wextra -g -fsanitize=address,undefined ./deps/htmc/htm.c main.c -o /tmp/asan.o
/tmp/asan.o > /dev/null; echo "exit=$?"
/tmp/asan.o --prod > /dev/null; echo "exit=$?"
```

- Run it in both modes: prod builds extra strings (`share_meta()`,
  `write_nginx_conf()`) that dev never touches. The last run decides what's in
  `out/`, so rebuild with the mode you want to leave behind.
- Expected: exit `0` and no `LeakSanitizer` / `AddressSanitizer` /
  `runtime error` output. LeakSanitizer is on by default with ASan on Linux.
- Valgrind does not work on this machine (glibc has no debug symbols). Use ASan.
- Do not report "no leaks" without running this.

### Ownership rules

- Every function returning `char *` returns a heap string **owned by the caller**.
- Inside an `htmc(...)` expression, wrap such calls in `own(...)`:
  `div(own(navbar(base)))`. `own()` hands the string to the enclosing `htmc()`,
  which frees it after copying it into the parent.
- Parameters typed `char *` (not `const`) are taken over the same way
  (see `page_shell(title, base, head_extra, body)`). `const char *` means borrowed.
- Never `own()` a string literal or a buffer you still need: it will be freed.
- **Never use an `own()`ed string in another argument of the same htmc
  expression.** Argument evaluation order is unspecified, and once a buffer has
  been consumed htmc may reuse or free it. If a value is needed twice, compute
  the derived value into a local variable *before* the `htmc(...)` call.
- Heap strings used outside htmc (paths, titles) are freed explicitly with
  `free()` after use (see `page_article()`).
- `write_page(path, html)` frees `html`. Don't free it again.

### No fixed-size buffers

- Do not declare `char buf[N]`, `char path[4096]` and similar. Size everything
  to the data:
  - formatted strings: `format_string(fmt, ...)` (heap, exact size, exits on
    malloc failure);
  - files: `read_file(path)` / `read_file_sized(path, &len)` (heap, sized to
    the file, NUL-terminated);
  - inside htmc: `htmc_fmt(...)`.
- Text built piece by piece outside htmc (JSON, the heading transform) goes
  through `StrBuf` (`strbuf_new()`, `strbuf_append()`, `strbuf_append_n()`,
  `strbuf_putc()`): a growable heap string, always NUL-terminated; return its
  `.data` (the caller owns it). Lists of strings grow with `realloc` the same
  way (see `IdList`).
- Check `malloc`/`fopen`/`fclose`/`fwrite` results. On fatal build errors,
  `die()` is acceptable (it calls `perror`); for errors in the site's own data,
  where errno means nothing, use `die_msg(fmt, ...)`. Never pass data as its
  format. Page writers return `-1` and `main()` aggregates status.

## 3. htmc pitfalls

- **System headers go before `htmc.h`.** htmc defines function-like macros
  named after HTML tags (`link`, `select`, `time`, `data`, `label`, `table`,
  `a`, `b`, `i`, `p`, `s`, `u`, `q`, `div`, `header`, `footer`, `title`,
  `code`, `small`, ...). They break headers included after it.
- Don't name functions, or call anything, with a tag name followed by `(`.
  That's why the closing section is `final_words()` and not `footer()`, and
  why parameters use names like `entry` and `page_title`.
  `main` has no macro: use `attr(main, ...)(...)` for `<main>`.
- htmc takes `char *`. Passing `const char *` warns. Pass const data through
  `htmc_fmt("%s", value)` or inside a format.
- Static attributes: `htmc_strlit(class="flex p-2" id="x")` stringizes the
  tokens, so the quotes are kept. Its contents must tokenize: no unbalanced
  `'` outside a quoted value.
- Never put `%` from data into a format string. Class names like `w-[90%]`
  are fine inside `htmc_strlit`, but inside `htmc_fmt` they must be passed as
  `%s` arguments.
- Elements with no children need an explicit empty string: `attr(div, ...)("")`.
- **Known htmc bug (void tags):** `htmc_make_tag_with_attrs()` (used by
  `meta`, `link`, `img`, `input`, …) grows its buffer only when
  `size + attr_len + 1 >= cap`, then writes `" " + attr + ">\0"`. When the
  attributes end exactly two bytes short of the capacity, it writes one byte
  past the buffer (ASan: heap-buffer-overflow in `htm.c`). Existing calls
  happen to miss that length; new `meta()`/`link()` calls for `share_meta()`
  hit it. For void tags whose attribute lengths depend on data, format the
  tag with `format_string()` (see `share_meta()`). Don't patch `deps/htmc/`.
  ASan in both build modes catches it.
- Loops: `htmc_ccode(for (...) htmc_yield(own(component(...)));)`. Don't nest
  two `htmc_ccode` blocks in one `htmc()`.
- Text and attribute data are inserted **without escaping**. Keep data
  trusted, or escape `& < > "` before inserting.
- Article bodies (`articles/*.html`) are pre-escaped HTML inserted verbatim.
  Never re-escape or reformat them; code blocks depend on exact whitespace.
  **One narrow exception:** `link_headings()` runs on the body between
  `read_file()` and insertion. It touches only `<h2>`–`<h6>` tags (outside
  HTML comments): it adds `id="..."` right after `<hN` (unless the heading
  already has an id, which is kept) and wraps the heading's content in
  `<a href="#id">…</a>` (unless the content already holds a link). Every other
  byte is copied unchanged. Don't extend it to other tags, and don't edit the
  body files or use JS to do it. After changing it, check that removing the
  added `id` and `<a>` from the output gives back the body file byte for byte.
  - Ids come from the heading text (`heading_slug()`): tags dropped, ASCII
    letters and digits lowercased, Turkish letters transliterated
    (ç→c, ğ→g, ı→i, ö→o, ş→s, ü→u, uppercase too), every other run of
    characters (spaces, punctuation, entities, other UTF-8) → one `-`, never
    leading or trailing, `section` if nothing is left. Repeats get `-2`,
    `-3`, …; existing ids are reserved first so generated ones never clash.
  - The home page's section headings are generated by C, so they are **not**
    run through `link_headings()` (never run it on a whole page). They come
    already linked from `home_heading()`; see "Linkable headings" in section 5.

## 4. Code structure and style

- **One C file.** All generator code lives in `main.c`: no runtime server, no
  extra build tooling. Supporting inputs are read at build time.
- **Big contents live in their own files, not in C strings.** A script,
  stylesheet or config file goes in `js/`, `css/` or `config/` and is read
  at build time (`inline_script()`, `inline_style(css, tailwind)`,
  `read_file()`). C keeps only markup, short component-specific rules (a few
  lines, like the page-transition cover's CSS or a component's keyframes) and
  templates built from escaped data (`json_ld()`, `share_meta()`). A value
  that only C knows goes in as an `@NAME@` placeholder, filled by
  `replace_tokens(text, token, value)` (a plain search, not a format string),
  or as a `data-*` attribute that the script reads (`data-fade-ms` on the
  cover). Don't write a placeholder's literal name in a comment of that file:
  it would be replaced too.

  | Path | Purpose |
  | --- | --- |
  | `deps/htmc/` | HTML builder library (don't modify) |
  | `deps/js/tailwind.js` | Tailwind v4 browser build, inlined on every page |
  | `deps/js/highlight.min.js`, `deps/js/highlight-github-dark.min.css` | highlight.js 11.9.0 and its theme, inlined on article pages |
  | `js/*.js` | component scripts, each inlined inside an IIFE |
  | `css/tailwind-config.css` | Tailwind v4 config (`tailwind_config()`), inlined as `<style type="text/tailwindcss">` on every page |
  | `css/base.css` | plain rules Tailwind can't express (`base_styles()`): `@font-face` and scrollbar hiding; `@BASE@` becomes the page's base path |
  | `css/prose.css` | `.prose` defaults and `prose-*` variants (`prose_styles()`), as `text/tailwindcss` on the home and article pages |
  | `config/nginx.conf` | template of `out/nginx.conf` (`write_nginx_conf()`, prod only); `@SITE_NAME@` becomes `site_name` |
  | `js/article-lang.js` | articles list (`articles_list()`): shows one card per translation group, in the visitor's language |
  | `js/hero-shader.js` | home hero glow (`hero_shader()`): WebGL1 shader on `#hero-shader` that redraws `#pink-overlay`'s radial gradients and slowly animates them; the CSS gradients stay on `#pink-overlay` as the fallback until a frame is drawn; WebGL starts after `load` and the motion eases in, so the swap is invisible |
  | `js/page-transition.js` | the page-transition cover's behaviour (`page_transition()`): lift after load, fade-out on internal links, same-page anchors, bfcache; reads the fade duration from the cover's `data-fade-ms`, which C sets from `page_fade_ms` (also used by the cover's CSS transition) |
  | `articles/<slug>.html` | article bodies |
  | `assets/` | every font, image, icon and video the site uses (copied to `out/assets/`) |
  | `js/gameproject2-slider.js` | the gameproject2 slider (`gameproject2_slider()`): moves `data-active` between slides, arrows and left/right keys wrap around, pauses hidden videos and plays the shown one, updates the screen-reader status; inlined only when there are 2+ slides |
  | `assets/video/gameproject2.webm` | the gameproject2 slider's first slide (currently a placeholder test pattern; replace the file, keep the name) |
  | `assets/images/gameproject2/` | the gameproject2 slider's images (`screenshot-1.webp`, `screenshot-2.webp` are 1280×720 placeholders) |

- **One function per page** (`page_home`, `page_articles`, `page_404`,
  `page_article(const Article *)`), all called from `main()`, which also
  parses the mode and, in prod, calls `write_nginx_conf()`.
- `page_shell(page_title, base, lang, graph_node, head_extra, body)`: `lang`
  is the page's ISO 639-1 code for `<html lang>` (`"en"` for the non-article
  pages, `article->lang` on article pages); `graph_node` is an extra JSON-LD
  `@graph` node (taken over, `NULL` except on article pages); `head_extra`
  may be `NULL`.
- **One component per function.** A component emits its own markup, its own
  `<script>` (via `inline_script("js/NAME.js")`) and its own page-specific
  styles.
- **Base path:** every page gets its `base` from `page_base(depth)` (`0` for
  root pages, `1` for `out/article/`): `""` / `"../"` in dev, `"/"` in prod.
  Components that emit internal URLs or asset paths take `base`, and every such
  path must use it. Never hardcode `"../"` or a bare `assets/…`. The one
  exception is `js/skills.js`, which builds icon URLs as `assets/…` relative
  to the home page. That works in both modes because the home page is only
  served at a root-level URL (`/`, `/index.html`, `/index`).

### Build modes

The mode is chosen when the generator runs (`./output.o --dev`, the default, or
`--prod`; see section 1), stored once in `production` by `parse_args()`, and
read wherever output differs:

| | dev | prod |
| --- | --- | --- |
| internal links and assets | relative (`page_base()`: `""`, `"../"`) | root-absolute (`/assets/…`, `/article/…`) |
| canonical, `og:*`, `twitter:card` (`share_meta()`) | not emitted | absolute `https://notfound404.dev/…` on every page except `404.html` |
| hreflang `alternate` links, JSON-LD `@id` / `url` | absolute `site_url` (unchanged) | same |
| `out/nginx.conf` | never written; a stale one is deleted | written by `write_nginx_conf()` |
| home page's gameproject2 section (`gameproject2_section()`) | shown | left out (placeholder content), with its slider script and keyframes; its assets are still copied |

- **Dev is the reference build:** it works from `python3 -m http.server` in
  `out/` (or any directory). A change that only touches the mode plumbing must
  leave dev output byte-identical. Check with `diff -r` against a dev build
  made before the change.
- **Why root-absolute in prod:** nginx serves `404.html` (`error_page 404`)
  at whatever URL was missing, at any depth (`/article/x/y`), where relative
  paths would resolve to `/article/x/assets/…` and break. `try_files` also
  serves `/articles` and `/article/<slug>` without `.html`. Root-absolute paths
  work from all of them. Absolute `https://` URLs are kept for what search
  engines and link previews read (canonical, `og:`, hreflang, JSON-LD), so the
  pages don't depend on the domain for anything else.
- `share_meta(path, og_type, title, description, image)`: canonical +
  `og:type/site_name/title/description/url/image` + `twitter:card`. Text is
  escaped with `escape_attr()`. `og:description` is left out when the
  description is empty. `image` is the article cover (`summary_large_image`)
  or the site icon (`summary`). `404.html` gets none: it stands in for any
  missing URL.
- `out/nginx.conf` (prod only) is one `server` block for `site_name`, to
  `include` from nginx's `http` block. It lives in the web root, so it
  refuses to serve itself (`location = /nginx.conf { return 404; }`). Its
  `root` is the placeholder `/CHANGE/ME/out`. HTTPS is set up with
  `listen 443 ssl` and the `ssl_certificate*` lines commented out (no paths
  hardcoded), plus an 80→443 redirect. `nginx -t` rejects the file until
  certificates are set. Edit the template `config/nginx.conf` (its comments
  explain each choice), not `out/nginx.conf`. `write_nginx_conf()` only reads
  it, replaces `@SITE_NAME@` with `site_name` and writes it out.
  - It has its own `types` block (html, css, js, json, txt, svg, png, jpg,
    webp, ico, woff2, ttf, webm), so it doesn't depend on the host's
    `mime.types`. A `types` block replaces the inherited one, so add any new
    asset extension there.
  - `charset utf-8`; gzip for css/js/json/svg/txt (html always).
  - `Cache-Control: no-cache` for pages, including the 404 page (`always`).
    `public, max-age=2592000` (30 days) for `/assets/`: asset names carry no
    content hash and a video is replaced under the same name, so no
    `immutable`.
  - Routing: `location / { try_files $uri $uri.html =404; }` plus
    `location ~ /$ { try_files ${uri}index.html =404; }`. Not `$uri/`: with
    it, a directory without `index.html` (`/article/`) gives 403 instead of
    the 404 page.
- **Global scripts** (Tailwind config and build, theme) live in
  `global_scripts()` only. Page-specific scripts belong to their component.
- **Theme switch:** `toggleTheme()` in `js/theme.js` flips `<html class="dark">`
  inside `document.startViewTransition()`, so the whole page cross-fades
  between the themes (400 ms, set on `::view-transition-group(root)` in
  `css/tailwind-config.css`; the old/new snapshots inherit it). Browsers
  without view transitions and reduced-motion users switch at once. Don't add
  per-element colour transitions for the theme: they'd fight the cross-fade
  and slow every hover.
- **Scripts never leak globals.** Every inlined script is wrapped in an IIFE.
  After changing JS, `window` must not gain new names.
- **Data-driven content:** add an article by adding one entry to `articles[]`
  and its body file. Every article needs `lang` (ISO 639-1). `articles[]` can
  stay in any order: `page_articles()` sorts a `const Article *` array with
  `qsort` (`compare_newest_first()`: `published` descending, ties by slug).
  Never reorder the source array by hand for display.
- **Translations:** a translation is an ordinary `articles[]` entry with its
  own body file and its own card in the articles list's HTML, but not its own
  *visible* card: the list shows one card per translation group (see below).
  Link it with
  an inline `ArticleTranslation { lang, slug, AI_used }` list ending with `{ 0 }`
  (like `Job.skills` ends with `NULL`), no separate variable:
  `.translations = (const ArticleTranslation[]){ { .lang = "tr", .slug = "<slug>" }, { 0 } },`
  Leave `translations` out when there are none. Declare it on both entries so both pages
  carry the full set of hreflang links and language buttons.
  `AI_used` (optional third field, `false` when left out) describes the
  *target*: `.AI_used = true` means `<slug>` was translated from the
  declaring article with AI. Set it only on the source's link. Use
  designated initializers: positional `{ "tr", "<slug>" }` still means
  `false`, but `-Wextra` warns about the missing field (both compilers).
  Never silence that warning with a pragma around `articles[]`: it hides
  every diagnostic of that kind for all entries in the editor and the build.
  `check_translations()` runs first in `main()` and `die_msg()`s when an
  article has no lang, a translation slug isn't in `articles[]`, a
  translation has the source's lang, its declared lang differs from the
  target article's, both sides of a pair set `AI_used`, or an AI-translated
  article's lang has no text in `ai_translation_notice_text()` (only `en`
  and `tr` so far: add the text there for a new language). Every article
  page gets `article_date()` right under the h1: a muted `<p>` holding
  `<time datetime="YYYY-MM-DD">` with `published_date_text()` (in the page's
  language: "March 28, 2025" for `en`, "28 Mart 2025" for `tr`, the ISO date
  for any other lang; add month names there for a new language). It
  `die_msg()`s on a `published` that isn't `YYYY-MM-DD`. An article page with translations gets
  `language_switcher()` under the date (current language as a highlighted span,
  links to `<base>article/<slug>.html` labelled with the uppercase code);
  an article that some other article lists with `AI_used = true` gets
  `ai_translation_notice()` below it (a muted italic `<p role="note">` in the
  page's language; pages without one render exactly as before);
  every article page gets `alternate_links()` (`<link rel="alternate" hreflang>`
  for itself and each translation) in its head.
- **Articles list, one card per translation group:** a group is an article
  plus every article linked to it through `translations` (either direction,
  followed transitively); `article_group()` computes it from `articles[]`.
  Its id is the alphabetically first slug; its *fallback* is the `en` article,
  else the source (no `AI_used = true` link points at it), else the first slug.
  `article_card()` gives every grouped card `data-group` and `data-lang`, the
  fallback `data-fallback` and every other one `data-hidden` (styled by
  `data-[hidden]:hidden`). Articles without translations get none of these and
  are always shown. `js/article-lang.js`, inlined right after the list so it
  runs before the cover lifts, moves `data-hidden` per group to the card whose
  `data-lang` is the primary subtag of `navigator.language` (lowercased,
  `"en"` when missing), or to the fallback. Every card stays in the HTML for
  crawlers, and the order stays `compare_newest_first()`. Without JS (so
  without Tailwind either), a `<noscript>` rule in `articles_list()` hides
  `[data-hidden]`, so the list shows the fallbacks.
- The gameproject2 section is dev only for now: `page_home()` skips it in prod
  (see "Build modes"). Remove that condition to publish it.
- Slides of the home page's gameproject2 slider go in `gameproject2_slides[]`
  (`kind` `SLIDE_IMAGE` or `SLIDE_VIDEO`, `path` under `assets/`, `alt`: the
  img alt or the video's aria-label), in display order; the first is shown
  before JS runs. Each slide fills the 16:9 box with `object-cover`, so use
  16:9 media. Images are formatted with `format_string()` (void-tag bug,
  section 3). Hidden slides are `invisible opacity-0` (cross-fade, out of the
  accessibility tree); a `<noscript>` rule hides them, and the arrows carry
  `hidden` until the script removes it. With one slide there are no arrows,
  no status and no script.
- Jobs for the home page's Experience section go in
  `jobs[]` (rendered by `experience_section()` / `job_card()`; a `url` makes
  the card an external link to the company; the duration is
  computed at runtime by `js/experience.js`, never hardcoded, and refreshes
  itself for a current job); social profiles
  go in `social_links[]` or `extra_profiles[]`; site identity goes in the `site_*` / `author_*` constants
  (they feed the meta description and the JSON-LD).
- Readable C: short functions, `const char *` for borrowed inputs, minimal new
  macros (only `own()` exists beyond htmc), and comments that explain *why*.

## 5. Front-end rules

### Tailwind (v4.3.3 browser build, not v3)

- The config is `css/tailwind-config.css`, inlined as a
  `<style type="text/tailwindcss">` block (`tailwind_config()`):
  `@custom-variant dark`, `@theme` breakpoints `xxs`/`xs`, fonts
  `sans`/`pixelify`, and `animate-fade`. `tailwind.config = {...}`
  does nothing in v4.
- **No plugins.** The typography plugin can't load, so `css/prose.css`
  (`prose_styles()`) re-implements the `.prose` defaults and the `prose-*:`
  variants. Add a variant there if a new `prose-xxx:` class is needed.
- v3→v4 differences to watch: `backdrop-blur-sm`(v3) = `backdrop-blur-xs`(v4),
  `rounded-sm`(v3) = `rounded-xs`(v4), `flex-shrink-*` → `shrink-*`,
  `outline-none` → `outline-hidden`, `!class` → `class!`, `leading-0` now sets
  line-height 0, and breakpoints are sorted by size.
- `<button>` defaults to `cursor: default` in v4. Every interactive element
  (`<a href>`, `<button>`, `role="button"`, anything with a click listener,
  including markup built in `js/*.js`) carries `cursor-pointer` on the element
  that receives the click. Article-body links get it via `prose-a:cursor-pointer`
  on the prose container, since the body files are inserted verbatim.
- Tailwind first: no static inline `style="..."`. Use utilities, arbitrary
  values (`bg-[image:radial-gradient(...)]`, `[scrollbar-width:none]`) and
  variants (`dark:`, `md:`, `data-[open]:`). Plain `<style>` is only for
  `@font-face` and scrollbar hiding (`css/base.css`), keyframes, the page-transition cover
  (which paints before Tailwind loads), and `<noscript>` fallbacks for state
  that Tailwind variants handle when JS runs (the articles list's
  `[data-hidden]`). Only values computed at runtime may be
  set through `element.style` in JS.
- Keyframes used by a single component live in that component's own plain
  `<style>` (e.g. `gameproject2-spin` in `gameproject2_section()`) and are
  applied with an arbitrary `animate-[name_4s_linear_infinite]` utility.
  Prefix the name with the component to avoid clashes. Shared animations go in
  `@theme` in `css/tailwind-config.css`. Every looping animation has a
  `motion-reduce:` fallback.
- Videos reserve their size before loading (a fixed box or `aspect-video`),
  are `muted loop playsinline autoplay`, and never use `loading="lazy"`.
- **Text sizes:** every sized text element has a mobile size and a `md:`
  desktop size, one step apart on Tailwind's scale (`text-sm md:text-base`,
  `text-xl md:text-2xl`). Desktop never goes below `text-base`, mobile never
  below `text-sm`. Built-in `text-xs`…`text-9xl` only: no arbitrary sizes
  (`text-[0.8rem]`), no `@theme` font-size tokens. `.prose` headings stay in
  `em` so they follow the container's size.
- For state toggled by JS, prefer data attributes plus variants
  (`data-[open]:flex`, `data-[hidden]:-translate-y-24`) over adding and
  removing classes.
- Classes added at runtime are styled in the same frame (verified), so no
  safelist is needed.

### Loading, transitions and flashing

- `page_transition()` is a full-screen cover (markup and plain CSS in C, behaviour
  in `js/page-transition.js`): white, or `#202020` in the dark
  theme. Its own script reads the saved theme (`localStorage['dark-theme']`)
  for the first paint, since `theme.js` applies it only at DOMContentLoaded,
  and re-syncs with `<html class="dark">` before each fade-out. It lifts (the page fades
  in) after `load`, once Tailwind CSS is applied, registered promises have
  settled, fonts are ready and running CSS transitions have finished, or after
  5 s at most. Then it scrolls to the URL's `#anchor` again (`toAnchor`) and
  waits two frames before fading. Clicking an internal link
  fades it back in and then navigates. The duration is `page_fade_ms`.
  - **Transitions:** in Tailwind v4 a bare `duration-*` (no `transition-*`
    property) means `transition-property: all`. When Tailwind's runtime CSS
    first applies, such elements animate from their unstyled values (the
    hero mail button and social links grow for 200 ms). On a busy machine
    that ran past the lift: about half of loaded runs had a layout shift after
    the cover lifted. Waiting for `CSSTransition`s (finite, unlike the
    looping animations) fixes it. Prefer an explicit `transition-opacity` /
    `transition-colors` next to a `duration-*` in new markup.
  - **Two frames:** a font that has just loaded is laid out in the frame
    *after* `fonts.ready` resolves, and rAF callbacks run before that frame's
    layout. One frame lifted the cover in the same frame as the swap (seen
    with the Pixelify nav links).
  - **`toAnchor`:** the browser scrolls to a URL's fragment while the page is
    still unstyled. Tailwind's CSS and the fonts then move the target, so the
    cover scrolls there again (`scrollIntoView()`, which honours `scroll-mt`).
  - **Same page under another name:** the click handler treats `/` and
    `/index.html` as one page, and `/x` and `/x.html` (nginx `try_files`) too
    (`pageKey()`). A link with a hash to the current page never fades. When
    only the name differs (on `/`, a link to `/index.html#id`), it calls
    `location.assign(url.hash)` instead of reloading. `assign`, not
    `location.hash =`, so a repeated click on the same hash still scrolls.
- A component whose async data changes layout must register its first fetch:
  `document.dispatchEvent(new CustomEvent('page:wait', { detail: promise }))`.
  The promise must never reject (catch errors inside it).
- Don't add `loading="lazy"` to above-the-fold or grid images: they would pop
  in after the page fades in.
- There must be **no layout shift after the cover lifts** (see section 7).

### Self-contained assets

- The build never reads from other projects. Every asset lives
  in `assets/`, and pages reference only `assets/...` paths.
- `assets/` holds only files the site uses. A new skill icon, social icon or
  article cover must be added to `assets/images/<type>/` along with its entry,
  otherwise it 404s.

### No external runtime dependencies

- Everything the page needs is inlined or served from `out/assets/`: Tailwind,
  highlight.js and its theme, the Outfit font (self-hosted woff2) and local
  fonts.
- Don't add CDN `<script>`/`<link>` tags. Vendor the file into `deps/` or
  `assets/` and inline it or copy it instead. Check that the file doesn't
  contain `</script>` or `</style>` before inlining it.
- The only allowed runtime network calls are the GitHub repos API, the Lanyard
  presence API, and images those APIs return.

### Navbar

- `nav_links(base, link_class)` emits "Articles" (`<base>articles.html`) and
  "Who am I?" (`<base>index.html#who-am-i`, the id from
  `home_heading_id(HOME_ABOUT)`, so it follows the heading if the text
  changes). They're used in both the desktop bar and the mobile menu. Both
  links are `font-pixelify` with `text-base md:text-lg`; the logo isn't.
  The nav box stays 56 px tall, 8–64 px from the top. Pixelify only widens
  the links group, which stays centred.
- `js/nav.js` closes the mobile menu when one of its links is clicked: an
  in-page link like "Who am I?" on the home page doesn't unload the page.

### Linkable headings (article pages and the home page)

- `heading_link_styles()` adds the `prose-heading-link` variant
  (`h2`–`h6` `> a[href^='#']`) in the head of article pages and the home page.
  `heading_link_class` puts its utilities on every `.prose` container whose
  headings link to themselves: next to `article_prose_class` on articles, and
  next to `home_prose_class` on each home section. Heading links inherit colour
  and weight and add no
  underline of their own (they use `!` to beat the `prose-a:` utilities; the
  h2's own underline stays), carry `cursor-pointer`, and show an absolutely
  positioned `#` on hover/focus so headings never rewrap.
- `prose-headings:scroll-mt-24` keeps a heading clear of the fixed navbar
  (64 px tall) when scrolled to, from a click or a hash in the URL.
- **Home page:** the section headings are listed in `home_headings[]`, in
  document order and indexed by `HOME_ABOUT`, `HOME_EXPERIENCE`,
  `HOME_SKILLS`, `HOME_GAMEPROJECT2`. `home_heading(index)` emits
  `<h2 id="x"><a href="#x">Text</a></h2>`. `home_heading_id(index)` gives
  the id by the article rules (`heading_slug()`, repeats `-2`, `-3`, …),
  computed from that list alone. That keeps ids deterministic whatever order
  htmc evaluates arguments in, and lets other pages link to them (`nav_links()`).
  Ids: `who-am-i`, `experience`, `knowledge-center`, `gameproject2`. A new
  home section heading gets an entry there, not a bare `h2(...)`.
- `page_transition()` ignores same-page hashes, so heading clicks don't fade.
  `js/heading-links.js` (on article pages and the home page) handles
  back/forward between heading entries. Browsers only restore the window's
  scroll, but both pages scroll inside their `[data-scroll-root]`
  (`#article-scroll`, `#scroll-container`); the script falls back to the
  window if there is none. Before any link with a `#` is followed, it saves
  the root's `scrollTop` in `history.state.scrollRootTop` and restores it on
  `popstate`. It listens in the capture phase, so it saves before
  `page_transition()`'s handler may `location.assign()` the anchor.

### SEO

- `json_ld(extra_node)` in `head_common()` emits one schema.org `@graph`
  (WebSite + Person) on every page. It must stay valid JSON. Edit the
  constants, not the template. With `extra_node == NULL` the output is
  byte-for-byte unchanged.
- Article pages pass `article_json_ld(entry)` as a third node: a
  `BlogPosting` with `@id` / `url` / `mainEntityOfPage`
  (`<site_url>article/<slug>.html`), `headline`, `description`,
  `datePublished`, `inLanguage`, `image` (only with a cover), `author` /
  `publisher` (`#person`), `isPartOf` (`#website`) and `workTranslation`
  (translations' `@id`s, only when there are any). It's built only from the
  `articles[]` entry and the `site_*` constants.
- Values from data go through `json_append_string()` / `json_member()`, never
  raw: it escapes `"`, `\`, control characters, and `<` (as `\u003c`, so a
  `</script>` can't end the block). UTF-8 passes through as-is (valid JSON).
- JSON-LD and hreflang URLs are absolute (`site_url`) in both build modes.
  Prod adds `share_meta()` (canonical, Open Graph, Twitter card; see "Build
  modes" in section 4). Dev doesn't, which keeps dev output unchanged.

## 6. Output contract

```
out/index.html
out/articles.html
out/404.html
out/article/<slug>.html   (one per articles[] entry, translations included)
out/assets/...            (copy of assets/)
out/nginx.conf            (prod builds only; a dev build deletes it)
```

## 7. Verification checklist (run before saying "done")

Run steps 1–5 for **both** build modes. Build into `out/`, copy it aside
(`cp -a out /tmp/out-dev`, `/tmp/out-prod`), and finish with a dev build so
`out/` is left in dev mode.

1. Build: `./build.sh ./deps/htmc/htm.c main.c` and
   `./build.sh ./deps/htmc/htm.c main.c --prod` → no warnings, all pages
   written (`out/nginx.conf` only in prod).
2. gcc: `gcc -Wall -Wextra ./deps/htmc/htm.c main.c -o /tmp/g.o` → no warnings.
3. ASan: see section 2, in both modes → exit 0, no reports.
4. Output: if the change shouldn't affect markup, `diff -r` the old and new
   `out/` and confirm only the intended pages changed.
   - Dev output must not depend on mode plumbing: `diff -r` a dev build
     against one from before a mode-only change → identical, and no
     `out/nginx.conf`.
   - Prod: no relative URL in canonical / `og:` / hreflang / JSON-LD:
     `grep -rhoE '(href|content)="[^"]*"' out --include='*.html' | grep -E 'canonical|og:'`
     and the JSON-LD `"@id"` / `"url"` values all start with
     `https://notfound404.dev/`. The only relative `src`/`href` left in prod
     are the template literals in `js/skills.js` (see section 4):
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
5. Browser (serve the dev `out/` with `python3 -m http.server`; serve prod
   from the site root with nginx, or a stand-in that follows the config's
   routing: `try_files $uri $uri.html`, `${uri}index.html` for trailing
   slashes, `404.html` with status 404 at any depth, `/nginx.conf` → 404.
   Drive with Playwright and Chromium; see the note at the end):
   - no `pageerror` or console errors on any page (Chrome logs the 404
     page's own status when it is served for a missing URL; that one is
     expected);
   - in prod, also `/`, `/articles`, `/article/<slug>` and a deep missing URL
     (`/a/b/c`): the 404 page there loads its fonts and assets;
   - interactive pieces work: theme toggle (cross-fades over 400 ms; instant
     with reduced motion) and saved preference, mobile menu,
     nav hide/show on scroll, progress bar, skills tooltip, category filter,
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
6. Visual check: take screenshots in light and dark themes at both viewports
   and confirm the intended design:
   - light-mode code blocks are readable;
   - the 404 page is styled;
   - the nav hides while scrolled down;
   - text follows the text-size rule in section 5, and the skill tooltip and
     presence panel are wide enough to fit it;
   - the Experience section sits above "Knowledge Center";
   - prod: the home page has no gameproject2 section ("Knowledge Center" is
     the last one);
   - dev: the "gameproject2" section below "Knowledge Center": lorem ipsum text
     and a 16:9 slider of videos and images (a looping video first) with round previous/next arrows over its edges
     and a rotating yellow border (static under `prefers-reduced-motion`,
     where slides also switch without the fade);
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
7. Report results truthfully. If a check was skipped or failed, say so and
   include the output.

## 8. Safeguards

- Don't modify `deps/htmc/` or `deps/js/*` (vendored upstream files). Replace a vendored file only when upgrading it, and say so.
- Don't delete or overwrite anything outside `out/` without asking.
  `out/` is generated output and can be rebuilt at any time.
- Don't change the text, links or code blocks unless the user asks for a
  content change.
- Ask before changing the site's look or behaviour beyond the request.
  Mention visual side effects in the report.
- Keep this file up to date when a rule, path, command or component changes.

---

**Note on Playwright:** the Chromium browsers are cached in
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
