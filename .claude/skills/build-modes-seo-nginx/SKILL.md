---
name: build-modes-seo-nginx
description: Dev vs prod build differences and SEO output for this site generator. Use when a change touches the --dev/--prod mode plumbing (production, parse_args, page_base, home_url), relative vs root-absolute URLs, canonical/Open Graph/Twitter tags (share_meta, escape_attr), JSON-LD (json_ld, article_json_ld, json_append_string), hreflang alternate links, or the nginx config template (config/nginx.conf, write_nginx_conf, out/nginx.conf). Also when deciding whether something should appear only in dev or only in prod.
---

# Build modes, SEO and the nginx template

The build commands and how the mode is chosen are in `CLAUDE.md`.

## Build modes

| | dev | prod |
| --- | --- | --- |
| internal links and assets | relative (`page_base()`: `""`, `"../"`) | root-absolute (`/assets/…`, `/article/…`) |
| links to the home page (`home_url()`: navbar logo, "Who am I?") | `<base>index.html` | `/` (and `/#who-am-i`), never `/index.html` |
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
  `og:type/site_name/title/description/url/image` + `twitter:card`
  (always `summary_large_image`). Text is
  escaped with `escape_attr()`. `og:description` is left out when the
  description is empty. `image` is the article cover, or `NULL` for
  `share_image` (`assets/images/og/home.png`), which also gets
  `og:image:width`/`height` (1200×630; covers have no size tags). `404.html` gets none: it stands in for any
  missing URL. Its `meta`/`link` tags are formatted with `format_string()`
  because of the htmc void-tag bug (see the `htmc-c-rules` skill).

## nginx template

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
- The live server doesn't use `out/nginx.conf` directly: its config is
  hand-adapted from it (see the local `deploy` skill).

## SEO

- Titles: `notfound404.dev | Full-Stack Developer` on the home page
  (`home_title_suffix`),
  `Articles | notfound404.dev`, `<article title> | notfound404.dev`; never
  starting with "404" (only `404.html` is "404 NOT FOUND"). `og:title` is the
  plain title (`home_title` on the home page).
- `<meta name="description">` (`description_meta()`, formatted directly
  because of the void-tag bug) on the home page (`site_description`), the
  articles list (`articles_description`) and every article (its `summary`),
  in **both** modes; `og:description` uses the same text in prod.
- Crawler files, written in both modes (they hold only absolute URLs):
  `write_sitemap()` (home, articles list with the newest edit as `lastmod`,
  every article with `article_modified()`; translated articles carry
  `xhtml:link` alternates for each language and `x-default`),
  `write_robots_txt()` (`Allow: /` plus the sitemap; Cloudflare's managed
  robots.txt adds its content signals above it) and `write_llms_txt()`.
  `config/nginx.conf` serves `xml` as `application/xml`.
- hreflang: `alternate_links()` adds `x-default` (the translation group's
  fallback, see `article_group()`) on articles with translations.

- `json_ld(extra_node)` in `head_common()` emits one schema.org `@graph`
  (WebSite + Person, with `image` from `author_image` and `knowsAbout` from
  `author_knows_about[]`) on every page. It must stay valid JSON. Edit the
  constants, not the template. With `extra_node == NULL` the output is
  byte-for-byte unchanged.
- Article pages pass `article_json_ld(entry)` as a third node: a
  `BlogPosting` with `@id` / `url` / `mainEntityOfPage`
  (`<site_url>article/<slug>.html`), `headline`, `description`,
  `datePublished`, `dateModified` (`article_modified()`: `updated`, else
  `published`), `inLanguage`, `image` (`article_image()`: the cover, else
  `share_image`), `description` (the `summary`), `author` /
  `publisher` (`#person`), `isPartOf` (`#website`) and `workTranslation`
  (translations' `@id`s, only when there are any). It's built only from the
  `articles[]` entry and the `site_*` constants.
- Values from data go through `json_append_string()` / `json_member()`, never
  raw: it escapes `"`, `\`, control characters, and `<` (as `\u003c`, so a
  `</script>` can't end the block). UTF-8 passes through as-is (valid JSON).
- JSON-LD and hreflang URLs are absolute (`site_url`) in both build modes.
  Prod adds `share_meta()` (canonical, Open Graph, Twitter card; see "Build
  modes" above). Dev doesn't, which keeps dev output unchanged.
