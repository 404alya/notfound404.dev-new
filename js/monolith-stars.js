// Monolith star button: fills in the repository's star count from the
// GitHub repos API. The count's box is reserved in the markup and the fetch
// is registered with the page cover, so nothing shifts when it arrives. On
// failure the button keeps its "Star" label with an empty count.
const button = document.querySelector('[data-monolith-stars]');

if (button) {
  const count = button.querySelector('[data-monolith-stars-count]');
  const format = new Intl.NumberFormat('en', { notation: 'compact', maximumFractionDigits: 1 });

  const load = fetch('https://api.github.com/repos/404alya/Monolith', {
    headers: { Accept: 'application/vnd.github+json' },
  })
    .then(response => (response.ok ? response.json() : Promise.reject(response.status)))
    .then(repo => {
      const stars = repo.stargazers_count;
      if (typeof stars !== 'number') return;
      count.textContent = format.format(stars);
      button.setAttribute('aria-label', `Star Monolith on GitHub (${stars} ${stars === 1 ? 'star' : 'stars'})`);
    })
    .catch(() => {});

  document.dispatchEvent(new CustomEvent('page:wait', { detail: load }));
}
