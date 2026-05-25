// Luma Tools — MV3 background service worker.
// Adds right-click context-menu entries on images, video, and audio that
// download the media and POST it to the corresponding /api/tools/ endpoint
// using the user's API key (stored in chrome.storage.sync).

const BASE = 'https://tools.lumaplayground.com';

const MENUS = [
  { id: 'lt-image-compress', title: 'Compress with Luma Tools', contexts: ['image'], tool: 'image-compress', accept: 'image/*' },
  { id: 'lt-image-convert',  title: 'Convert image…',           contexts: ['image'], tool: 'image-convert',  accept: 'image/*' },
  { id: 'lt-image-bg',       title: 'Remove background',        contexts: ['image'], tool: 'image-bg-remove', accept: 'image/*' },
  { id: 'lt-video-compress', title: 'Compress video',           contexts: ['video'], tool: 'video-compress', accept: 'video/*' },
  { id: 'lt-video-togif',    title: 'Video → GIF',              contexts: ['video'], tool: 'video-to-gif',   accept: 'video/*' },
  { id: 'lt-audio-convert',  title: 'Convert audio',            contexts: ['audio'], tool: 'audio-convert',  accept: 'audio/*' },
];

chrome.runtime.onInstalled.addListener(() => {
  chrome.contextMenus.removeAll(() => {
    for (const m of MENUS) {
      chrome.contextMenus.create({ id: m.id, title: m.title, contexts: m.contexts });
    }
  });
});

chrome.contextMenus.onClicked.addListener(async (info, _tab) => {
  const menu = MENUS.find(m => m.id === info.menuItemId);
  if (!menu) return;
  const srcUrl = info.srcUrl;
  if (!srcUrl) return notify('Luma Tools', 'No source URL found for that element.');
  await runTool(menu.tool, srcUrl);
});

async function getKey() {
  return new Promise(r => chrome.storage.sync.get(['apiKey'], d => r(d.apiKey || '')));
}

async function runTool(tool, srcUrl) {
  const key = await getKey();
  if (!key) {
    notify('Luma Tools — sign in needed',
      'Open the extension popup and paste your API key first. Get one at ' + BASE + '/account');
    chrome.action.openPopup?.();
    return;
  }
  notify('Luma Tools', 'Sending to ' + tool + '…');
  try {
    const fileResp = await fetch(srcUrl);
    if (!fileResp.ok) throw new Error('Could not fetch source (' + fileResp.status + ')');
    const blob = await fileResp.blob();
    const filename = filenameFromUrl(srcUrl, blob.type);
    const fd = new FormData();
    fd.append('file', blob, filename);
    const resp = await fetch(BASE + '/api/tools/' + tool, {
      method: 'POST',
      headers: { 'Authorization': 'Bearer ' + key },
      body: fd
    });
    if (!resp.ok) {
      const txt = await resp.text();
      let msg = txt;
      try { msg = JSON.parse(txt).error || txt; } catch {}
      throw new Error('HTTP ' + resp.status + ': ' + msg.slice(0, 200));
    }
    const ct = resp.headers.get('content-type') || '';
    if (ct.includes('application/json')) {
      const j = await resp.json();
      notify('Luma Tools — done', JSON.stringify(j).slice(0, 200));
      return;
    }
    // Binary result — save via downloads API.
    const ab = await resp.arrayBuffer();
    const outBlob = new Blob([ab]);
    const outUrl  = URL.createObjectURL(outBlob);
    const cd = resp.headers.get('content-disposition') || '';
    let outName = (/filename="?([^";]+)"?/.exec(cd) || [])[1] || ('luma-' + tool + '-' + Date.now());
    chrome.downloads.download({ url: outUrl, filename: outName, saveAs: true });
    notify('Luma Tools — done', 'Saved ' + outName + ' (' + (ab.byteLength / 1024).toFixed(1) + ' KB)');
  } catch (e) {
    notify('Luma Tools — error', String(e.message || e));
  }
}

function filenameFromUrl(u, mime) {
  try {
    const url = new URL(u);
    const base = url.pathname.split('/').pop() || 'media';
    if (base.includes('.')) return base;
    // Append extension from mime.
    const m = /^(image|video|audio)\/(\w+)/.exec(mime || '');
    return base + (m ? '.' + m[2] : '');
  } catch { return 'media'; }
}

function notify(title, message) {
  try {
    // iconUrl omitted — Chrome falls back to the extension's default action
    // icon, which is fine until real PNG icons are added to icons/.
    chrome.notifications.create({ type: 'basic', iconUrl: '', title, message });
  } catch {}
}
