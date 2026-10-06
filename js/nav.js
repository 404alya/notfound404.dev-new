// Navigation: hide on scroll down / show on scroll up, mobile menu toggle.
// The page's scrolling element is marked with data-scroll-root.
// Components render before the scroll root, so wait for the full DOM.
document.addEventListener('DOMContentLoaded', () => {
  const nav = document.getElementById('main-nav');
  const menuBtn = document.getElementById('menu-btn');
  const mobileMenu = document.getElementById('mobile-menu');
  const scrollRoot = document.querySelector('[data-scroll-root]');

  if (!nav) return;

  const THRESHOLD = 10;
  const DEBOUNCE = 50;
  let lastScrollY = 0;
  let lastScrollTime = Date.now();

  // Styling lives in the markup as data-[open]: / data-[hidden]: Tailwind variants
  function setMenuOpen(open) {
    if (mobileMenu) mobileMenu.toggleAttribute('data-open', open);
  }

  function onScroll() {
    const now = Date.now();
    const currentY = scrollRoot ? scrollRoot.scrollTop : window.scrollY;
    const diff = Math.abs(currentY - lastScrollY);

    if (diff > THRESHOLD && now - lastScrollTime > DEBOUNCE) {
      const scrollingDown = currentY > lastScrollY;
      nav.toggleAttribute('data-hidden', scrollingDown);
      if (scrollingDown) setMenuOpen(false);
      lastScrollY = currentY;
      lastScrollTime = now;
    }
  }

  (scrollRoot || window).addEventListener('scroll', onScroll, { passive: true });

  if (menuBtn && mobileMenu) {
    menuBtn.addEventListener('click', () => {
      setMenuOpen(!mobileMenu.hasAttribute('data-open'));
    });
    // A link that stays on this page (the home page's "Who am I?") doesn't
    // unload it, so close the menu on any link.
    mobileMenu.addEventListener('click', e => {
      if (e.target.closest('a')) setMenuOpen(false);
    });
  }
});
