const THEME_KEY = 'dark-theme';

function initTheme() {
  const saved = localStorage.getItem(THEME_KEY);
  const isDark = saved === 'true';
  document.documentElement.classList.toggle('dark', isDark);
  return isDark;
}

function toggleTheme() {
  const flip = () => {
    const isDark = document.documentElement.classList.toggle('dark');
    localStorage.setItem(THEME_KEY, String(isDark));
  };
  // Cross-fade the whole page between the two themes (duration in
  // css/tailwind-config.css). Older browsers and reduced motion switch at once.
  if (!document.startViewTransition || matchMedia('(prefers-reduced-motion: reduce)').matches) return flip();
  document.startViewTransition(flip);
}

// Wire up all theme buttons on the page
document.addEventListener('DOMContentLoaded', () => {
  initTheme();
  document.querySelectorAll('.theme-btn').forEach(btn => {
    btn.addEventListener('click', toggleTheme);
  });
});
