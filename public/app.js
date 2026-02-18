/**
 * Luma Tools — Frontend Application
 * Handles sidebar navigation, media downloading, and file processing tools
 */

// ═══════════════════════════════════════════════════════════════════════════
// STATE & CONFIG
// ═══════════════════════════════════════════════════════════════════════════

const state = {
    currentTool: 'downloader',
    // Downloader state
    url: '', platform: null, mediaInfo: null,
    selectedFormat: 'mp3', selectedQuality: 'best',
    downloadId: null, pollInterval: null, isDownloading: false,
    playlistItems: [], batchResults: [],
    resolvingTitles: false, resolveAborted: false,
    // File tools state — per-tool file storage
    files: {},          // { 'image-compress': File, 'video-trim': File, ... }
    multiFiles: {},     // { 'pdf-merge': [File, File, ...] }
    outputFormats: {},  // { 'image-convert': 'png', ... }
    presets: {},        // { 'video-compress': 'medium', ... }
    aspectLock: true,
    // Processing state
    processing: {},     // { 'image-compress': true, ... }
    jobPolls: {},       // { 'video-compress': intervalId, ... }
};

const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => document.querySelectorAll(sel);

// ═══════════════════════════════════════════════════════════════════════════
// SIDEBAR & NAVIGATION
// ═══════════════════════════════════════════════════════════════════════════

function switchTool(toolId) {
    state.currentTool = toolId;
    // Update nav items
    $$('.nav-item').forEach(el => el.classList.toggle('active', el.dataset.tool === toolId));
    // Show/hide panels
    $$('.tool-panel').forEach(el => el.classList.toggle('active', el.id === 'tool-' + toolId));
    // Close mobile sidebar
    if (window.innerWidth <= 768) toggleSidebar(false);
}

function toggleSidebar(forceState) {
    const sidebar = $('#sidebar');
    const overlay = $('#sidebarOverlay');
    const isOpen = typeof forceState === 'boolean' ? forceState : !sidebar.classList.contains('open');
    sidebar.classList.toggle('open', isOpen);
    overlay.classList.toggle('open', isOpen);
}

// ═══════════════════════════════════════════════════════════════════════════
// PLATFORM DETECTION (Downloader)
// ═══════════════════════════════════════════════════════════════════════════

const PLATFORM_PATTERNS = [
    { pattern: /youtube\.com|youtu\.be/i,   id: 'youtube',    name: 'YouTube',     icon: 'fab fa-youtube',      color: '#FF0000' },
    { pattern: /tiktok\.com/i,              id: 'tiktok',     name: 'TikTok',      icon: 'fab fa-tiktok',       color: '#00F2EA' },
    { pattern: /instagram\.com/i,           id: 'instagram',  name: 'Instagram',   icon: 'fab fa-instagram',    color: '#E1306C' },
    { pattern: /spotify\.com/i,             id: 'spotify',    name: 'Spotify',     icon: 'fab fa-spotify',      color: '#1DB954' },
    { pattern: /soundcloud\.com/i,          id: 'soundcloud', name: 'SoundCloud',  icon: 'fab fa-soundcloud',   color: '#FF5500' },
    { pattern: /twitter\.com|x\.com/i,      id: 'twitter',    name: 'X / Twitter', icon: 'fab fa-x-twitter',    color: '#1DA1F2' },
    { pattern: /facebook\.com|fb\.watch/i,  id: 'facebook',   name: 'Facebook',    icon: 'fab fa-facebook',     color: '#1877F2' },
    { pattern: /twitch\.tv/i,              id: 'twitch',     name: 'Twitch',      icon: 'fab fa-twitch',       color: '#9146FF' },
    { pattern: /vimeo\.com/i,              id: 'vimeo',      name: 'Vimeo',       icon: 'fab fa-vimeo-v',      color: '#1AB7EA' },
    { pattern: /reddit\.com/i,             id: 'reddit',     name: 'Reddit',      icon: 'fab fa-reddit-alien', color: '#FF4500' },
    { pattern: /dailymotion\.com/i,        id: 'dailymotion',name: 'Dailymotion', icon: 'fas fa-play-circle',  color: '#0066DC' },
    { pattern: /pinterest\.com/i,          id: 'pinterest',  name: 'Pinterest',   icon: 'fab fa-pinterest',    color: '#E60023' },
];
const AUDIO_ONLY_PLATFORMS = ['spotify', 'soundcloud'];

function detectPlatform(url) {
    for (const p of PLATFORM_PATTERNS) { if (p.pattern.test(url)) return p; }
    return null;
}

// ═══════════════════════════════════════════════════════════════════════════
// FILE UPLOAD HANDLING
// ═══════════════════════════════════════════════════════════════════════════

function initUploadZones() {
    document.querySelectorAll('.upload-zone').forEach(zone => {
        const input = zone.querySelector('.upload-input');
        const toolId = zone.id.replace('uz-', '');
        const isMulti = zone.classList.contains('multi');

        // Drag events
        zone.addEventListener('dragover', e => { e.preventDefault(); zone.classList.add('dragover'); });
        zone.addEventListener('dragleave', () => zone.classList.remove('dragover'));
        zone.addEventListener('drop', e => {
            e.preventDefault(); zone.classList.remove('dragover');
            const files = [...e.dataTransfer.files];
            if (isMulti) handleMultiFiles(toolId, files);
            else if (files.length > 0) handleFileSelect(toolId, files[0]);
        });

        // Click/change
        input.addEventListener('change', e => {
            if (isMulti) handleMultiFiles(toolId, [...e.target.files]);
            else if (e.target.files.length > 0) handleFileSelect(toolId, e.target.files[0]);
            e.target.value = ''; // allow re-selecting same file
        });
    });
}

function handleFileSelect(toolId, file) {
    const maxSize = parseInt(document.getElementById('uz-' + toolId)?.dataset.max || '52428800');
    if (file.size > maxSize) {
        showToast(`File too large. Max ${formatBytes(maxSize)}`, 'error');
        return;
    }
    state.files[toolId] = file;

    // Update preview
    const preview = document.querySelector(`.file-preview[data-tool="${toolId}"]`);
    if (preview) {
        preview.classList.remove('hidden');
        preview.querySelector('.file-name').textContent = file.name;
        preview.querySelector('.file-size').textContent = formatBytes(file.size);
    }
    // Hide upload zone
    const zone = document.getElementById('uz-' + toolId);
    if (zone) zone.classList.add('hidden');

    // Hide any previous result
    hideResult(toolId);
}

function handleMultiFiles(toolId, files) {
    if (!state.multiFiles[toolId]) state.multiFiles[toolId] = [];

    for (const file of files) {
        state.multiFiles[toolId].push(file);
    }

    renderMultiFileList(toolId);
}

function renderMultiFileList(toolId) {
    const list = document.querySelector(`.multi-file-list[data-tool="${toolId}"]`);
    if (!list) return;
    list.classList.remove('hidden');
    list.innerHTML = '';

    const files = state.multiFiles[toolId] || [];
    files.forEach((file, idx) => {
        const item = document.createElement('div');
        item.className = 'multi-file-item';
        item.innerHTML = `
            <div class="file-info">
                <i class="fas fa-file-pdf"></i>
                <div><span class="file-name">${escapeHTML(file.name)}</span><span class="file-size">${formatBytes(file.size)}</span></div>
            </div>
            <button class="file-remove" onclick="removeMultiFile('${toolId}',${idx})"><i class="fas fa-times"></i></button>
        `;
        list.appendChild(item);
    });
}

function removeFile(toolId) {
    delete state.files[toolId];
    const preview = document.querySelector(`.file-preview[data-tool="${toolId}"]`);
    if (preview) preview.classList.add('hidden');
    const zone = document.getElementById('uz-' + toolId);
    if (zone) zone.classList.remove('hidden');
    hideResult(toolId);
}

function removeMultiFile(toolId, idx) {
    if (state.multiFiles[toolId]) {
        state.multiFiles[toolId].splice(idx, 1);
        if (state.multiFiles[toolId].length === 0) {
            const list = document.querySelector(`.multi-file-list[data-tool="${toolId}"]`);
            if (list) list.classList.add('hidden');
        } else {
            renderMultiFileList(toolId);
        }
    }
}

function hideResult(toolId) {
    const result = document.querySelector(`.result-section[data-tool="${toolId}"]`);
    if (result) result.classList.add('hidden');
    const proc = document.querySelector(`.processing-status[data-tool="${toolId}"]`);
    if (proc) proc.classList.add('hidden');
}

// ═══════════════════════════════════════════════════════════════════════════
// TOOL OPTIONS
// ═══════════════════════════════════════════════════════════════════════════

function selectOutputFmt(btn) {
    const grid = btn.closest('.format-select-grid');
    grid.querySelectorAll('.fmt-btn').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    const toolId = grid.dataset.tool;
    if (toolId) state.outputFormats[toolId] = btn.dataset.fmt;
}

function selectPreset(btn) {
    const grid = btn.closest('.preset-grid');
    grid.querySelectorAll('.preset-btn').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    const toolId = grid.dataset.tool;
    if (toolId) state.presets[toolId] = btn.dataset.val;
}

function toggleAspectLock() {
    state.aspectLock = !state.aspectLock;
    const lock = $('#dimLock');
    lock.classList.toggle('active', state.aspectLock);
    lock.innerHTML = state.aspectLock ? '<i class="fas fa-link"></i>' : '<i class="fas fa-unlink"></i>';
}

// ═══════════════════════════════════════════════════════════════════════════
// FILE PROCESSING (Calls backend /api/tools/*)
// ═══════════════════════════════════════════════════════════════════════════

async function processFile(toolId) {
    // Multi-file tools
    if (toolId === 'pdf-merge') {
        const files = state.multiFiles[toolId];
        if (!files || files.length < 2) { showToast('Please select at least 2 PDF files', 'error'); return; }
        return processMultiFile(toolId, files);
    }

    const file = state.files[toolId];
    if (!file) { showToast('Please select a file first', 'error'); return; }

    // Build form data
    const formData = new FormData();
    formData.append('file', file);

    // Add tool-specific options
    switch (toolId) {
        case 'image-compress':
            formData.append('quality', $('#imageCompressQuality').value);
            break;
        case 'image-resize':
            formData.append('width', $('#resizeWidth').value || '');
            formData.append('height', $('#resizeHeight').value || '');
            break;
        case 'image-convert':
            formData.append('format', getSelectedFmt('image-convert') || 'png');
            break;
        case 'video-compress':
            formData.append('preset', getSelectedPreset('video-compress') || 'medium');
            break;
        case 'video-trim':
            formData.append('start', $('#trimStart').value || '00:00:00');
            formData.append('end', $('#trimEnd').value || '');
            if (!$('#trimEnd').value) { showToast('Please enter an end time', 'error'); return; }
            break;
        case 'video-convert':
            formData.append('format', getSelectedFmt('video-convert') || 'mp4');
            break;
        case 'video-extract-audio':
            formData.append('format', getSelectedFmt('video-extract-audio') || 'mp3');
            break;
        case 'audio-convert':
            formData.append('format', getSelectedFmt('audio-convert') || 'mp3');
            break;
        case 'pdf-compress':
            formData.append('level', getSelectedPreset('pdf-compress') || 'ebook');
            break;
        case 'pdf-to-images':
            formData.append('format', getSelectedFmt('pdf-to-images') || 'png');
            formData.append('dpi', $('#pdfDpi').value || '200');
            break;
    }

    // Show processing
    showProcessing(toolId, true);

    const isAsync = toolId.startsWith('video-');

    try {
        const endpoint = '/api/tools/' + toolId;
        const res = await fetch(endpoint, { method: 'POST', body: formData });

        if (!res.ok) {
            const err = await res.json().catch(() => ({ error: 'Processing failed' }));
            throw new Error(err.error || 'Processing failed');
        }

        if (isAsync) {
            // Async processing — get job ID and poll
            const data = await res.json();
            if (data.job_id) {
                pollJobStatus(toolId, data.job_id);
            } else {
                throw new Error('No job ID returned');
            }
        } else {
            // Synchronous — check content type
            const contentType = res.headers.get('content-type') || '';
            if (contentType.includes('application/json')) {
                // JSON response (e.g. pdf-to-images multi-page)
                const data = await res.json();
                if (data.pages && data.pages.length > 0) {
                    showMultiResult(toolId, data.pages);
                } else {
                    throw new Error('No output files');
                }
            } else {
                const blob = await res.blob();
                const filename = getFilenameFromResponse(res) || file.name;
                showResult(toolId, blob, filename);
            }
            showProcessing(toolId, false);
        }
    } catch (err) {
        showProcessing(toolId, false);
        showToast(err.message, 'error');
    }
}

async function processMultiFile(toolId, files) {
    const formData = new FormData();
    files.forEach((f, i) => formData.append('file' + i, f));
    formData.append('count', files.length.toString());

    showProcessing(toolId, true);

    try {
        const res = await fetch('/api/tools/' + toolId, { method: 'POST', body: formData });
        if (!res.ok) {
            const err = await res.json().catch(() => ({ error: 'Processing failed' }));
            throw new Error(err.error || 'Processing failed');
        }
        const blob = await res.blob();
        const filename = getFilenameFromResponse(res) || 'merged.pdf';
        showResult(toolId, blob, filename);
    } catch (err) {
        showToast(err.message, 'error');
    } finally {
        showProcessing(toolId, false);
    }
}

function pollJobStatus(toolId, jobId) {
    const procEl = document.querySelector(`.processing-status[data-tool="${toolId}"]`);
    const progressBar = procEl?.querySelector('.progress-bar');
    const progressPct = procEl?.querySelector('.progress-pct');
    const procText = procEl?.querySelector('.processing-text');

    if (state.jobPolls[toolId]) clearInterval(state.jobPolls[toolId]);

    state.jobPolls[toolId] = setInterval(async () => {
        try {
            const res = await fetch(`/api/tools/status/${jobId}`);
            const data = await res.json();

            if (data.status === 'completed') {
                clearInterval(state.jobPolls[toolId]);
                delete state.jobPolls[toolId];
                if (progressBar) progressBar.style.width = '100%';
                if (progressPct) progressPct.textContent = '100%';

                // Download the result
                const fileRes = await fetch(`/api/tools/result/${jobId}`);
                if (!fileRes.ok) throw new Error('Failed to download result');
                const blob = await fileRes.blob();
                const filename = getFilenameFromResponse(fileRes) || 'processed_file';
                showResult(toolId, blob, filename);
                showProcessing(toolId, false);
            } else if (data.status === 'error') {
                clearInterval(state.jobPolls[toolId]);
                delete state.jobPolls[toolId];
                showProcessing(toolId, false);
                showToast(data.error || 'Processing failed', 'error');
            } else {
                // Update progress
                const pct = data.progress || 0;
                if (progressBar) progressBar.style.width = pct + '%';
                if (progressPct) progressPct.textContent = pct > 0 ? Math.round(pct) + '%' : '';
                if (procText && data.stage) procText.textContent = data.stage;
            }
        } catch (err) {
            // keep polling
        }
    }, 1000);
}

function getSelectedFmt(toolId) {
    const grid = document.querySelector(`.format-select-grid[data-tool="${toolId}"]`);
    const active = grid?.querySelector('.fmt-btn.active');
    return active?.dataset.fmt || '';
}

function getSelectedPreset(toolId) {
    const grid = document.querySelector(`.preset-grid[data-tool="${toolId}"]`);
    const active = grid?.querySelector('.preset-btn.active');
    return active?.dataset.val || '';
}

function getFilenameFromResponse(res) {
    const disp = res.headers.get('content-disposition');
    if (disp) {
        const match = disp.match(/filename[*]?=(?:UTF-8'')?["']?([^"';\n]+)/i);
        if (match) return decodeURIComponent(match[1]);
    }
    return null;
}

function showProcessing(toolId, show) {
    const el = document.querySelector(`.processing-status[data-tool="${toolId}"]`);
    if (el) el.classList.toggle('hidden', !show);
    // Disable process button
    const panel = document.getElementById('tool-' + toolId);
    const btn = panel?.querySelector('.process-btn');
    if (btn) btn.disabled = show;
}

function showResult(toolId, blob, filename) {
    const result = document.querySelector(`.result-section[data-tool="${toolId}"]`);
    if (!result) return;
    result.classList.remove('hidden');

    result.querySelector('.result-name').textContent = filename;
    result.querySelector('.result-size').textContent = formatBytes(blob.size);

    const downloadLink = result.querySelector('.result-download');
    if (downloadLink._objectUrl) URL.revokeObjectURL(downloadLink._objectUrl);
    const objectUrl = URL.createObjectURL(blob);
    downloadLink._objectUrl = objectUrl;
    downloadLink.href = objectUrl;
    downloadLink.download = filename;
}

function showMultiResult(toolId, pages) {
    const result = document.querySelector(`.result-section[data-tool="${toolId}"]`);
    if (!result) return;
    result.classList.remove('hidden');

    result.querySelector('.result-name').textContent = `${pages.length} pages extracted`;
    result.querySelector('.result-size').textContent = '';

    // Replace download link with a page list
    const downloadLink = result.querySelector('.result-download');
    downloadLink.style.display = 'none';

    let listEl = result.querySelector('.multi-result-list');
    if (!listEl) {
        listEl = document.createElement('div');
        listEl.className = 'multi-result-list';
        result.appendChild(listEl);
    }
    listEl.innerHTML = pages.map(p =>
        `<a href="${p.url}" download="${escapeHTML(p.name)}" class="result-page-link"><i class="fas fa-download"></i> ${escapeHTML(p.name)}</a>`
    ).join('');
}

// ═══════════════════════════════════════════════════════════════════════════
// DOWNLOADER (existing logic preserved)
// ═══════════════════════════════════════════════════════════════════════════

let detectTimeout;

function initDownloader() {
    const urlInput = $('#urlInput');
    if (!urlInput) return;

    urlInput.addEventListener('input', (e) => {
        clearTimeout(detectTimeout);
        detectTimeout = setTimeout(() => {
            const url = e.target.value.trim();
            state.url = url;
            const platform = detectPlatform(url);
            const badge = $('#platformBadge');
            const wrapper = $('#inputWrapper');
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
    const urlInput = $('#urlInput');
    const url = urlInput.value.trim();
    if (!url) { showToast('Please enter a URL', 'error'); return; }
    if (!url.match(/^https?:\/\/.+/i)) { urlInput.value = 'https://' + url; state.url = urlInput.value; } else { state.url = url; }

    showDlSection('loading');
    $('#analyzeBtn').disabled = true;

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
        $('#errorText').textContent = msg;
        showDlSection('error');
    } finally { $('#analyzeBtn').disabled = false; }
}

function retryAnalysis() { analyzeURL(); }

function renderMediaInfo(data) {
    const thumb = $('#mediaThumbnail');
    if (data.thumbnail) {
        thumb.src = data.thumbnail;
        thumb.onerror = () => { thumb.src = 'data:image/svg+xml,' + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="320" height="180" fill="%23222"><rect width="320" height="180"/><text x="160" y="95" text-anchor="middle" fill="%23666" font-size="14">No Thumbnail</text></svg>'); };
    }
    if (data.duration) {
        const m = Math.floor(data.duration / 60), s = Math.floor(data.duration % 60);
        $('#mediaDuration').textContent = `${m}:${s.toString().padStart(2, '0')}`;
    } else { $('#mediaDuration').textContent = ''; }

    $('#mediaTitle').textContent = data.title || 'Unknown Title';
    $('#mediaUploader').querySelector('span').textContent = data.uploader || 'Unknown';

    if (data.platform) {
        $('#platformOverlay').innerHTML = `<i class="${data.platform.icon}"></i>`;
        $('#platformOverlay').style.background = data.platform.color;
    }

    const isAudioOnly = data.platform && !data.platform.supports_video;
    const mp4Tab = $('#formatTabs').querySelector('[data-format="mp4"]');
    if (isAudioOnly) { mp4Tab.classList.add('disabled'); selectFormat('mp3'); } else { mp4Tab.classList.remove('disabled'); }
    renderQualities(data.formats || []);
}

function renderQualities(formats) {
    const grid = $('#qualityGrid');
    grid.innerHTML = '';
    const bestChip = document.createElement('button');
    bestChip.className = 'quality-chip active'; bestChip.dataset.quality = 'best';
    bestChip.innerHTML = 'Best Quality'; bestChip.onclick = () => selectQuality('best');
    grid.appendChild(bestChip);
    for (const q of formats.filter(f => f.height > 0)) {
        const chip = document.createElement('button');
        chip.className = 'quality-chip'; chip.dataset.quality = q.quality;
        let sizeLabel = q.filesize > 0 ? `<span class="label">${formatBytes(q.filesize)}</span>` : '';
        chip.innerHTML = `${q.quality}${sizeLabel}`; chip.onclick = () => selectQuality(q.quality);
        grid.appendChild(chip);
    }
    state.selectedQuality = 'best';
}

function selectFormat(format) {
    state.selectedFormat = format;
    $$('.format-tab').forEach(tab => tab.classList.toggle('active', tab.dataset.format === format));
    const qs = $('#qualitySection');
    if (qs) { format === 'mp4' ? qs.classList.remove('hidden') : qs.classList.add('hidden'); }
}

function selectQuality(quality) {
    state.selectedQuality = quality;
    $$('.quality-chip').forEach(c => c.classList.toggle('active', c.dataset.quality === quality));
}

async function startDownload() {
    if (!state.url) return;
    $('#downloadBtn').disabled = true;
    showDlSection('progress');
    try {
        const data = await apiCall('/api/download', { url: state.url, format: state.selectedFormat, quality: state.selectedQuality, title: state.mediaInfo?.title || '' });
        state.downloadId = data.download_id;
        pollDownloadStatus();
    } catch (err) { showToast('Download failed: ' + err.message, 'error'); showDlSection('media'); $('#downloadBtn').disabled = false; }
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
                $('#progressBar').style.width = '100%'; $('#progressTitle').textContent = 'Complete!'; $('#progressStatus').textContent = 'Preparing file...';
                setTimeout(() => { $('#saveBtn').href = data.download_url; $('#saveBtn').download = data.filename || 'download'; showDlSection('complete'); }, 600);
            } else if (data.status === 'error') {
                clearInterval(state.pollInterval); state.pollInterval = null;
                showToast('Download error: ' + (data.error || 'Unknown error'), 'error');
                showDlSection('media'); $('#downloadBtn').disabled = false;
            } else {
                const pct = data.progress || 0;
                $('#progressBar').style.width = Math.max(pct, 2) + '%';
                const pctEl = $('#progressPct'); if (pctEl) pctEl.textContent = pct > 0 ? pct.toFixed(1) + '%' : '';
                if (data.speed) { const el = $('#progressSpeed'); if (el) el.textContent = data.speed; }
                const etaEl = $('#progressEta'); if (etaEl) etaEl.textContent = (data.eta != null && data.eta >= 0) ? formatETA(data.eta) : '';
                if (data.filesize) { const el = $('#progressSize'); if (el) el.textContent = data.filesize; }
                if (data.status === 'processing') { $('#progressStatus').textContent = 'Processing file...'; $('#progressBar').style.width = '95%'; }
                else if (data.status === 'downloading') { $('#progressStatus').textContent = 'Downloading...'; }
                else { $('#progressStatus').textContent = 'Starting download...'; }
            }
        } catch (err) { /* keep polling */ }
    }, 800);
}

function resetDownloaderUI() {
    const urlInput = $('#urlInput');
    if (urlInput) urlInput.value = '';
    state.url = ''; state.mediaInfo = null; state.downloadId = null;
    state.selectedFormat = 'mp3'; state.selectedQuality = 'best';
    state.isDownloading = false; state.playlistItems = []; state.batchResults = [];
    if (state.pollInterval) { clearInterval(state.pollInterval); state.pollInterval = null; }
    const badge = $('#platformBadge');
    if (badge) { badge.innerHTML = '<i class="fas fa-link"></i>'; badge.classList.remove('detected'); badge.style.background = ''; }
    const wrapper = $('#inputWrapper');
    if (wrapper) wrapper.classList.remove('detected');
    const db = $('#downloadBtn'); if (db) db.disabled = false;
    const pb = $('#progressBar'); if (pb) pb.style.width = '0%';
    const pi = $('#playlistItems'); if (pi) pi.innerHTML = '';
    const bf = $('#batchFiles'); if (bf) bf.innerHTML = '';
    $$('#tool-downloader .format-tab').forEach(tab => tab.classList.toggle('active', tab.dataset.format === 'mp3'));
    showDlSection(null);
    if (urlInput) urlInput.focus();
}

// ═══════════════════════════════════════════════════════════════════════════
// PLAYLIST FUNCTIONS (preserved from original)
// ═══════════════════════════════════════════════════════════════════════════

function renderPlaylist(data) {
    state.playlistItems = data.items || [];
    $('#playlistTitle').textContent = data.title || 'Playlist';
    $('#playlistCount').textContent = `${data.item_count || data.items.length} items`;
    $('#playlistUploader').textContent = data.uploader || 'Unknown';

    const isAudioOnly = data.platform && !data.platform.supports_video;
    const mp4Tab = $('#playlistFormatTabs')?.querySelector('[data-format="mp4"]');
    if (mp4Tab) { isAudioOnly ? mp4Tab.classList.add('disabled') : mp4Tab.classList.remove('disabled'); }
    if (isAudioOnly) selectFormat('mp3');

    const container = $('#playlistItems');
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
    const resolving = $('#playlistResolving'); if (resolving) resolving.classList.add('hidden');
    const skipBtn = $('#resolvingSkipBtn'); if (skipBtn) skipBtn.classList.add('hidden');
}

async function resolveUnknownTitles() {
    const unknowns = state.playlistItems.filter(item => /^Track \d+$/.test(item.title));
    if (unknowns.length === 0) return;
    state.resolvingTitles = true; state.resolveAborted = false;
    const resolving = $('#playlistResolving'); if (resolving) resolving.classList.remove('hidden');
    const skipBtn = $('#resolvingSkipBtn'); if (skipBtn) skipBtn.classList.add('hidden');
    const resolvingText = $('#resolvingText');
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
                const el = $('#playlistItems')?.querySelector(`[data-index="${item.index}"]`);
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
    const resolving = $('#playlistResolving'); if (resolving) resolving.classList.add('hidden');
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
    const sc = $('#selectedCount'); if (sc) sc.textContent = count;
    const pdb = $('#playlistDownloadBtn'); if (pdb) pdb.disabled = count === 0;
    const allSelected = count === total.length && total.length > 0;
    const sai = $('#selectAllIcon'); if (sai) sai.className = allSelected ? 'fas fa-times' : 'fas fa-check-double';
    const sat = $('#selectAllText'); if (sat) sat.textContent = allSelected ? 'Deselect All' : 'Select All';
}

async function startPlaylistDownload() {
    const selectedEls = [...$$('.playlist-item.selected')];
    if (selectedEls.length === 0) { showToast('Select at least one item to download', 'error'); return; }
    const selectedItems = selectedEls.map(el => { const idx = parseInt(el.dataset.index); return state.playlistItems[idx]; });
    state.batchResults = [];
    showDlSection('batchProgress');
    const total = selectedItems.length;
    $('#batchTotalNum').textContent = total; $('#batchCurrentNum').textContent = '0';
    $('#batchOverallBar').style.width = '0%'; $('#batchTitle').textContent = 'Downloading playlist...';
    for (let i = 0; i < total; i++) {
        const item = selectedItems[i], num = i + 1;
        $('#batchCurrentNum').textContent = num; $('#batchOverallBar').style.width = ((num - 1) / total * 100) + '%';
        $('#batchStatus').textContent = `Item ${num} of ${total}`; $('#batchItemName').textContent = item.title || `Track ${num}`;
        $('#batchItemBar').style.width = '0%'; $('#batchItemPct').textContent = ''; $('#batchItemSpeed').textContent = ''; $('#batchItemEta').textContent = '';
        try {
            const data = await apiCall('/api/download', { url: item.url, format: state.selectedFormat, quality: 'best', title: item.title || '' });
            const result = await pollBatchItem(data.download_id);
            state.batchResults.push({ title: item.title, ...result });
        } catch (err) { state.batchResults.push({ title: item.title, status: 'error', error: err.message }); }
    }
    $('#batchOverallBar').style.width = '100%';
    renderBatchComplete(); showDlSection('batchComplete');
}

function pollBatchItem(downloadId) {
    return new Promise((resolve) => {
        const interval = setInterval(async () => {
            try {
                const res = await fetch(`/api/status/${downloadId}`);
                const data = await res.json();
                if (data.status === 'completed') { clearInterval(interval); $('#batchItemBar').style.width = '100%'; $('#batchItemPct').textContent = '100%'; resolve({ status: 'completed', download_url: data.download_url, filename: data.filename }); }
                else if (data.status === 'error') { clearInterval(interval); resolve({ status: 'error', error: data.error || 'Download failed' }); }
                else {
                    const pct = data.progress || 0;
                    $('#batchItemBar').style.width = Math.max(pct, 2) + '%';
                    $('#batchItemPct').textContent = pct > 0 ? pct.toFixed(1) + '%' : '';
                    if (data.speed) $('#batchItemSpeed').textContent = data.speed;
                    if (data.eta != null && data.eta >= 0) $('#batchItemEta').textContent = formatETA(data.eta);
                }
            } catch { /* keep polling */ }
        }, 800);
    });
}

function renderBatchComplete() {
    const success = state.batchResults.filter(r => r.status === 'completed').length;
    const fail = state.batchResults.filter(r => r.status === 'error').length;
    $('#batchCompleteText').textContent = fail === 0 ? `All ${success} downloads complete!` : `${success} completed, ${fail} failed`;
    const container = $('#batchFiles'); container.innerHTML = '';
    for (const result of state.batchResults) {
        const row = document.createElement('div'); row.className = 'batch-file-row';
        if (result.status === 'completed') { row.innerHTML = `<span class="batch-file-name">${escapeHTML(result.title)}</span><a class="batch-file-save" href="${result.download_url}" download="${escapeHTML(result.filename || 'download')}"><i class="fas fa-save"></i> Save</a>`; }
        else { row.innerHTML = `<span class="batch-file-name">${escapeHTML(result.title)}</span><span class="batch-file-error"><i class="fas fa-times-circle"></i> Failed</span>`; }
        container.appendChild(row);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// UTILITIES
// ═══════════════════════════════════════════════════════════════════════════

function formatBytes(bytes) {
    if (!bytes || bytes === 0) return '0 B';
    const k = 1024, sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];
}

function escapeHTML(str) {
    const div = document.createElement('div'); div.textContent = str; return div.innerHTML;
}

function showToast(message, type = 'info') {
    const existing = document.querySelector('.toast'); if (existing) existing.remove();
    const toast = document.createElement('div'); toast.className = `toast ${type}`;
    const icon = type === 'error' ? 'fas fa-exclamation-circle' : type === 'success' ? 'fas fa-check-circle' : 'fas fa-info-circle';
    toast.innerHTML = `<i class="${icon}"></i> ${escapeHTML(message)}`;
    document.body.appendChild(toast);
    setTimeout(() => toast.remove(), 3500);
}

// ═══════════════════════════════════════════════════════════════════════════
// HEALTH CHECK
// ═══════════════════════════════════════════════════════════════════════════

async function checkServerHealth() {
    const statusEl = $('#serverStatus');
    if (!statusEl) return;
    const dot = statusEl.querySelector('.status-dot'), text = statusEl.querySelector('.status-text');
    try {
        const res = await fetch('/api/health');
        const data = await res.json();
        if (data.status === 'ok') {
            dot.className = 'status-dot online';
            let info = [];
            if (data.yt_dlp_available) info.push('yt-dlp ' + data.yt_dlp_version);
            if (data.ffmpeg_available) info.push('FFmpeg');
            text.textContent = info.length ? info.join(' | ') : 'Online';
        } else {
            dot.className = 'status-dot offline'; text.textContent = 'Server error';
        }
    } catch { dot.className = 'status-dot offline'; text.textContent = 'Server offline'; }
}

// ═══════════════════════════════════════════════════════════════════════════
// PARTICLES
// ═══════════════════════════════════════════════════════════════════════════

function initParticles() {
    const canvas = document.getElementById('particles');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    let width, height;
    const particles = [], COUNT = 50;

    function resize() { width = canvas.width = window.innerWidth; height = canvas.height = window.innerHeight; }
    function create() { return { x: Math.random() * width, y: Math.random() * height, vx: (Math.random() - 0.5) * 0.3, vy: (Math.random() - 0.5) * 0.3, size: Math.random() * 2 + 0.5, alpha: Math.random() * 0.3 + 0.05 }; }

    function animate() {
        ctx.clearRect(0, 0, width, height);
        for (const p of particles) {
            p.x += p.vx; p.y += p.vy;
            if (p.x < 0) p.x = width; if (p.x > width) p.x = 0;
            if (p.y < 0) p.y = height; if (p.y > height) p.y = 0;
            ctx.beginPath(); ctx.arc(p.x, p.y, p.size, 0, Math.PI * 2);
            ctx.fillStyle = `rgba(124, 92, 255, ${p.alpha})`; ctx.fill();
        }
        for (let i = 0; i < particles.length; i++) {
            for (let j = i + 1; j < particles.length; j++) {
                const dx = particles[i].x - particles[j].x, dy = particles[i].y - particles[j].y;
                const dist = Math.sqrt(dx * dx + dy * dy);
                if (dist < 150) {
                    ctx.beginPath(); ctx.moveTo(particles[i].x, particles[i].y); ctx.lineTo(particles[j].x, particles[j].y);
                    ctx.strokeStyle = `rgba(124, 92, 255, ${0.05 * (1 - dist / 150)})`; ctx.lineWidth = 0.5; ctx.stroke();
                }
            }
        }
        requestAnimationFrame(animate);
    }

    window.addEventListener('resize', resize);
    resize();
    for (let i = 0; i < COUNT; i++) particles.push(create());
    animate();
}

// ═══════════════════════════════════════════════════════════════════════════
// INIT
// ═══════════════════════════════════════════════════════════════════════════

document.addEventListener('DOMContentLoaded', () => {
    initParticles();
    initUploadZones();
    initDownloader();
    checkServerHealth();

    // Initialize default output formats and presets
    document.querySelectorAll('.format-select-grid').forEach(grid => {
        const active = grid.querySelector('.fmt-btn.active');
        if (active && grid.dataset.tool) state.outputFormats[grid.dataset.tool] = active.dataset.fmt;
    });
    document.querySelectorAll('.preset-grid').forEach(grid => {
        const active = grid.querySelector('.preset-btn.active');
        if (active && grid.dataset.tool) state.presets[grid.dataset.tool] = active.dataset.val;
    });

    const urlInput = $('#urlInput');
    if (urlInput) urlInput.focus();
});
