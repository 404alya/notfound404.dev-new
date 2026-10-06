const LANYARD_USER_ID = '713803539117244496';
const LANYARD_URL = `https://api.lanyard.rest/v1/users/${LANYARD_USER_ID}`;
const PRESENCE_KEY = 'presence';

const STATUS_DOT_COLORS = {
  online: '#00c950',
  idle: '#f0b100',
  dnd: '#fb2c36db',
  offline: '#6a7282de',
};

let prevActivitiesHtml;

function renderPresenceContent(data) {
  const platform = [
    data.active_on_discord_web && 'Web',
    data.active_on_discord_desktop && 'Desktop',
    data.active_on_discord_mobile && 'Mobile',
  ].filter(Boolean).join(', ');

  const dotColor = STATUS_DOT_COLORS[data.discord_status] || STATUS_DOT_COLORS.offline;

  const activitiesHtml = (data.activities || []).map(act => {
    if (!act || act.type === 4) return '';
    const isMusic = act.name === 'YouTube Music' || act.name === 'Spotify';
    const hasLargeImg = isMusic && act.assets?.large_image;
    const imgSrc = hasLargeImg
      ? 'https://' + act.assets.large_image.substring(act.assets.large_image.indexOf('https/') + 6)
      : null;
    const hasSmIcon = !!act.assets?.small_image;
    const smSrc = hasSmIcon
      ? 'https://' + act.assets.small_image.substring(act.assets.small_image.indexOf('https/') + 6)
      : null;
    const ytLink = act.name === 'YouTube Music' && act.assets?.large_image && act.details
      ? `https://music.youtube.com/search?q=${encodeURIComponent((act.details.trim() || '').replace(/\s+/g, '_'))}_${encodeURIComponent((act.state?.trim() || '').replace(/\s+/g, '-'))}`
      : null;

    return `
      <div class="relative flex grow flex-col rounded py-1 pl-1">
        ${ytLink ? `<a href="${ytLink}" target="_blank" class="absolute left-0 top-0 z-10 flex cursor-pointer h-full w-full items-center justify-center rounded-lg bg-[#0000006c] text-base text-white opacity-0 duration-500 hover:opacity-100">Open in YouTube Music</a>` : ''}
        <div class="flex flex-row gap-2 space-y-0.5 text-sm">
          ${imgSrc ? `<img src="${imgSrc}" class="flex size-[4rem] rounded-md object-cover object-center shrink-0" alt="">` : ''}
          <div class="flex flex-col ${!imgSrc ? 'w-[12.5rem] md:w-[16.5rem]' : 'w-[9.5rem] md:w-[13.5rem]'}">
            <div class="font-medium text-sm text-gray-200">${act.details ? act.details.trim().replace(act.state || '', '') : ''}</div>
            <div class="break-all pr-3 text-gray-400">${act.state || ''}</div>
            <div class="flex flex-row items-center gap-1 pr-3 pt-[0.2rem]">
              ${smSrc ? `<img src="${smSrc}" class="flex h-[0.8rem] w-[0.8rem] rounded-md object-cover" alt="">` : ''}
              <div class="overflow-hidden text-sm text-ellipsis whitespace-nowrap break-all text-gray-500">
                ${act.assets?.small_text || ''} ${act.name}
              </div>
            </div>
          </div>
        </div>
      </div>`;
  }).join('');


  presenceDot.style.backgroundColor = dotColor
  presencePlatform.textContent = `${data.discord_status}${platform ? ' (' + platform + ')' : ''}`
  if (prevActivitiesHtml != activitiesHtml) activities.innerHTML = activitiesHtml;

  prevActivitiesHtml = activitiesHtml
}


let presenceInterval = null;

async function fetchPresence() {
  try {
    const res = await fetch(LANYARD_URL);
    const json = await res.json();
    if (json?.data) renderPresenceContent(json.data);
  } catch (e) {
    console.error('Presence fetch failed:', e);
  }
}


function closePanel() {
  localStorage.setItem(PRESENCE_KEY, '0');
  clearInterval(presenceInterval)
  presenceContent.style.display = 'none'
  openPresenceBtn.style.removeProperty('display')
}

function openPanel() {
  localStorage.setItem(PRESENCE_KEY, '1');
  const firstFetch = startPresence();
  openPresenceBtn.style.display = 'none'
  presenceContent.style.removeProperty('display')
  return firstFetch;
}

function setupOpenButton() {
  openPresenceBtn.addEventListener('click', openPanel);
}

function setupCloseButton() {
  closePresenceBtn.addEventListener('click', closePanel)
}

setupCloseButton()
setupOpenButton()

function startPresence() {
  if (!presenceInterval) presenceInterval = setInterval(fetchPresence, 2000);
  return fetchPresence();
}

document.addEventListener('DOMContentLoaded', () => {
  if (window.matchMedia('(pointer: coarse)').matches) return;

  if (localStorage.getItem(PRESENCE_KEY) === '0') closePanel()
  // Keep the loading screen up until the first status is in.
  else document.dispatchEvent(new CustomEvent('page:wait', { detail: openPanel() }));
});
