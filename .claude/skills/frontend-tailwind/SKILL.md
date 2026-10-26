---
name: frontend-tailwind
description: Front-end rules for this site's markup, Tailwind v4 classes, CSS and component JavaScript. Use when adding or changing classes, text sizes, colours, dark mode, animations or keyframes, videos and images, js/*.js scripts, or any visible component - the page-transition cover and loading/flashing, theme switch, logo draw-on animation, navbar and mobile menu, skills tooltip and touch drawer, linkable headings and anchors on the home and article pages, the home hero WebGL shader, or the gameproject2 section and slider.
---

# Front-end rules

The hard rules that apply here (no CDN, self-contained assets, no layout shift
after the cover lifts, ask before visual changes) are in `CLAUDE.md`.

## Tailwind (v4.3.3 browser build, not v3)

- The config is `css/tailwind-config.css`, inlined as a
  `<style type="text/tailwindcss">` block (`tailwind_config()`):
  `@custom-variant dark`, `@theme` breakpoints `xxs`/`xs`, fonts
  `sans`/`pixelify`, `animate-fade` and `animate-logo-draw` (see
  "Logo" below). `tailwind.config = {...}`
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

## Scripts

- **Scripts never leak globals.** Every inlined script is wrapped in an IIFE.
  After changing JS, `window` must not gain new names.

## Theme switch

- `toggleTheme()` in `js/theme.js` flips `<html class="dark">`
  inside `document.startViewTransition()`, so the whole page cross-fades
  between the themes (400 ms, set on `::view-transition-group(root)` in
  `css/tailwind-config.css`; the old/new snapshots inherit it). Browsers
  without view transitions and reduced-motion users switch at once. Don't add
  per-element colour transitions for the theme: they'd fight the cross-fade
  and slow every hover.

## Loading, transitions and flashing

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

## Logo

- `logo_svg(svg_class, label)` emits the site logo as inline SVG (the paths
  of `logo_animation/logo-clean.svg`), used by the navbar (`w-14`) and the
  404 page (`w-48 md:w-64`, above the text). Both draw it on like animation
  01 "drawOn" of `logo_animation/`: every stroke has `pathLength="1"`, the
  group carries a `1px 2px` dash pattern (inherited by the strokes) and
  `animate-logo-draw` (`@theme`, 1.8 s) moves the offset from 1.01 to 0.
  Both are `motion-safe:` and the animation only applies under
  `[#page-transition.lifted~*_&]`, so it starts when the cover lifts.
  `js/page-transition.js` adds `lifted` with `done` and never removes it:
  keyed on `done`, following a link mid-draw dropped the animation and
  snapped the logo to finished while the page faded out. Reduced
  motion shows the finished logo. The box comes from the width class and the
  viewBox, so nothing shifts while it draws.
- `assets/images/svg/logo.svg` is no longer referenced by any page.

## Navbar

- The logo links to `home_url(base)`: `<base>index.html` in dev, `/` in prod.
- `nav_links(base, link_class)` emits "Articles" (`<base>articles.html`) and
  "Who am I?" (`<home_url>#who-am-i`, the id from
  `home_heading_id(HOME_ABOUT)`, so it follows the heading if the text
  changes). They're used in both the desktop bar and the mobile menu. Both
  links are `font-pixelify` with `text-base md:text-lg`; the logo isn't.
  The nav box stays 56 px tall, 8–64 px from the top. Pixelify only widens
  the links group, which stays centred.
- `js/nav.js` closes the mobile menu when one of its links is clicked: an
  in-page link like "Who am I?" on the home page doesn't unload the page.

## Skill details: tooltip and drawer

- Devices that can hover get the cursor-following `#skill-tooltip`. Touch
  screens (`(hover: none)`: phones, tablets) get `skill_drawer()` instead: a
  bottom drawer in the tooltip's style (`bg-[#000000b9] backdrop-blur-lg`)
  over a dimmed backdrop. Both show `skillDetailsHtml()` from `js/skills.js`.
- The drawer and backdrop start `hidden` (so they can't animate while
  Tailwind first styles the page) and `js/skills.js` moves them to `<body>`:
  the skills section is inside a `z-10` stacking context, under the navbar.
- Swipe down closes the drawer (`initDrawerDrag()`, touch events): it follows
  the finger (inline `translate`, runtime value) and closes past a quarter of
  its height or on a flick (over 20 px, faster than 0.5 px/ms over the last
  ~100 ms); otherwise it springs back. From the content it drags only while
  that is scrolled to the top, so the details still scroll.
- Skill icons carry `active:scale-90` as a press effect. iOS Safari applies
  `:active` only under a touch listener, hence the empty passive `touchstart`
  on `#skills-grid`.

## Linkable headings (article pages and the home page)

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
  the id by the article rules (`heading_slug()`, repeats `-2`, `-3`, …; see
  the `articles-and-translations` skill),
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

## Hero shader

The home hero's pink glow is `hero_shader()` + `js/hero-shader.js`; what it
does is in the file table of the `htmc-c-rules` skill, and how it should look
is in step 6 of the `verify` skill. To test it on the real GPU, see the
Playwright notes there.

## gameproject2 section

- The gameproject2 section is dev only for now: `page_home()` skips it in prod
  (see the `build-modes-seo-nginx` skill). Remove that condition to publish it.
- The section's heading row is a flex row of `home_heading(HOME_GAMEPROJECT2)`
  and a yellow "In development" pill (`gameproject2_badge_class`) pushed to
  the row's right end with `ml-auto`: `bg-yellow-400` like the slider's
  border in the dark theme, `bg-yellow-300` (a shade lighter) in the light
  one, `text-sm md:text-base`. The pill sits beside the h2, not inside it, so
  the id and link are unchanged; `gap-x-12` keeps the hover `#` clear of it
  on narrow rows.
- Slides of the home page's gameproject2 slider go in `gameproject2_slides[]`
  (`kind` `SLIDE_IMAGE` or `SLIDE_VIDEO`, `path` under `assets/`, `alt`: the
  img alt or the video's aria-label), in display order; the first is shown
  before JS runs. Each slide fills the 16:9 box with `object-cover`, so use
  16:9 media. Images are formatted with `format_string()` (void-tag bug,
  see the `htmc-c-rules` skill). Slides switch at once, with no fade: hidden ones are
  `invisible` (out of the accessibility tree); a `<noscript>` rule hides them, and the arrows carry
  `hidden` until the script removes it. With one slide there are no arrows,
  no status and no script.
- The slider's script and assets are listed in the file table of the
  `htmc-c-rules` skill.
