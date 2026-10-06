const THEME_KEY = 'dark-theme';

function initTheme() {
  const saved = localStorage.getItem(THEME_KEY);
  const isDark = saved === 'true';
  document.documentElement.classList.toggle('dark', isDark);
  return isDark;
}

function toggleTheme() {
  const isDark = document.documentElement.classList.toggle('dark');
  localStorage.setItem(THEME_KEY, String(isDark));
}

// Wire up all theme buttons on the page
document.addEventListener('DOMContentLoaded', () => {
  initTheme();
  document.querySelectorAll('.theme-btn').forEach(btn => {
    btn.addEventListener('click', toggleTheme);
  });
});
