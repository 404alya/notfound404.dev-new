// Full-screen cover that fades pages in and out (see page_transition() in
// main.c, which also sets the fade duration in data-fade-ms).
const cover = document.getElementById('page-transition');
const FADE_MS = Number(cover.dataset.fadeMs);

// theme.js applies the saved theme only at DOMContentLoaded, so read it
// here to paint the first frame in the right colour. Later fades follow
// the live theme, which the toggle may have changed.
try { cover.classList.toggle('dark', localStorage.getItem('dark-theme') === 'true'); } catch {}
const fadeIn = () => cover.classList.add('done');
const fadeOut = () => {
  cover.classList.toggle('dark', document.documentElement.classList.contains('dark'));
  cover.classList.remove('done');
};

const pending = [];
document.addEventListener('page:wait', e => pending.push(e.detail));

const nextFrame = () => new Promise(resolve => requestAnimationFrame(resolve));
const windowLoaded = new Promise(resolve => addEventListener('load', resolve, { once: true }));
// <html> carries Tailwind's overflow-hidden class, so this flips once its CSS is live.
const tailwindApplied = async () => {
  while (getComputedStyle(document.documentElement).overflow !== 'hidden') await nextFrame();
};

const ready = (async () => {
  await windowLoaded;
  await tailwindApplied();
  await Promise.allSettled(pending);
  void document.body.offsetHeight; // lay out once so every font in use starts loading
  await document.fonts.ready;
  // A bare duration-* animates every property, so Tailwind's first styles
  // make such elements grow from their unstyled size. Let those finish.
  await Promise.allSettled(document.getAnimations()
    .filter(a => a instanceof CSSTransition).map(a => a.finished));
})();
const timeout = new Promise(resolve => setTimeout(resolve, 5000));

// The browser scrolls to the URL's #anchor while the page is still unstyled;
// Tailwind's CSS and the fonts then move it. Scroll there again under the
// cover (scrollIntoView honours the heading's scroll-mt).
const toAnchor = () => {
  const target = location.hash && document.getElementById(decodeURIComponent(location.hash.slice(1)));
  if (target) target.scrollIntoView();
};

// Two frames: a font that has just loaded is laid out in the frame after
// fonts.ready, and rAF callbacks run before that frame's layout, so one
// frame would lift the cover in the same frame the text reflows.
Promise.race([ready, timeout]).then(toAnchor).then(nextFrame).then(nextFrame).then(fadeIn);

// "/" and "/index.html" are one page, as are "/x" and "/x.html" (nginx's try_files).
const pageKey = path => path.replace(/\.html$/, '').replace(/\/index$/, '/');

// Fade out before following a link to another page of this site.
document.addEventListener('click', e => {
  const link = e.target.closest('a[href]');
  if (!link || e.defaultPrevented || e.button !== 0) return;
  if (e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) return;
  if ((link.target && link.target !== '_self') || link.hasAttribute('download')) return;

  const url = new URL(link.href, location.href);
  if (url.protocol !== location.protocol || url.origin !== location.origin) return;
  if (url.hash && pageKey(url.pathname) === pageKey(location.pathname)) {
    // Same-page anchor: no fade. Under another name of this page ("/" for
    // "/index.html") the browser would reload, so move to the anchor here.
    if (url.pathname !== location.pathname) {
      e.preventDefault();
      location.assign(url.hash);
    }
    return;
  }

  e.preventDefault();
  fadeOut();
  setTimeout(() => { location.href = url.href; }, FADE_MS);
});

// Back/forward cache restores the page with the cover still up.
addEventListener('pageshow', e => { if (e.persisted) fadeIn(); });
