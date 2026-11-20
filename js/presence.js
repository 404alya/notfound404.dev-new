const LANYARD_USER_ID = '713803539117244496';
const LANYARD_URL = `https://api.lanyard.rest/v1/users/${LANYARD_USER_ID}`;
const LANYARD_SOCKET = 'wss://api.lanyard.rest/socket';
const PRESENCE_KEY = 'presence';

const STATUS_DOT_COLORS = {
  online: '#00c950',
  idle: '#f0b100',
  dnd: '#fb2c36db',
  offline: '#6a7282de',
};

const STATUS_LABELS = {
  online: 'Online',
  idle: 'Away',
  dnd: 'Busy',
  offline: 'Offline',
};

let prevActivitiesHtml;

// Activity image keys come in three shapes: "mp:external/.../https/<host>/<path>"
// (proxied external images), "spotify:<id>" (Spotify album art) and a bare
// numeric id (an image uploaded to the Discord application).
function assetUrl(asset, applicationId) {
  if (!asset) return null;
  if (asset.startsWith('spotify:')) return 'https://i.scdn.co/image/' + asset.slice(8);
  const at = asset.indexOf('https/');
  if (at >= 0) return 'https://' + asset.substring(at + 6);
  if (/^\d+$/.test(asset) && applicationId) return `https://cdn.discordapp.com/app-assets/${applicationId}/${asset}.png`;
  return null;
}

function escapeHtml(text) {
  return String(text ?? '').replace(/[&<>"']/g, (c) => `&#${c.charCodeAt(0)};`);
}

const ACTIVITY_VERBS = { 0: 'Playing', 1: 'Streaming', 2: 'Listening to', 3: 'Watching', 5: 'Competing in' };

// m:ss, or h:mm:ss for long tracks.
function formatClock(ms) {
  const total = Math.max(0, Math.floor(ms / 1000));
  const h = Math.floor(total / 3600), m = Math.floor(total / 60) % 60, s = total % 60;
  const ss = String(s).padStart(2, '0');
  return h ? `${h}:${String(m).padStart(2, '0')}:${ss}` : `${m}:${ss}`;
}

// "for 5m", "for 1h 12m"
function formatDuration(ms) {
  const mins = Math.max(0, Math.floor(ms / 60000));
  if (mins < 1) return 'just started';
  const h = Math.floor(mins / 60), m = mins % 60;
  return 'for ' + (h ? `${h}h ${m}m` : `${m}m`);
}

const EQ_ICON = `<span class="flex h-3 items-end gap-[2px]" aria-hidden="true">${
  [0, 0.3, 0.15].map(d => `<span class="h-full w-[3px] origin-bottom animate-eq rounded-sm bg-white motion-reduce:animate-none" style="animation-delay:-${d}s"></span>`).join('')
}</span>`;
const PAUSE_ICON = `<span class="flex h-3 gap-[3px]" aria-hidden="true"><span class="h-full w-[3px] rounded-sm bg-gray-400"></span><span class="h-full w-[3px] rounded-sm bg-gray-400"></span></span>`;

function activityHtml(act) {
  const isMusic = act.type === 2;
  const paused = isMusic && /paus/i.test(act.assets?.small_text || '');
  const start = act.timestamps?.start, end = act.timestamps?.end;
  const imgSrc = assetUrl(act.assets?.large_image, act.application_id)
    || assetUrl(act.assets?.small_image, act.application_id);
  const title = act.details ? act.details.trim().replace(act.state || '', '').trim() : '';
  // Spotify lists several artists as "A; B". PreMiD sends "Unknown Artist"
  // when it can't read one: show nothing instead.
  const rawArtist = act.name === 'Spotify' ? (act.state || '').split(';').map(a => a.trim()).join(', ') : act.state;
  const artist = /^unknown( artist)?$/i.test((rawArtist || '').trim()) ? '' : rawArtist;
  // PreMiD falls back to the service's own logo when there is no cover art.
  const isLogo = /\/PreMiD\/websites\//.test(act.assets?.large_image || '');
  const link = act.details_url && /^https:\/\//.test(act.details_url) ? act.details_url
    : act.name === 'Spotify' && act.sync_id ? `https://open.spotify.com/track/${encodeURIComponent(act.sync_id)}`
    : act.name === 'YouTube Music' && title
      ? `https://music.youtube.com/search?q=${encodeURIComponent(`${title} ${act.state || ''}`.trim())}`
      : null;
  const tag = link ? 'a' : 'div';
  const linkAttrs = link
    ? ` href="${escapeHtml(link)}" target="_blank" rel="noopener noreferrer" title="${escapeHtml(act.assets?.large_text ? `Open in ${act.name}: ${act.assets.large_text}` : `Open in ${act.name}`)}"`
    : '';

  const label = isMusic
    ? `
    <div class="mb-1 flex items-center gap-1.5 text-xs text-gray-400 md:text-sm">
      ${paused ? PAUSE_ICON : EQ_ICON}
      <span class="truncate">${paused ? 'Paused' : 'Listening'} · ${escapeHtml(act.name)}</span>
    </div>`
    : `<div class="mb-1 truncate text-xs text-gray-400 md:text-sm">${ACTIVITY_VERBS[act.type] || 'Playing'} ${escapeHtml(act.name)}</div>`;

  // Music with a known length gets a progress bar; anything else with a start
  // time shows how long it has been going. Both are filled in by tickProgress().
  const progress = isMusic && start && end && end > start
    ? `
    <div class="mt-2 flex items-center gap-2 text-xs tabular-nums text-gray-400 md:text-sm" data-start="${start}" data-end="${end}" ${paused ? 'data-paused' : ''}>
      <span data-elapsed class="min-w-[4ch] text-right"></span>
      <div class="h-1 grow overflow-hidden rounded-full bg-white/15">
        <div data-bar class="h-full w-full origin-left rounded-full ${paused ? 'bg-gray-400' : 'bg-white'}"></div>
      </div>
      <span class="min-w-[4ch]">${formatClock(end - start)}</span>
    </div>`
    : start && !isMusic
      ? `<div class="truncate text-gray-500" data-since="${start}"></div>`
      : '';

  return `
    <${tag}${linkAttrs} class="block rounded-md p-0.5 ${link ? 'duration-300 hover:bg-white/10' : ''}">
      <div class="flex items-center gap-2.5 md:gap-3">
        ${imgSrc ? `<img src="${escapeHtml(imgSrc)}" class="size-16 shrink-0 rounded-md md:size-19 ${isLogo ? 'bg-black/30 object-contain p-4 md:p-5' : 'object-cover'}" alt="">` : ''}
        <div class="flex min-w-0 flex-col text-sm leading-tight md:text-base">
          ${label}
          ${title ? `<div class="truncate text-white">${escapeHtml(title)}</div>` : ''}
          ${artist ? `<div class="truncate text-gray-400">${escapeHtml(artist)}</div>` : ''}
          ${progress && !isMusic ? progress : ''}
        </div>
      </div>
      ${progress && isMusic ? progress : ''}
    </${tag}>`;
}

// Moves the progress bars and elapsed times on between API polls. A paused
// track has no live position, so its bar stays where it was drawn.
function tickProgress() {
  const now = Date.now();
  for (const el of activities.querySelectorAll('[data-start]')) {
    if (el.hasAttribute('data-paused') && el.dataset.drawn) continue;
    const start = Number(el.dataset.start), end = Number(el.dataset.end);
    const elapsed = Math.min(Math.max(now - start, 0), end - start);
    el.querySelector('[data-elapsed]').textContent = formatClock(elapsed);
    el.querySelector('[data-bar]').style.transform = `scaleX(${elapsed / (end - start)})`;
    el.dataset.drawn = '1';
  }
  for (const el of activities.querySelectorAll('[data-since]')) {
    el.textContent = formatDuration(now - Number(el.dataset.since));
  }
}

function renderPresenceContent(data) {
  const status = STATUS_LABELS[data.discord_status] ? data.discord_status : 'offline';
  const activitiesHtml = (data.activities || [])
    .filter(act => act && act.type !== 4)
    .map(activityHtml)
    .join('');

  presenceDot.style.backgroundColor = STATUS_DOT_COLORS[status];
  presencePlatform.textContent = `${STATUS_LABELS[status]} on Discord`;
  if (prevActivitiesHtml != activitiesHtml) {
    activities.innerHTML = activitiesHtml;
    tickProgress();
  }

  prevActivitiesHtml = activitiesHtml
}


// Live updates: Lanyard's WebSocket first. If it can't connect, sends nothing
// within SOCKET_FIRST_DATA_MS, or drops later, poll the REST API instead and
// try the socket again every SOCKET_RETRY_MS; polling stops once it is back.
const POLL_MS = 2000;
const SOCKET_FIRST_DATA_MS = 3000;
const SOCKET_RETRY_MS = 30000;

let presenceActive = false;
let socket = null;
let heartbeatInterval = null;
let retryTimer = null;
let presenceInterval = null;
let firstPoll = null;
let progressInterval = null;

async function fetchPresence() {
  try {
    const res = await fetch(LANYARD_URL);
    const json = await res.json();
    if (json?.data) renderPresenceContent(json.data);
  } catch (e) {
    console.error('Presence fetch failed:', e);
  }
}

// Returns the first fetch, so callers can wait for data; calling it again
// while polling is a no-op.
function startPolling() {
  if (!presenceInterval) {
    presenceInterval = setInterval(fetchPresence, POLL_MS);
    firstPoll = fetchPresence();
  }
  return firstPoll;
}

function stopPolling() {
  clearInterval(presenceInterval);
  presenceInterval = null;
}

function scheduleSocketRetry() {
  if (retryTimer || !presenceActive) return;
  retryTimer = setTimeout(() => {
    retryTimer = null;
    if (presenceActive && !socket) connectSocket();
  }, SOCKET_RETRY_MS);
}

// Resolves true once the socket delivers a presence, false if it fails first.
function connectSocket() {
  return new Promise(resolve => {
    let ws;
    try {
      ws = new WebSocket(LANYARD_SOCKET);
    } catch {
      resolve(false);
      return;
    }
    socket = ws;
    let settled = false;
    const settle = ok => {
      if (settled) return;
      settled = true;
      clearTimeout(firstDataTimer);
      resolve(ok);
    };
    const firstDataTimer = setTimeout(() => { settle(false); ws.close(); }, SOCKET_FIRST_DATA_MS);

    ws.addEventListener('message', e => {
      if (socket !== ws) return;
      let msg;
      try { msg = JSON.parse(e.data); } catch { return; }
      if (msg.op === 1) { // Hello: start the heartbeat, then subscribe
        clearInterval(heartbeatInterval);
        heartbeatInterval = setInterval(() => {
          if (ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify({ op: 3 }));
        }, msg.d.heartbeat_interval);
        ws.send(JSON.stringify({ op: 2, d: { subscribe_to_id: LANYARD_USER_ID } }));
      } else if (msg.op === 0 && (msg.t === 'INIT_STATE' || msg.t === 'PRESENCE_UPDATE') && msg.d) {
        renderPresenceContent(msg.d);
        stopPolling();
        settle(true);
      }
    });

    // Fires after errors too, so this is the one place that falls back.
    ws.addEventListener('close', () => {
      if (socket !== ws) return;
      socket = null;
      clearInterval(heartbeatInterval);
      settle(false);
      if (presenceActive) {
        startPolling();
        scheduleSocketRetry();
      }
    });
  });
}

async function startPresence() {
  presenceActive = true;
  if (!progressInterval) progressInterval = setInterval(tickProgress, 1000);
  if (socket) return;
  if (typeof WebSocket !== 'undefined' && await connectSocket()) return;
  if (presenceActive) await startPolling();
}

function stopPresence() {
  presenceActive = false;
  stopPolling();
  clearTimeout(retryTimer);
  clearInterval(heartbeatInterval);
  clearInterval(progressInterval);
  retryTimer = progressInterval = null;
  if (socket) {
    const ws = socket;
    socket = null;
    ws.close();
  }
}


function closePanel() {
  localStorage.setItem(PRESENCE_KEY, '0');
  stopPresence()
  presenceContent.style.display = 'none'
  openPresenceBtn.style.removeProperty('display')
}

function openPanel() {
  localStorage.setItem(PRESENCE_KEY, '1');
  const firstData = startPresence();
  openPresenceBtn.style.display = 'none'
  presenceContent.style.removeProperty('display')
  return firstData;
}

openPresenceBtn.addEventListener('click', openPanel);
closePresenceBtn.addEventListener('click', closePanel);

document.addEventListener('DOMContentLoaded', () => {
  // Not shown on phones and tablets: CSS hides it below md, and on touch
  // screens it is removed so it doesn't poll Lanyard in the background.
  if (window.matchMedia('(pointer: coarse)').matches) {
    document.getElementById('presence-widget').remove();
    return;
  }

  if (localStorage.getItem(PRESENCE_KEY) === '0') closePanel()
  // Keep the loading screen up until the first status is in.
  else document.dispatchEvent(new CustomEvent('page:wait', { detail: openPanel() }));
});
