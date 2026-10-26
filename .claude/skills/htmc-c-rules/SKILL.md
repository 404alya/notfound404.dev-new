---
name: htmc-c-rules
description: C and htmc rules for main.c in this site generator. Use whenever writing, changing or reviewing C code in main.c: adding a component or page function, calling htmc macros (htmc, attr, own, htmc_fmt, htmc_strlit, htmc_ccode, meta/link/img void tags), handling heap strings and ownership, error handling with die/die_msg, base paths (page_base, home_url), page_shell, inlining js/css/config files, or the jobs/social/site data arrays. Also explains which file in the repo does what.
---

# htmc and C rules for `main.c`

The hard rules (one C file, zero warnings, ASan after every C change, the core
ownership rules, no fixed-size buffers, system headers before `htmc.h`, never
modify `deps/`) are in `CLAUDE.md`. This skill holds the details.

## Ownership details

- Parameters typed `char *` (not `const`) are taken over the same way as
  `own()`ed strings (see `page_shell(title, base, head_extra, body)`).
  `const char *` means borrowed.
- Heap strings used outside htmc (paths, titles) are freed explicitly with
  `free()` after use (see `page_article()`).

## Error handling

- Check `malloc`/`fopen`/`fclose`/`fwrite` results. On fatal build errors,
  `die()` is acceptable (it calls `perror`); for errors in the site's own data,
  where errno means nothing, use `die_msg(fmt, ...)`. Never pass data as its
  format. Page writers return `-1` and `main()` aggregates status.

## htmc pitfalls

- System headers go before `htmc.h` because htmc defines function-like macros
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
- Article bodies are inserted verbatim (except for the heading transform):
  see the `articles-and-translations` skill.

## Code structure and style

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
  | `js/page-transition.js` | the page-transition cover's behaviour (`page_transition()`): lift after load (adds `done`, and `lifted`, which is never removed), fade-out on internal links (removes `done`), same-page anchors, bfcache; reads the fade duration from the cover's `data-fade-ms`, which C sets from `page_fade_ms` (also used by the cover's CSS transition) |
  | `articles/<slug>.html` | article bodies |
  | `assets/` | every font, image, icon and video the site uses (copied to `out/assets/`) |
  | `js/gameproject2-slider.js` | the gameproject2 slider (`gameproject2_slider()`): moves `data-active` between slides, arrows and left/right keys wrap around, pauses hidden videos and plays the shown one, updates the screen-reader status; inlined only when there are 2+ slides |
  | `assets/video/gameproject2.webm` | the gameproject2 slider's first slide (currently a placeholder test pattern; replace the file with a gameplay walk, keep the name) |
  | `assets/images/gameproject2/` | the gameproject2 slider's images: `screenshot-1.webp` … `screenshot-4.webp`, 1280×720 stills rendered by the game (canyon, hall, stairs, tower), in route order |

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
- **Global scripts** (Tailwind config and build, theme) live in
  `global_scripts()` only. Page-specific scripts belong to their component.
- Jobs for the home page's Experience section go in
  `jobs[]` (rendered by `experience_section()` / `job_card()`; a `url` makes
  the card an external link to the company; the duration is
  computed at runtime by `js/experience.js`, never hardcoded, and refreshes
  itself for a current job); social profiles
  go in `social_links[]` or `extra_profiles[]`; site identity goes in the `site_*` / `author_*` constants
  (they feed the meta description and the JSON-LD).
- Readable C: short functions, `const char *` for borrowed inputs, minimal new
  macros (only `own()` exists beyond htmc), and comments that explain *why*.

Articles, translations and the articles list: `articles-and-translations`.
Dev/prod differences, `share_meta()`, JSON-LD and nginx: `build-modes-seo-nginx`.
