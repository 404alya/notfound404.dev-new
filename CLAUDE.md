# notfound404.dev generator

This project is a C static site generator. `main.c` builds every page of
notfound404.dev with the [htmc](deps/htmc/README.md) library and writes it to
`out/`.

This file holds what applies to every task. Topic details live in the project
skills listed at the end; load the matching one before working in its area.

## Build and run

Always run from the project root:

```sh
./build.sh ./deps/htmc/htm.c main.c          # development build (the default)
./build.sh ./deps/htmc/htm.c main.c --prod   # production build, also writes out/nginx.conf
```

- `build.sh` passes `--dev` / `--prod` to the generator (`./output.o --dev`,
  `./output.o --prod`) and every other argument to clang. Without a flag the
  build is dev. Any other argument to `output.o` stops it with a usage error.
- The mode is stored once in `production` by `parse_args()` and read wherever
  output differs. In short: dev (the reference build) uses relative paths;
  prod uses root-absolute paths, adds canonical/Open Graph tags, writes
  `out/nginx.conf` and leaves out the gameproject2 section. The full table
  and its rules are in the `build-modes-seo-nginx` skill.
- `main.c` includes only `deps/htmc/htmc.h`. `htm.c` must be passed to the
  compiler. Never `#include "htm.c"` in `main.c`: that causes "multiple
  definition" link errors.
- The build must print **zero warnings** under `-Wall -Wextra` (clang via
  `build.sh`, and also gcc).
- Every page listed under "Output contract" must be written to `out/` at its
  listed path.

### Output contract

```
out/index.html
out/articles.html
out/404.html
out/article/<slug>.html   (one per articles[] entry, translations included)
out/assets/...            (copy of assets/)
out/nginx.conf            (prod builds only; a dev build deletes it)
```

## Hard rules

- **One C file.** All generator code lives in `main.c`: no runtime server, no
  extra build tooling. Supporting inputs are read at build time.
- **System headers go before `htmc.h`** (htmc defines macros named after
  HTML tags; see the `htmc-c-rules` skill).
- **Never modify `deps/htmc/` or `deps/js/*`** (vendored upstream files).
  Replace a vendored file only when upgrading it, and say so.
- **No CDN, no external runtime dependencies** (below).
- **No layout shift after the page-transition cover lifts** (checked by the
  `verify` skill).
- Don't delete or overwrite anything outside `out/` without asking.
  `out/` is generated output and can be rebuilt at any time.
- Don't change the text, links or code blocks unless the user asks for a
  content change.
- Ask before changing the site's look or behaviour beyond the request.
  Mention visual side effects in the report.
- Keep this file and the project skills up to date when a rule, path, command
  or component changes: each rule lives in exactly one of them.

### Memory checks (required after every C change)

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

### Ownership

- Every function returning `char *` returns a heap string **owned by the caller**.
- Inside an `htmc(...)` expression, wrap such calls in `own(...)`:
  `div(own(navbar(base)))`. `own()` hands the string to the enclosing `htmc()`,
  which frees it after copying it into the parent.
- Never `own()` a string literal or a buffer you still need: it will be freed.
- **Never use an `own()`ed string in another argument of the same htmc
  expression.** Argument evaluation order is unspecified, and once a buffer has
  been consumed htmc may reuse or free it. If a value is needed twice, compute
  the derived value into a local variable *before* the `htmc(...)` call.
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

### No external runtime dependencies

- Everything the page needs is inlined or served from `out/assets/`: Tailwind,
  highlight.js and its theme, the Outfit font (self-hosted woff2) and local
  fonts.
- Don't add CDN `<script>`/`<link>` tags. Vendor the file into `deps/` or
  `assets/` and inline it or copy it instead. Check that the file doesn't
  contain `</script>` or `</style>` before inlining it.
- The only allowed runtime network calls are the GitHub repos API, the Lanyard
  presence API, and images those APIs return.

### Self-contained assets

- The build never reads from other projects. Every asset lives
  in `assets/`, and pages reference only `assets/...` paths.
- `assets/` holds only files the site uses. A new skill icon, social icon or
  article cover must be added to `assets/images/<type>/` along with its entry,
  otherwise it 404s.

## Project skills (`.claude/skills/`)

| Skill | Use it when |
| --- | --- |
| `htmc-c-rules` | writing or changing C in `main.c`: htmc pitfalls and the void-tag bug, ownership details, error handling, code structure, base paths, the file table |
| `articles-and-translations` | adding or editing articles, translations, `AI_used`, dates, heading ids (`link_headings()`), the articles list |
| `frontend-tailwind` | markup, classes, JS components, animations: Tailwind v4, text sizes, the page-transition cover, theme, logo, navbar, skills tooltip/drawer, linkable headings, hero shader, gameproject2 |
| `build-modes-seo-nginx` | anything that differs between dev and prod, `share_meta()`, JSON-LD, hreflang, `config/nginx.conf` |
| `verify` | before saying "done": the full build, ASan, output, browser and visual checklist, with helper scripts |
| `deploy` | redeploying or troubleshooting the live site (server, TLS, DNS). Local only: gitignored |
