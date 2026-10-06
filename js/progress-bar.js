// Scroll progress bar: fills as the page's scroll root scrolls
// Components render before the scroll root, so wait for the full DOM.
document.addEventListener('DOMContentLoaded', () => {
  const fill = document.getElementById('progress-bar-fill');
  const container = document.querySelector('[data-scroll-root]');
  if (!fill || !container) return;

  container.addEventListener('scroll', () => {
    const total = container.scrollHeight - container.clientHeight;
    const pct = total > 0 ? (container.scrollTop / total) * 100 : 0;
    fill.style.width = Math.min(100, Math.max(0, pct)) + '%';
  }, { passive: true });
});
