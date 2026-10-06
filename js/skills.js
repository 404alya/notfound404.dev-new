// Skills rendering, filtering, tooltip, and shrink logic
// Depends on SKILLS from skills-data.js (inlined into the same block scope)

const SKILL_CATEGORIES = ['All', 'Language', 'Library-framework', 'Service', 'Tool', 'Database', 'API', 'Dev tool', 'Other'];

const CATEGORY_COLORS = {
  'All':               'rgba(255,255,255,0.5)',
  'Language':          '#ffb1f7',
  'Library-framework': '#fffab1',
  'Service':           '#9785ff',
  'Tool':              '#1184ff',
  'Database':          'rgb(0, 178, 131, 74%)',
  'API':               'rgba(255,190,77,0.74)',
  'Dev tool':          'rgba(255,255,255,0.5)',
  'Other':             'rgba(255,255,255,0.5)',
};

// Border color classes per type (matching original Tailwind classes)
const TYPE_BORDER = {
  'Language':          'border-[#fff67e]',
  'Service':           'border-[#8975ff]',
  'Tool':              'border-[#1184ff]',
  'Database':          'border-[rgb(0,178,131,74%)]/[0.74]',
  'API':               'border-[#ffbe4d]/[0.74]',
};

const LAST_UPDATED_TS = 1790506687827;

let currentCategory = 'All';
let isShrinked = true;
let tooltipTimeout = null;
let skillEventsAttached = false;

// ─── Icon URL helper ───────────────────────────────────────────────
function getIconUrl(icon, basePath = '') {
  if (!icon) return null;
  const ext = icon.type === 'png' ? 'png' : icon.type === 'webp' ? 'webp' : 'svg';
  return `${basePath}assets/images/${icon.type}/${icon.name}.${ext}`;
}

// ─── Opacity calculation ───────────────────────────────────────────
function getOpacity(skill) {
  // Skills not used for years are dimmed to the minimum regardless of level
  if (skill.notUsedForYears) return 0.2;
  const useReached = skill.useReachedFirst !== false;
  const factor = useReached
    ? (skill.reachedSkillLevel ?? skill.skillLevel)
    : (skill.skillLevel ?? skill.reachedSkillLevel);
  if (typeof factor !== 'number') return 1;
  return Math.max(factor / 100, 0.1);
}

// ─── Render one skill icon ─────────────────────────────────────────
function renderSkillIcon(skill, basePath, index) {
  const opacity = getOpacity(skill);
  const iconUrl = getIconUrl(skill.icon, basePath);
  const borderClass = TYPE_BORDER[skill.type] || 'border-white/30';
  const sizeClass = skill.iconSize || (iconUrl ? 'size-12' : 'size-11');

  let iconHtml;
  if (!iconUrl) {
    iconHtml = `<img src="${basePath}assets/images/svg/ban.svg" class="${sizeClass}" alt="no icon">`;
  } else if (skill.icon?.darkBg) {
    iconHtml = `<div class="flex ${sizeClass} items-center justify-center rounded-md bg-black p-1"><img src="${iconUrl}" class="h-full w-full" alt="${skill.title}"></div>`;
  } else {
    const rounded = skill.icon?.rounded ? ' rounded-md' : '';
    iconHtml = `<img loading="eager" src="${iconUrl}" class="${sizeClass}${rounded}" alt="${skill.title}">`;
  }

  const learningDot = skill.learning
    ? `<div class="absolute bottom-[-3px] right-[-3px] h-2 w-2 animate-pulse rounded-full bg-[#ff7dee] shadow-[0_0px_10px_1px_rgba(255,255,255,255.2)]"></div>` : '';
  // Sits outside the dimmed layer so it stays at full opacity; the offset adds
  // the layer's 0.15rem border to land where the other markers' -3px would.
  const notUsedDot = skill.notUsedForYears
    ? `<div class="absolute right-[calc(0.15rem-3px)] top-[calc(0.15rem-3px)] h-2 w-2 rounded-full bg-[#ff8c1a] shadow-[0_0px_20px_1px_rgba(255,255,255,255.2)]"></div>` : '';
  const relearnBar = skill.forgotButCanRelearnIn
    ? `<div class="absolute bottom-[-3px] left-[-3px] h-[0.3rem] w-4 rounded-full bg-[#33ff99] shadow-[0_0px_10px_1px_rgba(255,255,255,255.2)]"></div>` : '';

  // The outer element scales and receives events; the inner layer (border,
  // background, icon) carries the opacity so the orange dot isn't dimmed.
  return `
    <div
      class="group relative size-16 cursor-pointer rounded-lg duration-200 hover:scale-110"
      data-index="${index}"
      role="button"
      tabindex="0"
      aria-label="${skill.title}"
    >
      <div
        class="absolute inset-0 flex flex-col items-center justify-center rounded-lg border-[0.15rem] ${borderClass} bg-[#ffffffa3] p-[0.15rem] duration-200 group-hover:opacity-100! group-hover:shadow-[0_0px_30px_3px_#ffffff9b]"
        style="opacity:${opacity}"
      >
        ${iconHtml}
        ${learningDot}
        ${relearnBar}
      </div>
      ${notUsedDot}
    </div>`;
}

// ─── Render skills grid ────────────────────────────────────────────
function renderSkills(basePath = '') {
  const grid = document.getElementById('skills-grid');
  if (!grid) return;

  // Shrinking only applies to the All view, so the button is useless elsewhere
  document.getElementById('shrink-btn')?.toggleAttribute('data-hidden', currentCategory !== 'All');

  const html = SKILLS
    .map((skill, index) => ({ skill, index }))
    .filter(({ skill }) => {
      if (currentCategory !== 'All' && skill.type !== currentCategory) return false;
      if (currentCategory === 'All' && isShrinked && !skill.highlighted) return false;
      return true;
    })
    .map(({ skill, index }) => renderSkillIcon(skill, basePath, index))
    .join('');

  grid.innerHTML = html || '<div class="text-white p-4">No skills in this category.</div>';
  attachSkillEvents();
}

// ─── Tooltip ──────────────────────────────────────────────────────
// Measures the rendered tooltip so it stays on screen whatever its text size.
function placeTooltip(tooltip, mouseX, mouseY) {
  const PAD = 16;
  const vw = window.innerWidth, vh = window.innerHeight;
  const w = tooltip.offsetWidth, h = tooltip.offsetHeight;

  const left = mouseX > vw / 2
    ? Math.max(PAD, mouseX - w - PAD)
    : Math.min(mouseX + PAD, vw - w - PAD);

  const top = mouseY > vh / 2 ? mouseY - 10 - h : mouseY + 20;

  tooltip.style.left = left + 'px';
  tooltip.style.top = Math.max(PAD, Math.min(top, vh - h - PAD)) + 'px';
}

function showTooltip(skill, mouseX, mouseY) {
  clearTimeout(tooltipTimeout);
  const tooltip = document.getElementById('skill-tooltip');
  if (!tooltip) return;


  const learningBadge = skill.learning
    ? `<div class="items-center pl-2 border-l-2 border-[#ff8cf0]] text-[#ff8cf0] text-sm md:text-base">Still learning</div>` : '';
  const relearnBadge = skill.forgotButCanRelearnIn
    ? `<div class="items-center pl-2 border-l-2 border-[#33ff99] text-[#33ff99] text-sm md:text-base">Forgot it but can relearn in ${skill.forgotButCanRelearnIn}</div>` : '';

  const notUsedBadge = skill.notUsedForYears
    ? `<div class="items-center pl-2 border-l-2 border-[#ff8c1a] text-[#ff8c1a] text-sm md:text-base">I had experience with it, not working on it for years so I can make mistakes.</div>` : '';

  const skillLevelHtml = typeof skill.skillLevel === 'number' ? `
    <div class="flex flex-col gap-4">
      <div class="w-full h-[0.1rem] bg-[#ffffff45]"></div>
      <div class="flex flex-col">
        <div class="text-sm md:text-base"><span class="text-[#ffffffd0]">Current Workability level:</span> ${skill.skillLevel}/100</div>
        ${skill.reachedSkillLevel ? `<div class="text-sm md:text-base rounded-l-xs bg-gradient-to-r from-[#33ff998a] to-transparent text-center"><span class="text-[#ffffffd0] ml-[-0.5rem]">Reached workability level:</span> ${skill.reachedSkillLevel}/100</div>` : ''}
      </div>
      <div class="text-sm text-gray-400 md:text-base">
        <span class="text-white">READ ME:</span> Workability level is calculated based on my guess, experience and the amount of time I've spent using the technology.
        <span class="font-medium italic text-gray-300">Doesn't mean I know every little thing about it. Just describes my workability with it.</span>
        ${skill.reachedSkillLevel ? '<span> The reached workability level represents the max level I\'ve reached with that skill and it\'s also my personal thought.</span>' : ''}
      </div>
    </div>` : '';

  tooltip.innerHTML = `
    <div class="flex w-[15rem] md:w-[20rem] flex-col gap-3 rounded-md p-3 text-white shadow-[0_0px_10px_1px_rgba(0,0,0,0.5)] backdrop-blur-lg bg-[#000000b9]">
      ${learningBadge}
      ${notUsedBadge}
      ${relearnBadge}
      <div class="flex flex-col gap-1">
        <div class="text-base font-bold md:text-lg">${skill.title}</div>
        ${skill.description ? `<div class="text-sm md:text-base">${skill.description}</div>` : ''}
      </div>
      ${skillLevelHtml}
      <div class="flex flex-col gap-1">
        ${skill.type ? `<div class="text-sm md:text-base">${skill.type}</div>` : ''}
      </div>
    </div>`;

  tooltip.classList.remove('hidden');
  placeTooltip(tooltip, mouseX, mouseY);
}

function moveTooltip(mouseX, mouseY) {
  const tooltip = document.getElementById('skill-tooltip');
  if (!tooltip || tooltip.classList.contains('hidden')) return;
  placeTooltip(tooltip, mouseX, mouseY);
}

function hideTooltipDelayed() {
  tooltipTimeout = setTimeout(() => {
    const tooltip = document.getElementById('skill-tooltip');
    if (tooltip) tooltip.classList.add('hidden');
  }, 300);
}

// ─── Attach hover events to skill icons (delegated, attached once) ─
function attachSkillEvents() {
  const grid = document.getElementById('skills-grid');
  if (!grid || skillEventsAttached) return;
  skillEventsAttached = true;

  grid.addEventListener('mouseover', e => {
    const el = e.target.closest('[data-index]');
    if (!el || !grid.contains(el)) return;
    const skill = SKILLS[parseInt(el.dataset.index, 10)];
    if (skill) showTooltip(skill, e.clientX, e.clientY);
  });

  grid.addEventListener('mousemove', e => {
    const el = e.target.closest('[data-index]');
    if (!el || !grid.contains(el)) return;
    moveTooltip(e.clientX, e.clientY);
  });

  grid.addEventListener('mouseout', e => {
    const el = e.target.closest('[data-index]');
    if (!el || !grid.contains(el)) return;
    hideTooltipDelayed();
  });

  grid.addEventListener('click', e => {
    const el = e.target.closest('[data-index]');
    if (!el || !grid.contains(el)) return;
    const skill = SKILLS[parseInt(el.dataset.index, 10)];
    if (!skill) return;
    const tooltip = document.getElementById('skill-tooltip');
    if (tooltip?.classList.contains('hidden')) {
      showTooltip(skill, e.clientX || window.innerWidth / 2, e.clientY || window.innerHeight / 2);
    } else {
      hideTooltipDelayed();
    }
  });

  const sc = document.getElementById('scroll-container');
  if (sc) {
    sc.addEventListener('scroll', () => {
      document.getElementById('skill-tooltip')?.classList.add('hidden');
    }, { passive: true });
  }
}

// ─── Category dropdown ─────────────────────────────────────────────
function initCategoryDropdown(basePath = '') {
  const btn = document.getElementById('category-dropdown-btn');
  const menu = document.getElementById('category-dropdown-menu');
  const arrow = document.getElementById('category-arrow');
  const label = document.getElementById('category-label');
  if (!btn || !menu) return;

  function updateBtnColor() {
    btn.style.borderColor = CATEGORY_COLORS[currentCategory] || 'rgba(255,255,255,0.5)';
  }

  function renderOptions() {
    menu.innerHTML = SKILL_CATEGORIES
      .filter(c => c !== currentCategory)
      .map(c => `
        <button class="w-full cursor-pointer px-2 py-1 text-left text-base md:text-lg hover:bg-gray-100 text-[#00000091]" data-cat="${c}">
          ${c.replace('-', '/')}
        </button>`)
      .join('');
  }

  menu.addEventListener('click', e => {
    const opt = e.target.closest('button[data-cat]');
    if (!opt) return;
    currentCategory = opt.dataset.cat;
    label.textContent = currentCategory.replace('-', '/');
    updateBtnColor();
    menu.classList.add('hidden');
    arrow.removeAttribute('data-open');
    renderOptions();
    renderSkills(basePath);
  });

  btn.addEventListener('click', e => {
    e.stopPropagation();
    const isOpen = menu.classList.toggle('hidden') === false;
    arrow.toggleAttribute('data-open', isOpen);
    if (isOpen) renderOptions();
  });

  document.addEventListener('mousedown', e => {
    const wrap = document.getElementById('category-dropdown-wrap');
    if (wrap && !wrap.contains(e.target)) {
      menu.classList.add('hidden');
      arrow.removeAttribute('data-open');
    }
  });

  updateBtnColor();
}

// ─── Shrink button ─────────────────────────────────────────────────
function initShrinkButton(basePath = '') {
  const btn = document.getElementById('shrink-btn');
  const label = document.getElementById('shrink-label');
  const icon = document.getElementById('shrink-icon');
  if (!btn) return;

  btn.addEventListener('mousedown', () => {
    isShrinked = !isShrinked;
    if (label) label.textContent = isShrinked ? 'Expand' : 'Minimize';
    if (icon) {
      icon.innerHTML = isShrinked
        ? `<polyline points="15 3 21 3 21 9"/><polyline points="9 21 3 21 3 15"/><line x1="21" y1="3" x2="14" y2="10"/><line x1="3" y1="21" x2="10" y2="14"/>`
        : `<polyline points="4 14 10 14 10 20"/><polyline points="20 10 14 10 14 4"/><line x1="10" y1="14" x2="3" y2="21"/><line x1="21" y1="3" x2="14" y2="10"/>`;
    }
    renderSkills(basePath);
  });
}

// ─── Last updated timer ────────────────────────────────────────────
function formatTimeElapsed(ms) {
  const seconds = Math.floor(ms / 1000);
  const minutes = Math.floor(seconds / 60);
  const hours   = Math.floor(minutes / 60);
  const days    = Math.floor(hours / 24);
  const weeks   = Math.floor(days / 7);

  if (weeks > 0) {
    const rd = days % 7, rh = hours % 24;
    let r = `${weeks} week${weeks > 1 ? 's' : ''}`;
    if (rd > 0) r += `, ${rd} day${rd > 1 ? 's' : ''}`;
    else if (rh > 0) r += `, ${rh} hour${rh > 1 ? 's' : ''}`;
    return r;
  }
  if (days > 0) {
    const rh = hours % 24;
    let r = `${days} day${days > 1 ? 's' : ''}`;
    if (rh > 0) r += `, ${rh} hour${rh > 1 ? 's' : ''}`;
    return r;
  }
  if (hours > 0) {
    const rm = minutes % 60;
    let r = `${hours} hour${hours > 1 ? 's' : ''}`;
    if (rm > 0) r += `, ${rm} min${rm > 1 ? 's' : ''}`;
    return r;
  }
  if (minutes > 0) return `${minutes} minute${minutes > 1 ? 's' : ''}`;
  return `${seconds} second${seconds !== 1 ? 's' : ''}`;
}

function initLastUpdated() {
  const el = document.getElementById('last-updated-text');
  if (!el) return;
  function tick() { el.textContent = 'Last updated: ' + formatTimeElapsed(Date.now() - LAST_UPDATED_TS) + ' ago'; }
  tick();
  setInterval(tick, 1000);
}

// ─── Init skills section ───────────────────────────────────────────
function initSkills(basePath = '') {
  renderSkills(basePath);
  initCategoryDropdown(basePath);
  initShrinkButton(basePath);
  initLastUpdated();
}

initSkills();
