// Fills in each job's duration after its dates, counted like LinkedIn (start
// and end month included): "1 yr 1 mo". A current job is counted up to this
// month and re-checked every minute, so it rolls over while the page is open.
const plural = (n, unit) => `${n} ${unit}${n === 1 ? '' : 's'}`;

function formatMonths(months) {
  const years = Math.floor(months / 12);
  const rest = months % 12;
  const parts = [];
  if (years > 0) parts.push(plural(years, 'yr'));
  if (rest > 0) parts.push(plural(rest, 'mo'));
  return parts.join(' ');
}

const jobs = [...document.querySelectorAll('[data-job-start]')];

function update() {
  const now = new Date();
  jobs.forEach(el => {
    const [startYear, startMonth] = el.dataset.jobStart.split('-').map(Number);
    const [endYear, endMonth] = el.dataset.jobEnd
      ? el.dataset.jobEnd.split('-').map(Number)
      : [now.getFullYear(), now.getMonth() + 1];
    const months = (endYear - startYear) * 12 + (endMonth - startMonth) + 1;
    const text = months > 0 ? formatMonths(months) : '';
    if (el.textContent !== text) el.textContent = text;
  });
}

update();
if (jobs.some(el => !el.dataset.jobEnd)) setInterval(update, 60 * 1000);
