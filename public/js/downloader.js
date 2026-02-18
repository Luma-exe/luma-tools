// ═══════════════════════════════════════════════════════════════════════════
// PLATFORM DETECTION
// ═══════════════════════════════════════════════════════════════════════════

const PLATFORM_PATTERNS = [
    { pattern: /youtube\.com|youtu\.be/i,   id: 'youtube',    name: 'YouTube',     icon: 'fab fa-youtube',      color: '#FF0000' },
    { pattern: /tiktok\.com/i,              id: 'tiktok',     name: 'TikTok',      icon: 'fab fa-tiktok',       color: '#00F2EA' },
    { pattern: /instagram\.com/i,           id: 'instagram',  name: 'Instagram',   icon: 'fab fa-instagram',    color: '#E1306C' },
    { pattern: /spotify\.com/i,             id: 'spotify',    name: 'Spotify',     icon: 'fab fa-spotify',      color: '#1DB954' },
    { pattern: /soundcloud\.com/i,          id: 'soundcloud', name: 'SoundCloud',  icon: 'fab fa-soundcloud',   color: '#FF5500' },
    { pattern: /twitter\.com|x\.com/i,      id: 'twitter',    name: 'X / Twitter', icon: 'fab fa-x-twitter',    color: '#1DA1F2' },
    { pattern: /facebook\.com|fb\.watch/i,  id: 'facebook',   name: 'Facebook',    icon: 'fab fa-facebook',     color: '#1877F2' },
    { pattern: /twitch\.tv/i,               id: 'twitch',     name: 'Twitch',      icon: 'fab fa-twitch',       color: '#9146FF' },
    { pattern: /vimeo\.com/i,               id: 'vimeo',      name: 'Vimeo',       icon: 'fab fa-vimeo-v',      color: '#1AB7EA' },
    { pattern: /reddit\.com/i,              id: 'reddit',     name: 'Reddit',      icon: 'fab fa-reddit-alien', color: '#FF4500' },
    { pattern: /dailymotion\.com/i,         id: 'dailymotion',name: 'Dailymotion', icon: 'fas fa-play-circle',  color: '#0066DC' },
    { pattern: /pinterest\.com/i,           id: 'pinterest',  name: 'Pinterest',   icon: 'fab fa-pinterest',    color: '#E60023' },
];

function detectPlatform(url) {
    for (const p of PLATFORM_PATTERNS) { if (p.pattern.test(url)) return p; }
    return null;
}

// ═══════════════════════════════════════════════════════════════════════════
// DOWNLOADER — URL input, analyze, download, single & playlist
// ═══════════════════════════════════════════════════════════════════════════

let detectTimeout;

function initDownloader() {
    const urlInput = $('urlInput');
    if (!urlInput) return;

    urlInput.addEventListener('input', (e) => {
        clearTimeout(detectTimeout);
        detectTimeout = setTimeout(() => {
            const url = e.target.value.trim();
            state.url = url;
            const platform = detectPlatform(url);
            const badge = $('platformBadge');
            const wrapper = $('inputWrapper');
            if (platform && url.length > 10) {
                state.platform = platform;
                badge.innerHTML = `<i class="${platform.icon}"></i>`;
                badge.classList.add('detected');
                badge.style.background = platform.color;
                wrapper.classList.add('detected');
                document.documentElement.style.setProperty('--platform-color', platform.color);
            } else {
                state.platform = null;
                badge.innerHTML = '<i class="fas fa-link"></i>';
                badge.classList.remove('detected');
                badge.style.background = '';
                wrapper.classList.remove('detected');
            }
        }, 200);
    });

    urlInput.addEventListener('keydown', (e) => { if (e.key === 'Enter') analyzeURL(); });
    urlInput.addEventListener('paste', () => {
        setTimeout(() => {
            urlInput.dispatchEvent(new Event('input'));
            setTimeout(() => analyzeURL(), 400);
        }, 50);
    });
}

function showDlSection(section) {
    const panels = ['loadingSection', 'errorSection', 'mediaSection', 'progressSection',
                    'completeSection', 'playlistSection', 'batchProgressSection', 'batchCompleteSection'];
    panels.forEach(id => {
        const el = document.getElementById(id);
        if (el) el.classList.add('hidden');
    });
    if (section) {
        const map = {
            loading: 'loadingSection', error: 'errorSection', media: 'mediaSection',
            progress: 'progressSection', complete: 'completeSection',
            playlist: 'playlistSection', batchProgress: 'batchProgressSection',
            batchComplete: 'batchCompleteSection',
        };
        const el = document.getElementById(map[section]);
        if (el) el.classList.remove('hidden');
    }
}

async function apiCall(endpoint, data) {
    const res = await fetch(endpoint, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(data) });
    const json = await res.json();
    if (!res.ok) throw new Error(json.error || 'Request failed');
    return json;
}

async function analyzeURL() {
    const urlInput = $('urlInput');
    const url = urlInput.value.trim();
    if (!url) { showToast('Please enter a URL', 'error'); return; }
    if (!url.match(/^https?:\/\/.+/i)) { urlInput.value = 'https://' + url; state.url = urlInput.value; } else { state.url = url; }

    showDlSection('loading');
    $('analyzeBtn').disabled = true;

    try {
        const data = await apiCall('/api/analyze', { url: state.url });
        state.mediaInfo = data;
        if (data.platform) {
            state.platform = data.platform;
            document.documentElement.style.setProperty('--platform-color', data.platform.color);
        }
        if (data.type === 'playlist') { renderPlaylist(data); showDlSection('playlist'); resolveUnknownTitles(); }
        else { renderMediaInfo(data); showDlSection('media'); }
    } catch (err) {
        let msg = err.message || 'Failed to analyze URL';
        msg = msg.replace(/^ERROR:\s*/, '').replace(/\[\w+\]\s*\w+:\s*/, '');
        if (msg.length > 150) msg = msg.substring(0, 150) + '...';
        $('errorText').textContent = msg;
        showDlSection('error');
    } finally { $('analyzeBtn').disabled = false; }
}

function retryAnalysis() { analyzeURL(); }

function renderMediaInfo(data) {
    const thumb = $('mediaThumbnail');
    if (data.thumbnail) {
        thumb.src = data.thumbnail;
        thumb.onerror = () => { thumb.src = 'data:image/svg+xml,' + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="320" height="180" fill="%23222"><rect width="320" height="180"/><text x="160" y="95" text-anchor="middle" fill="%23666" font-size="14">No Thumbnail</text></svg>'); };
    }
    if (data.duration) {
        const m = Math.floor(data.duration / 60), s = Math.floor(data.duration % 60);
        $('mediaDuration').textContent = `${m}:${s.toString().padStart(2, '0')}`;
    } else { $('mediaDuration').textContent = ''; }

    $('mediaTitle').textContent = data.title || 'Unknown Title';
    $('mediaUploader').querySelector('span').textContent = data.uploader || 'Unknown';

    if (data.platform) {
        $('platformOverlay').innerHTML = `<i class="${data.platform.icon}"></i>`;
        $('platformOverlay').style.background = data.platform.color;
    }

    const isAudioOnly = data.platform && !data.platform.supports_video;
    const mp4Tab = $('formatTabs').querySelector('[data-format="mp4"]');
    if (isAudioOnly) { mp4Tab.classList.add('disabled'); selectFormat('mp3'); } else { mp4Tab.classList.remove('disabled'); }
    renderQualities(data.formats || []);
}

function renderQualities(formats) {
    const grid = $('qualityGrid');
    grid.innerHTML = '';
    const bestChip = document.createElement('button');
    bestChip.className = 'quality-chip active'; bestChip.dataset.quality = 'best';
    bestChip.innerHTML = 'Best Quality'; bestChip.onclick = () => selectQuality('best');
    grid.appendChild(bestChip);
    for (const q of formats.filter(f => f.height > 0)) {
        const chip = document.createElement('button');
        chip.className = 'quality-chip'; chip.dataset.quality = q.quality;
        const sizeLabel = q.filesize > 0 ? `<span class="label">${formatBytes(q.filesize)}</span>` : '';
        chip.innerHTML = `${q.quality}${sizeLabel}`; chip.onclick = () => selectQuality(q.quality);
        grid.appendChild(chip);
    }
    state.selectedQuality = 'best';
}

function selectFormat(format) {
    state.selectedFormat = format;
    $$('.format-tab').forEach(tab => tab.classList.toggle('active', tab.dataset.format === format));
    const qs = $('qualitySection');
    if (qs) { format === 'mp4' ? qs.classList.remove('hidden') : qs.classList.add('hidden'); }
}

function selectQuality(quality) {
    state.selectedQuality = quality;
    $$('.quality-chip').forEach(c => c.classList.toggle('active', c.dataset.quality === quality));
}

async function startDownload() {
    if (!state.url) return;
    $('downloadBtn').disabled = true;
    showDlSection('progress');
    try {
        const data = await apiCall('/api/download', { url: state.url, format: state.selectedFormat, quality: state.selectedQuality, title: state.mediaInfo?.title || '' });
        state.downloadId = data.download_id;
        pollDownloadStatus();
    } catch (err) { showToast('Download failed: ' + err.message, 'error'); showDlSection('media'); $('downloadBtn').disabled = false; }
}

function formatETA(seconds) {
    if (seconds == null || seconds < 0) return '';
    const m = Math.floor(seconds / 60), s = seconds % 60;
    return m > 0 ? `${m}m ${s}s` : `${s}s`;
}

function pollDownloadStatus() {
    if (state.pollInterval) clearInterval(state.pollInterval);
    state.pollInterval = setInterval(async () => {
        try {
            const res = await fetch(`/api/status/${state.downloadId}`);
            const data = await res.json();
            if (data.status === 'completed') {
                clearInterval(state.pollInterval); state.pollInterval = null;
                $('progressBar').style.width = '100%'; $('progressTitle').textContent = 'Complete!'; $('progressStatus').textContent = 'Preparing file...';
                setTimeout(() => { $('saveBtn').href = data.download_url; $('saveBtn').download = lumaTag(data.filename || 'download'); showDlSection('complete'); }, 600);
            } else if (data.status === 'error') {
                clearInterval(state.pollInterval); state.pollInterval = null;
                showToast('Download error: ' + (data.error || 'Unknown error'), 'error');
                showDlSection('media'); $('downloadBtn').disabled = false;
            } else {
                const pct = data.progress || 0;
                $('progressBar').style.width = Math.max(pct, 2) + '%';
                const pctEl = $('progressPct'); if (pctEl) pctEl.textContent = pct > 0 ? pct.toFixed(1) + '%' : '';
                if (data.speed) { const el = $('progressSpeed'); if (el) el.textContent = data.speed; }
                const etaEl = $('progressEta'); if (etaEl) etaEl.textContent = (data.eta != null && data.eta >= 0) ? formatETA(data.eta) : '';
                if (data.filesize) { const el = $('progressSize'); if (el) el.textContent = data.filesize; }
                if (data.status === 'processing') { $('progressStatus').textContent = 'Processing file...'; $('progressBar').style.width = '95%'; }
                else if (data.status === 'downloading') { $('progressStatus').textContent = 'Downloading...'; }
                else { $('progressStatus').textContent = 'Starting download...'; }
            }
        } catch (err) { /* keep polling */ }
    }, 800);
}

function resetDownloaderUI() {
    const urlInput = $('urlInput');
    if (urlInput) urlInput.value = '';
    state.url = ''; state.mediaInfo = null; state.downloadId = null;
    state.selectedFormat = 'mp3'; state.selectedQuality = 'best';
    state.isDownloading = false; state.playlistItems = []; state.batchResults = [];
    if (state.pollInterval) { clearInterval(state.pollInterval); state.pollInterval = null; }
    const badge = $('platformBadge');
    if (badge) { badge.innerHTML = '<i class="fas fa-link"></i>'; badge.classList.remove('detected'); badge.style.background = ''; }
    const wrapper = $('inputWrapper');
    if (wrapper) wrapper.classList.remove('detected');
    const db = $('downloadBtn'); if (db) db.disabled = false;
    const pb = $('progressBar'); if (pb) pb.style.width = '0%';
    const pi = $('playlistItems'); if (pi) pi.innerHTML = '';
    const bf = $('batchFiles'); if (bf) bf.innerHTML = '';
    $$('#tool-downloader .format-tab').forEach(tab => tab.classList.toggle('active', tab.dataset.format === 'mp3'));
    showDlSection(null);
    if (urlInput) urlInput.focus();
}

// ═══════════════════════════════════════════════════════════════════════════
// PLAYLIST
// ═══════════════════════════════════════════════════════════════════════════

function renderPlaylist(data) {
    state.playlistItems = data.items || [];
    $('playlistTitle').textContent = data.title || 'Playlist';
    $('playlistCount').textContent = `${data.item_count || data.items.length} items`;
    $('playlistUploader').textContent = data.uploader || 'Unknown';

    const isAudioOnly = data.platform && !data.platform.supports_video;
    const mp4Tab = $('playlistFormatTabs')?.querySelector('[data-format="mp4"]');
    if (mp4Tab) { isAudioOnly ? mp4Tab.classList.add('disabled') : mp4Tab.classList.remove('disabled'); }
    if (isAudioOnly) selectFormat('mp3');

    const container = $('playlistItems');
    container.innerHTML = '';
    for (const item of state.playlistItems) {
        const el = document.createElement('div');
        el.className = 'playlist-item selected'; el.dataset.index = item.index;
        const dur = item.duration > 0 ? `${Math.floor(item.duration / 60)}:${Math.floor(item.duration % 60).toString().padStart(2, '0')}` : '';
        const uploaderText = item.uploader ? item.uploader : '';
        el.innerHTML = `<div class="playlist-item-check"><i class="fas fa-check"></i></div><span class="playlist-item-index">${item.index + 1}</span><div class="playlist-item-info"><div class="playlist-item-title">${escapeHTML(item.title)}</div><div class="playlist-item-meta">${escapeHTML(uploaderText)}</div></div><span class="playlist-item-duration">${dur}</span>`;
        el.addEventListener('click', () => toggleItem(el));
        container.appendChild(el);
    }
    updateSelectedCount();
    state.resolvingTitles = false; state.resolveAborted = false;
    const resolving = $('playlistResolving'); if (resolving) resolving.classList.add('hidden');
    const skipBtn = $('resolvingSkipBtn'); if (skipBtn) skipBtn.classList.add('hidden');
}

async function resolveUnknownTitles() {
    const unknowns = state.playlistItems.filter(item => /^Track \d+$/.test(item.title));
    if (unknowns.length === 0) return;
    state.resolvingTitles = true; state.resolveAborted = false;
    const resolving = $('playlistResolving'); if (resolving) resolving.classList.remove('hidden');
    const skipBtn = $('resolvingSkipBtn'); if (skipBtn) skipBtn.classList.add('hidden');
    const resolvingText = $('resolvingText');
    if (resolvingText) resolvingText.textContent = `Loading track names (0/${unknowns.length})...`;
    const skipTimer = setTimeout(() => { if (state.resolvingTitles && !state.resolveAborted && skipBtn) skipBtn.classList.remove('hidden'); }, 3000);
    let resolved = 0;
    for (const item of unknowns) {
        if (state.resolveAborted) break;
        try {
            const resp = await fetch('/api/resolve-title', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ url: item.url }) });
            const data = await resp.json();
            if (state.resolveAborted) break;
            if (data.title && data.title.trim()) {
                item.title = data.title;
                const el = $('playlistItems')?.querySelector(`[data-index="${item.index}"]`);
                if (el) { const titleEl = el.querySelector('.playlist-item-title'); if (titleEl) { titleEl.textContent = data.title; titleEl.classList.add('just-resolved'); setTimeout(() => titleEl.classList.remove('just-resolved'), 700); } }
            }
        } catch (err) { /* skip */ }
        resolved++;
        if (!state.resolveAborted && resolvingText) resolvingText.textContent = `Loading track names (${resolved}/${unknowns.length})...`;
    }
    clearTimeout(skipTimer); state.resolvingTitles = false;
    if (resolving) resolving.classList.add('hidden');
}

function skipResolving() {
    state.resolveAborted = true; state.resolvingTitles = false;
    const resolving = $('playlistResolving'); if (resolving) resolving.classList.add('hidden');
    showToast('Skipped loading track names', 'info');
}

function toggleItem(el) { el.classList.toggle('selected'); updateSelectedCount(); }

function toggleSelectAll() {
    const items = $$('.playlist-item');
    const allSelected = [...items].every(el => el.classList.contains('selected'));
    items.forEach(el => allSelected ? el.classList.remove('selected') : el.classList.add('selected'));
    updateSelectedCount();
}

function updateSelectedCount() {
    const selected = $$('.playlist-item.selected'), total = $$('.playlist-item');
    const count = selected.length;
    const sc = $('selectedCount'); if (sc) sc.textContent = count;
    const pdb = $('playlistDownloadBtn'); if (pdb) pdb.disabled = count === 0;
    const allSelected = count === total.length && total.length > 0;
    const sai = $('selectAllIcon'); if (sai) sai.className = allSelected ? 'fas fa-times' : 'fas fa-check-double';
    const sat = $('selectAllText'); if (sat) sat.textContent = allSelected ? 'Deselect All' : 'Select All';
}

async function startPlaylistDownload() {
    const selectedEls = [...$$('.playlist-item.selected')];
    if (selectedEls.length === 0) { showToast('Select at least one item to download', 'error'); return; }
    const selectedItems = selectedEls.map(el => { const idx = parseInt(el.dataset.index); return state.playlistItems[idx]; });
    state.batchResults = [];
    showDlSection('batchProgress');
    const total = selectedItems.length;
    $('batchTotalNum').textContent = total; $('batchCurrentNum').textContent = '0';
    $('batchOverallBar').style.width = '0%'; $('batchTitle').textContent = 'Downloading playlist...';
    for (let i = 0; i < total; i++) {
        const item = selectedItems[i], num = i + 1;
        $('batchCurrentNum').textContent = num; $('batchOverallBar').style.width = ((num - 1) / total * 100) + '%';
        $('batchStatus').textContent = `Item ${num} of ${total}`; $('batchItemName').textContent = item.title || `Track ${num}`;
        $('batchItemBar').style.width = '0%'; $('batchItemPct').textContent = ''; $('batchItemSpeed').textContent = ''; $('batchItemEta').textContent = '';
        try {
            const data = await apiCall('/api/download', { url: item.url, format: state.selectedFormat, quality: 'best', title: item.title || '' });
            const result = await pollBatchItem(data.download_id);
            state.batchResults.push({ title: item.title, ...result });
        } catch (err) { state.batchResults.push({ title: item.title, status: 'error', error: err.message }); }
    }
    $('batchOverallBar').style.width = '100%';
    renderBatchComplete(); showDlSection('batchComplete');
}

function pollBatchItem(downloadId) {
    return new Promise((resolve) => {
        const interval = setInterval(async () => {
            try {
                const res = await fetch(`/api/status/${downloadId}`);
                const data = await res.json();
                if (data.status === 'completed') {
                    clearInterval(interval); $('batchItemBar').style.width = '100%'; $('batchItemPct').textContent = '100%';
                    resolve({ status: 'completed', download_url: data.download_url, filename: data.filename });
                } else if (data.status === 'error') {
                    clearInterval(interval); resolve({ status: 'error', error: data.error || 'Download failed' });
                } else {
                    const pct = data.progress || 0;
                    $('batchItemBar').style.width = Math.max(pct, 2) + '%';
                    $('batchItemPct').textContent = pct > 0 ? pct.toFixed(1) + '%' : '';
                    if (data.speed) $('batchItemSpeed').textContent = data.speed;
                    if (data.eta != null && data.eta >= 0) $('batchItemEta').textContent = formatETA(data.eta);
                }
            } catch { /* keep polling */ }
        }, 800);
    });
}

function renderBatchComplete() {
    const success = state.batchResults.filter(r => r.status === 'completed').length;
    const fail = state.batchResults.filter(r => r.status === 'error').length;
    $('batchCompleteText').textContent = fail === 0 ? `All ${success} downloads complete!` : `${success} completed, ${fail} failed`;
    const container = $('batchFiles'); container.innerHTML = '';
    for (const result of state.batchResults) {
        const row = document.createElement('div'); row.className = 'batch-file-row';
        if (result.status === 'completed') {
            row.innerHTML = `<span class="batch-file-name">${escapeHTML(result.title)}</span><a class="batch-file-save" href="${result.download_url}" download="${escapeHTML(lumaTag(result.filename || 'download'))}"><i class="fas fa-save"></i> Save</a>`;
        } else {
            row.innerHTML = `<span class="batch-file-name">${escapeHTML(result.title)}</span><span class="batch-file-error"><i class="fas fa-times-circle"></i> Failed</span>`;
        }
        container.appendChild(row);
    }
}
