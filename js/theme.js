// Theme: dark or light on <html class="dark">. A choice made with the toggle
// is saved and wins; without one the page follows the device's colour scheme
// (prefers-color-scheme), also when that changes while the page is open.
const THEME_KEY = 'dark-theme';
const systemDark = matchMedia('(prefers-color-scheme: dark)');

function savedTheme() {
  try { return localStorage.getItem(THEME_KEY); } catch { return null; }
}

function applyTheme() {
  const saved = savedTheme();
  const isDark = saved === null ? systemDark.matches : saved === 'true';
  document.documentElement.classList.toggle('dark', isDark);
}

function toggleTheme() {
  const flip = () => {
    const isDark = document.documentElement.classList.toggle('dark');
    try { localStorage.setItem(THEME_KEY, String(isDark)); } catch {}
  };
  // Cross-fade the whole page between the two themes (duration in
  // css/tailwind-config.css). Older browsers and reduced motion switch at once.
  if (!document.startViewTransition || matchMedia('(prefers-reduced-motion: reduce)').matches) return flip();
  document.startViewTransition(flip);
}

// Wire up all theme buttons on the page
document.addEventListener('DOMContentLoaded', () => {
  applyTheme();
  systemDark.addEventListener('change', () => { if (savedTheme() === null) applyTheme(); });
  document.querySelectorAll('.theme-btn').forEach(btn => {
    btn.addEventListener('click', toggleTheme);
  });
});
