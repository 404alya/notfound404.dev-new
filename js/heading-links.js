// Back/forward between heading links on one page (an article, the home page).
// Pages scroll inside [data-scroll-root] (the window if there is none), and
// browsers only restore the window's scroll on history traversal, so the
// position is saved and restored here.
const root = document.querySelector('[data-scroll-root]') || document.scrollingElement;

// Before following a link that may stay on this page (a heading, the nav's
// "Who am I?"), remember where this entry was. Capture phase, so this runs
// before the page transition's handler, which may move to the anchor itself.
document.addEventListener('click', e => {
  if (!e.target.closest('a[href*="#"]')) return;
  history.replaceState({ ...history.state, scrollRootTop: root.scrollTop }, '');
}, true);

addEventListener('popstate', e => {
  const saved = e.state && e.state.scrollRootTop;
  const target = location.hash && document.getElementById(decodeURIComponent(location.hash.slice(1)));
  if (typeof saved === 'number') root.scrollTop = saved;
  else if (target) target.scrollIntoView(); // honours the heading's scroll-mt
  else root.scrollTop = 0;
});
