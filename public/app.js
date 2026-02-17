/**
 * Luma Tools — Frontend Application
 * Handles URL detection, media analysis, and download management
 */

// ─── State ──────────────────────────────────────────────────────────────────

const state = {
    url: '',
    platform: null,
    mediaInfo: null,
    selectedFormat: 'mp3',
    selectedQuality: 'best',
    downloadId: null,
    pollInterval: null,
    isDownloading: false,
    playlistItems: [],   // items from playlist analyze
    batchResults: [],    // { title, url, status, download_url, filename, error }
    resolvingTitles: false,  // whether we're currently resolving track names
    resolveAborted: false,   // user clicked Skip
};

// ─── DOM Elements ───────────────────────────────────────────────────────────

const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => document.querySelectorAll(sel);

const DOM = {
    urlInput:        $('#urlInput'),
    inputWrapper:    $('#inputWrapper'),
    platformBadge:   $('#platformBadge'),
    analyzeBtn:      $('#analyzeBtn'),
    loadingSection:  $('#loadingSection'),
    errorSection:    $('#errorSection'),
    errorText:       $('#errorText'),
    mediaSection:    $('#mediaSection'),
    mediaThumbnail:  $('#mediaThumbnail'),
    mediaDuration:   $('#mediaDuration'),
    mediaTitle:      $('#mediaTitle'),
    mediaUploader:   $('#mediaUploader'),
    platformOverlay: $('#platformOverlay'),
    formatTabs:      $('#formatTabs'),
    qualitySection:  $('#qualitySection'),
    qualityGrid:     $('#qualityGrid'),
    downloadBtn:     $('#downloadBtn'),
    progressSection: $('#progressSection'),
    progressBar:     $('#progressBar'),
    progressTitle:   $('#progressTitle'),
    progressStatus:  $('#progressStatus'),
    progressPct:     $('#progressPct'),
    progressSpeed:   $('#progressSpeed'),
    progressEta:     $('#progressEta'),
    progressSize:    $('#progressSize'),
    completeSection: $('#completeSection'),
    saveBtn:         $('#saveBtn'),
    serverStatus:    $('#serverStatus'),
    // Playlist elements
    playlistSection:     $('#playlistSection'),
    playlistTitle:       $('#playlistTitle'),
    playlistCount:       $('#playlistCount'),
    playlistUploader:    $('#playlistUploader'),
    playlistItems:       $('#playlistItems'),
    playlistDownloadBtn: $('#playlistDownloadBtn'),
    selectAllIcon:       $('#selectAllIcon'),
    selectAllText:       $('#selectAllText'),
    selectedCount:       $('#selectedCount'),
    playlistFormatTabs:  $('#playlistFormatTabs'),
    playlistResolving:   $('#playlistResolving'),
    resolvingText:       $('#resolvingText'),
    resolvingSkipBtn:    $('#resolvingSkipBtn'),
    // Batch progress elements
    batchProgressSection: $('#batchProgressSection'),
    batchTitle:           $('#batchTitle'),
    batchStatus:          $('#batchStatus'),
    batchOverallBar:      $('#batchOverallBar'),
    batchCurrentNum:      $('#batchCurrentNum'),
    batchTotalNum:        $('#batchTotalNum'),
    batchCurrentItem:     $('#batchCurrentItem'),
    batchItemName:        $('#batchItemName'),
    batchItemBar:         $('#batchItemBar'),
    batchItemPct:         $('#batchItemPct'),
    batchItemSpeed:       $('#batchItemSpeed'),
    batchItemEta:         $('#batchItemEta'),
    // Batch complete elements
    batchCompleteSection: $('#batchCompleteSection'),
    batchCompleteText:    $('#batchCompleteText'),
    batchFiles:           $('#batchFiles'),
};

// ─── Platform Detection (Client-side for instant feedback) ──────────────────

const PLATFORM_PATTERNS = [
    { pattern: /youtube\.com|youtu\.be/i,           id: 'youtube',    name: 'YouTube',     icon: 'fab fa-youtube',       color: '#FF0000' },
    { pattern: /tiktok\.com/i,                      id: 'tiktok',     name: 'TikTok',      icon: 'fab fa-tiktok',        color: '#00F2EA' },
    { pattern: /instagram\.com/i,                   id: 'instagram',  name: 'Instagram',   icon: 'fab fa-instagram',     color: '#E1306C' },
    { pattern: /spotify\.com/i,                     id: 'spotify',    name: 'Spotify',     icon: 'fab fa-spotify',       color: '#1DB954' },
    { pattern: /soundcloud\.com/i,                  id: 'soundcloud', name: 'SoundCloud',  icon: 'fab fa-soundcloud',    color: '#FF5500' },
    { pattern: /twitter\.com|x\.com/i,              id: 'twitter',    name: 'X / Twitter', icon: 'fab fa-x-twitter',     color: '#1DA1F2' },
    { pattern: /facebook\.com|fb\.watch/i,          id: 'facebook',   name: 'Facebook',    icon: 'fab fa-facebook',      color: '#1877F2' },
    { pattern: /twitch\.tv/i,                       id: 'twitch',     name: 'Twitch',      icon: 'fab fa-twitch',        color: '#9146FF' },
    { pattern: /vimeo\.com/i,                       id: 'vimeo',      name: 'Vimeo',       icon: 'fab fa-vimeo-v',       color: '#1AB7EA' },
    { pattern: /reddit\.com/i,                      id: 'reddit',     name: 'Reddit',      icon: 'fab fa-reddit-alien',  color: '#FF4500' },
    { pattern: /dailymotion\.com/i,                 id: 'dailymotion',name: 'Dailymotion', icon: 'fas fa-play-circle',   color: '#0066DC' },
    { pattern: /pinterest\.com/i,                   id: 'pinterest',  name: 'Pinterest',   icon: 'fab fa-pinterest',     color: '#E60023' },
];

const AUDIO_ONLY_PLATFORMS = ['spotify', 'soundcloud'];

function detectPlatform(url) {
    for (const p of PLATFORM_PATTERNS) {
        if (p.pattern.test(url)) return p;
    }
    return null;
}

// ─── URL Input Handling ─────────────────────────────────────────────────────

let detectTimeout;
DOM.urlInput.addEventListener('input', (e) => {
    clearTimeout(detectTimeout);
    detectTimeout = setTimeout(() => {
        const url = e.target.value.trim();
        state.url = url;

        const platform = detectPlatform(url);
        if (platform && url.length > 10) {
            state.platform = platform;
            DOM.platformBadge.innerHTML = `<i class="${platform.icon}"></i>`;
            DOM.platformBadge.classList.add('detected');
            DOM.platformBadge.style.background = platform.color;
            DOM.inputWrapper.classList.add('detected');
            document.documentElement.style.setProperty('--platform-color', platform.color);
        } else {
            state.platform = null;
            DOM.platformBadge.innerHTML = '<i class="fas fa-link"></i>';
            DOM.platformBadge.classList.remove('detected');
            DOM.platformBadge.style.background = '';
            DOM.inputWrapper.classList.remove('detected');
        }
    }, 200);
});

DOM.urlInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') analyzeURL();
});

// Handle paste
DOM.urlInput.addEventListener('paste', (e) => {
    setTimeout(() => {
        DOM.urlInput.dispatchEvent(new Event('input'));
        // Auto-analyze on paste
        setTimeout(() => analyzeURL(), 400);
    }, 50);
});

// ─── API Calls ──────────────────────────────────────────────────────────────

async function apiCall(endpoint, data) {
    const res = await fetch(endpoint, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(data),
    });
    const json = await res.json();
    if (!res.ok) throw new Error(json.error || 'Request failed');
    return json;
}

// ─── Analyze URL ────────────────────────────────────────────────────────────

async function analyzeURL() {
    const url = DOM.urlInput.value.trim();
    if (!url) {
        showToast('Please enter a URL', 'error');
        return;
    }

    // Validate URL format loosely
    if (!url.match(/^https?:\/\/.+/i)) {
        // Try adding https://
        DOM.urlInput.value = 'https://' + url;
        state.url = DOM.urlInput.value;
    } else {
        state.url = url;
    }

    showSection('loading');
    DOM.analyzeBtn.disabled = true;

    try {
        const data = await apiCall('/api/analyze', { url: state.url });
        state.mediaInfo = data;
        
        // Update platform info from server
        if (data.platform) {
            state.platform = data.platform;
            document.documentElement.style.setProperty('--platform-color', data.platform.color);
        }

        if (data.type === 'playlist') {
            renderPlaylist(data);
            showSection('playlist');
            resolveUnknownTitles();
        } else {
            renderMediaInfo(data);
            showSection('media');
        }
    } catch (err) {
        let msg = err.message || 'Failed to analyze URL';
        // Clean up yt-dlp error messages for display
        msg = msg.replace(/^ERROR:\s*/, '');
        msg = msg.replace(/\[\w+\]\s*\w+:\s*/, '');
        if (msg.length > 150) msg = msg.substring(0, 150) + '...';
        DOM.errorText.textContent = msg;
        showSection('error');
    } finally {
        DOM.analyzeBtn.disabled = false;
    }
}

function retryAnalysis() {
    analyzeURL();
}

// ─── Render Media Info ──────────────────────────────────────────────────────

function renderMediaInfo(data) {
    // Thumbnail
    if (data.thumbnail) {
        DOM.mediaThumbnail.src = data.thumbnail;
        DOM.mediaThumbnail.onerror = () => {
            DOM.mediaThumbnail.src = 'data:image/svg+xml,' + encodeURIComponent(
                '<svg xmlns="http://www.w3.org/2000/svg" width="320" height="180" fill="%23222"><rect width="320" height="180"/><text x="160" y="95" text-anchor="middle" fill="%23666" font-size="14">No Thumbnail</text></svg>'
            );
        };
    }

    // Duration
    if (data.duration) {
        const mins = Math.floor(data.duration / 60);
        const secs = Math.floor(data.duration % 60);
        DOM.mediaDuration.textContent = `${mins}:${secs.toString().padStart(2, '0')}`;
    } else {
        DOM.mediaDuration.textContent = '';
    }

    // Title & uploader
    DOM.mediaTitle.textContent = data.title || 'Unknown Title';
    DOM.mediaUploader.querySelector('span').textContent = data.uploader || 'Unknown';

    // Platform overlay
    if (data.platform) {
        DOM.platformOverlay.innerHTML = `<i class="${data.platform.icon}"></i>`;
        DOM.platformOverlay.style.background = data.platform.color;
    }

    // Format tabs
    const isAudioOnly = data.platform && !data.platform.supports_video;
    const mp4Tab = DOM.formatTabs.querySelector('[data-format="mp4"]');
    
    if (isAudioOnly) {
        mp4Tab.classList.add('disabled');
        selectFormat('mp3');
    } else {
        mp4Tab.classList.remove('disabled');
    }

    // Quality options
    renderQualities(data.formats || []);
}

function renderQualities(formats) {
    DOM.qualityGrid.innerHTML = '';

    // Always add "Best" option
    const bestChip = document.createElement('button');
    bestChip.className = 'quality-chip active';
    bestChip.dataset.quality = 'best';
    bestChip.innerHTML = 'Best Quality';
    bestChip.onclick = () => selectQuality('best');
    DOM.qualityGrid.appendChild(bestChip);

    // Add available qualities
    const qualities = formats.filter(f => f.height > 0);
    for (const q of qualities) {
        const chip = document.createElement('button');
        chip.className = 'quality-chip';
        chip.dataset.quality = q.quality;
        
        let sizeLabel = '';
        if (q.filesize > 0) {
            sizeLabel = `<span class="label">${formatBytes(q.filesize)}</span>`;
        }
        
        chip.innerHTML = `${q.quality}${sizeLabel}`;
        chip.onclick = () => selectQuality(q.quality);
        DOM.qualityGrid.appendChild(chip);
    }

    state.selectedQuality = 'best';
}

function formatBytes(bytes) {
    if (bytes === 0) return '';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];
}

// ─── Format & Quality Selection ─────────────────────────────────────────────

function selectFormat(format) {
    state.selectedFormat = format;

    $$('.format-tab').forEach(tab => {
        tab.classList.toggle('active', tab.dataset.format === format);
    });

    // Show/hide quality section
    if (format === 'mp4') {
        DOM.qualitySection.classList.remove('hidden');
    } else {
        DOM.qualitySection.classList.add('hidden');
    }
}

function selectQuality(quality) {
    state.selectedQuality = quality;
    $$('.quality-chip').forEach(chip => {
        chip.classList.toggle('active', chip.dataset.quality === quality);
    });
}

// ─── Download ───────────────────────────────────────────────────────────────

async function startDownload() {
    if (!state.url) return;

    DOM.downloadBtn.disabled = true;
    showSection('progress');

    try {
        const data = await apiCall('/api/download', {
            url: state.url,
            format: state.selectedFormat,
            quality: state.selectedQuality,
            title: state.mediaInfo?.title || '',
        });

        state.downloadId = data.download_id;
        pollDownloadStatus();
    } catch (err) {
        showToast('Download failed: ' + err.message, 'error');
        showSection('media');
        DOM.downloadBtn.disabled = false;
    }
}

function formatETA(seconds) {
    if (seconds == null || seconds < 0) return '';
    const m = Math.floor(seconds / 60);
    const s = seconds % 60;
    return m > 0 ? `${m}m ${s}s` : `${s}s`;
}

function pollDownloadStatus() {
    if (state.pollInterval) clearInterval(state.pollInterval);

    state.pollInterval = setInterval(async () => {
        try {
            const res = await fetch(`/api/status/${state.downloadId}`);
            const data = await res.json();

            if (data.status === 'completed') {
                clearInterval(state.pollInterval);
                state.pollInterval = null;

                // Animate progress to 100%
                DOM.progressBar.style.width = '100%';
                DOM.progressTitle.textContent = 'Complete!';
                DOM.progressStatus.textContent = 'Preparing file...';

                setTimeout(() => {
                    DOM.saveBtn.href = data.download_url;
                    DOM.saveBtn.download = data.filename || 'download';
                    showSection('complete');
                }, 600);

            } else if (data.status === 'error') {
                clearInterval(state.pollInterval);
                state.pollInterval = null;
                showToast('Download error: ' + (data.error || 'Unknown error'), 'error');
                showSection('media');
                DOM.downloadBtn.disabled = false;

            } else {
                // Use real progress from server
                const pct = data.progress || 0;
                DOM.progressBar.style.width = Math.max(pct, 2) + '%';

                if (DOM.progressPct) {
                    DOM.progressPct.textContent = pct > 0 ? pct.toFixed(1) + '%' : '';
                }
                if (DOM.progressSpeed && data.speed) {
                    DOM.progressSpeed.textContent = data.speed;
                }
                if (DOM.progressEta) {
                    DOM.progressEta.textContent = (data.eta != null && data.eta >= 0) ? formatETA(data.eta) : '';
                }
                if (DOM.progressSize && data.filesize) {
                    DOM.progressSize.textContent = data.filesize;
                }

                if (data.status === 'processing') {
                    DOM.progressStatus.textContent = 'Processing file...';
                    DOM.progressBar.style.width = '95%';
                } else if (data.status === 'downloading') {
                    DOM.progressStatus.textContent = 'Downloading...';
                } else {
                    DOM.progressStatus.textContent = 'Starting download...';
                }
            }
        } catch (err) {
            // Network error, keep polling
        }
    }, 800);
}

// ─── UI Helpers ─────────────────────────────────────────────────────────────

function showSection(section) {
    // Hide all content sections
    DOM.loadingSection.classList.add('hidden');
    DOM.errorSection.classList.add('hidden');
    DOM.mediaSection.classList.add('hidden');
    DOM.progressSection.classList.add('hidden');
    DOM.completeSection.classList.add('hidden');
    DOM.playlistSection.classList.add('hidden');
    DOM.batchProgressSection.classList.add('hidden');
    DOM.batchCompleteSection.classList.add('hidden');

    switch (section) {
        case 'loading':       DOM.loadingSection.classList.remove('hidden'); break;
        case 'error':         DOM.errorSection.classList.remove('hidden'); break;
        case 'media':         DOM.mediaSection.classList.remove('hidden'); break;
        case 'progress':      DOM.progressSection.classList.remove('hidden'); break;
        case 'complete':      DOM.completeSection.classList.remove('hidden'); break;
        case 'playlist':      DOM.playlistSection.classList.remove('hidden'); break;
        case 'batchProgress': DOM.batchProgressSection.classList.remove('hidden'); break;
        case 'batchComplete': DOM.batchCompleteSection.classList.remove('hidden'); break;
    }
}

function resetUI() {
    DOM.urlInput.value = '';
    state.url = '';
    state.mediaInfo = null;
    state.downloadId = null;
    state.selectedFormat = 'mp3';
    state.selectedQuality = 'best';
    state.isDownloading = false;
    state.playlistItems = [];
    state.batchResults = [];
    
    if (state.pollInterval) {
        clearInterval(state.pollInterval);
        state.pollInterval = null;
    }

    DOM.platformBadge.innerHTML = '<i class="fas fa-link"></i>';
    DOM.platformBadge.classList.remove('detected');
    DOM.platformBadge.style.background = '';
    DOM.inputWrapper.classList.remove('detected');
    DOM.downloadBtn.disabled = false;
    DOM.progressBar.style.width = '0%';
    DOM.playlistItems.innerHTML = '';
    DOM.batchFiles.innerHTML = '';

    // Reset format tabs in both sections
    $$('.format-tab').forEach(tab => {
        tab.classList.toggle('active', tab.dataset.format === 'mp3');
    });

    // Hide all sections
    showSection(null);
    DOM.urlInput.focus();
}

function showToast(message, type = 'info') {
    const existing = document.querySelector('.toast');
    if (existing) existing.remove();

    const toast = document.createElement('div');
    toast.className = `toast ${type}`;
    
    const icon = type === 'error' ? 'fas fa-exclamation-circle' :
                 type === 'success' ? 'fas fa-check-circle' : 'fas fa-info-circle';
    
    toast.innerHTML = `<i class="${icon}"></i> ${message}`;
    document.body.appendChild(toast);

    setTimeout(() => toast.remove(), 3500);
}

// ─── Playlist Functions ─────────────────────────────────────────────────────

function renderPlaylist(data) {
    state.playlistItems = data.items || [];

    DOM.playlistTitle.textContent = data.title || 'Playlist';
    DOM.playlistCount.textContent = `${data.item_count || data.items.length} items`;
    DOM.playlistUploader.textContent = data.uploader || 'Unknown';

    // Determine if platform is audio-only
    const isAudioOnly = data.platform && !data.platform.supports_video;
    const mp4Tab = DOM.playlistFormatTabs.querySelector('[data-format="mp4"]');
    if (isAudioOnly) {
        mp4Tab.classList.add('disabled');
        selectFormat('mp3');
    } else {
        mp4Tab.classList.remove('disabled');
    }

    // Build item list
    DOM.playlistItems.innerHTML = '';
    for (const item of state.playlistItems) {
        const el = document.createElement('div');
        el.className = 'playlist-item selected';
        el.dataset.index = item.index;

        const dur = item.duration > 0
            ? `${Math.floor(item.duration / 60)}:${Math.floor(item.duration % 60).toString().padStart(2, '0')}`
            : '';

        const uploaderText = item.uploader ? ` • ${item.uploader}` : '';

        el.innerHTML = `
            <div class="playlist-item-check"><i class="fas fa-check"></i></div>
            <span class="playlist-item-index">${item.index + 1}</span>
            <div class="playlist-item-info">
                <div class="playlist-item-title">${escapeHTML(item.title)}</div>
                <div class="playlist-item-meta">${escapeHTML(uploaderText.replace(/^ • /, ''))}</div>
            </div>
            <span class="playlist-item-duration">${dur}</span>
        `;

        el.addEventListener('click', () => toggleItem(el));
        DOM.playlistItems.appendChild(el);
    }

    updateSelectedCount();

    // Reset resolving state
    state.resolvingTitles = false;
    state.resolveAborted = false;
    DOM.playlistResolving.classList.add('hidden');
    DOM.resolvingSkipBtn.classList.add('hidden');
}

// ─── Title Resolution ───────────────────────────────────────────────────────

async function resolveUnknownTitles() {
    // Find items with generic "Track N" names
    const unknowns = state.playlistItems.filter(item => /^Track \d+$/.test(item.title));
    if (unknowns.length === 0) return;

    state.resolvingTitles = true;
    state.resolveAborted = false;

    // Show resolving indicator
    DOM.playlistResolving.classList.remove('hidden');
    DOM.resolvingSkipBtn.classList.add('hidden');
    DOM.resolvingText.textContent = `Loading track names (0/${unknowns.length})...`;

    // Show skip button after 3 seconds
    const skipTimer = setTimeout(() => {
        if (state.resolvingTitles && !state.resolveAborted) {
            DOM.resolvingSkipBtn.classList.remove('hidden');
        }
    }, 3000);

    let resolved = 0;
    for (const item of unknowns) {
        if (state.resolveAborted) break;

        try {
            const resp = await fetch('/api/resolve-title', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ url: item.url }),
            });
            const data = await resp.json();

            if (state.resolveAborted) break;

            if (data.title && data.title.trim()) {
                // Update state
                item.title = data.title;
                // Update DOM
                const el = DOM.playlistItems.querySelector(`[data-index="${item.index}"]`);
                if (el) {
                    const titleEl = el.querySelector('.playlist-item-title');
                    if (titleEl) {
                        titleEl.textContent = data.title;
                        titleEl.classList.add('just-resolved');
                        setTimeout(() => titleEl.classList.remove('just-resolved'), 700);
                    }
                }
            }
        } catch (err) {
            // Silently skip failed resolutions
        }

        resolved++;
        if (!state.resolveAborted) {
            DOM.resolvingText.textContent = `Loading track names (${resolved}/${unknowns.length})...`;
        }
    }

    // Done
    clearTimeout(skipTimer);
    state.resolvingTitles = false;
    DOM.playlistResolving.classList.add('hidden');
}

function skipResolving() {
    state.resolveAborted = true;
    state.resolvingTitles = false;
    DOM.playlistResolving.classList.add('hidden');
    showToast('Skipped loading track names', 'info');
}

function escapeHTML(str) {
    const div = document.createElement('div');
    div.textContent = str;
    return div.innerHTML;
}

function toggleItem(el) {
    el.classList.toggle('selected');
    updateSelectedCount();
}

function toggleSelectAll() {
    const items = $$('.playlist-item');
    const allSelected = [...items].every(el => el.classList.contains('selected'));

    items.forEach(el => {
        if (allSelected) {
            el.classList.remove('selected');
        } else {
            el.classList.add('selected');
        }
    });

    updateSelectedCount();
}

function updateSelectedCount() {
    const selected = $$('.playlist-item.selected');
    const total = $$('.playlist-item');
    const count = selected.length;

    DOM.selectedCount.textContent = count;
    DOM.playlistDownloadBtn.disabled = count === 0;

    // Update select all button text
    const allSelected = count === total.length && total.length > 0;
    DOM.selectAllIcon.className = allSelected ? 'fas fa-times' : 'fas fa-check-double';
    DOM.selectAllText.textContent = allSelected ? 'Deselect All' : 'Select All';
}

async function startPlaylistDownload() {
    const selectedEls = [...$$('.playlist-item.selected')];
    if (selectedEls.length === 0) {
        showToast('Select at least one item to download', 'error');
        return;
    }

    // Gather selected items
    const selectedItems = selectedEls.map(el => {
        const idx = parseInt(el.dataset.index);
        return state.playlistItems[idx];
    });

    state.batchResults = [];
    showSection('batchProgress');

    const total = selectedItems.length;
    DOM.batchTotalNum.textContent = total;
    DOM.batchCurrentNum.textContent = '0';
    DOM.batchOverallBar.style.width = '0%';
    DOM.batchTitle.textContent = 'Downloading playlist...';

    for (let i = 0; i < total; i++) {
        const item = selectedItems[i];
        const num = i + 1;

        DOM.batchCurrentNum.textContent = num;
        DOM.batchOverallBar.style.width = ((num - 1) / total * 100) + '%';
        DOM.batchStatus.textContent = `Item ${num} of ${total}`;
        DOM.batchItemName.textContent = item.title || `Track ${num}`;
        DOM.batchItemBar.style.width = '0%';
        DOM.batchItemPct.textContent = '';
        DOM.batchItemSpeed.textContent = '';
        DOM.batchItemEta.textContent = '';

        try {
            // Start download for this item
            const data = await apiCall('/api/download', {
                url: item.url,
                format: state.selectedFormat,
                quality: 'best',
                title: item.title || '',
            });

            const downloadId = data.download_id;

            // Poll until done
            const result = await pollBatchItem(downloadId);
            state.batchResults.push({
                title: item.title,
                ...result,
            });
        } catch (err) {
            state.batchResults.push({
                title: item.title,
                status: 'error',
                error: err.message,
            });
        }
    }

    // All done
    DOM.batchOverallBar.style.width = '100%';
    renderBatchComplete();
    showSection('batchComplete');
}

function pollBatchItem(downloadId) {
    return new Promise((resolve) => {
        const interval = setInterval(async () => {
            try {
                const res = await fetch(`/api/status/${downloadId}`);
                const data = await res.json();

                if (data.status === 'completed') {
                    clearInterval(interval);
                    DOM.batchItemBar.style.width = '100%';
                    DOM.batchItemPct.textContent = '100%';
                    resolve({
                        status: 'completed',
                        download_url: data.download_url,
                        filename: data.filename,
                    });
                } else if (data.status === 'error') {
                    clearInterval(interval);
                    resolve({
                        status: 'error',
                        error: data.error || 'Download failed',
                    });
                } else {
                    // Update individual item progress
                    const pct = data.progress || 0;
                    DOM.batchItemBar.style.width = Math.max(pct, 2) + '%';
                    DOM.batchItemPct.textContent = pct > 0 ? pct.toFixed(1) + '%' : '';
                    if (data.speed) DOM.batchItemSpeed.textContent = data.speed;
                    if (data.eta != null && data.eta >= 0) DOM.batchItemEta.textContent = formatETA(data.eta);
                }
            } catch {
                // Network error, keep polling
            }
        }, 800);
    });
}

function renderBatchComplete() {
    const successCount = state.batchResults.filter(r => r.status === 'completed').length;
    const failCount = state.batchResults.filter(r => r.status === 'error').length;

    if (failCount === 0) {
        DOM.batchCompleteText.textContent = `All ${successCount} downloads complete!`;
    } else {
        DOM.batchCompleteText.textContent = `${successCount} completed, ${failCount} failed`;
    }

    DOM.batchFiles.innerHTML = '';
    for (const result of state.batchResults) {
        const row = document.createElement('div');
        row.className = 'batch-file-row';

        if (result.status === 'completed') {
            row.innerHTML = `
                <span class="batch-file-name">${escapeHTML(result.title)}</span>
                <a class="batch-file-save" href="${result.download_url}" download="${escapeHTML(result.filename || 'download')}">
                    <i class="fas fa-save"></i> Save
                </a>
            `;
        } else {
            row.innerHTML = `
                <span class="batch-file-name">${escapeHTML(result.title)}</span>
                <span class="batch-file-error"><i class="fas fa-times-circle"></i> Failed</span>
            `;
        }

        DOM.batchFiles.appendChild(row);
    }
}

// ─── Server Health Check ────────────────────────────────────────────────────

async function checkServerHealth() {
    const statusEl = DOM.serverStatus;
    const dot = statusEl.querySelector('.status-dot');
    const text = statusEl.querySelector('.status-text');

    try {
        const res = await fetch('/api/health');
        const data = await res.json();
        
        if (data.yt_dlp_available) {
            dot.className = 'status-dot online';
            text.textContent = `yt-dlp ${data.yt_dlp_version}`;
        } else {
            dot.className = 'status-dot offline';
            text.textContent = 'yt-dlp not found';
            showToast('yt-dlp is not installed. Please install it first.', 'error');
        }
    } catch {
        dot.className = 'status-dot offline';
        text.textContent = 'Server offline';
    }
}

// ─── Particle Animation ────────────────────────────────────────────────────

function initParticles() {
    const canvas = document.getElementById('particles');
    const ctx = canvas.getContext('2d');
    
    let width, height;
    const particles = [];
    const PARTICLE_COUNT = 50;

    function resize() {
        width = canvas.width = window.innerWidth;
        height = canvas.height = window.innerHeight;
    }

    function createParticle() {
        return {
            x: Math.random() * width,
            y: Math.random() * height,
            vx: (Math.random() - 0.5) * 0.3,
            vy: (Math.random() - 0.5) * 0.3,
            size: Math.random() * 2 + 0.5,
            alpha: Math.random() * 0.3 + 0.05,
        };
    }

    function init() {
        resize();
        for (let i = 0; i < PARTICLE_COUNT; i++) {
            particles.push(createParticle());
        }
    }

    function animate() {
        ctx.clearRect(0, 0, width, height);

        for (const p of particles) {
            p.x += p.vx;
            p.y += p.vy;

            if (p.x < 0) p.x = width;
            if (p.x > width) p.x = 0;
            if (p.y < 0) p.y = height;
            if (p.y > height) p.y = 0;

            ctx.beginPath();
            ctx.arc(p.x, p.y, p.size, 0, Math.PI * 2);
            ctx.fillStyle = `rgba(124, 92, 255, ${p.alpha})`;
            ctx.fill();
        }

        // Draw lines between nearby particles
        for (let i = 0; i < particles.length; i++) {
            for (let j = i + 1; j < particles.length; j++) {
                const dx = particles[i].x - particles[j].x;
                const dy = particles[i].y - particles[j].y;
                const dist = Math.sqrt(dx * dx + dy * dy);

                if (dist < 150) {
                    ctx.beginPath();
                    ctx.moveTo(particles[i].x, particles[i].y);
                    ctx.lineTo(particles[j].x, particles[j].y);
                    ctx.strokeStyle = `rgba(124, 92, 255, ${0.05 * (1 - dist / 150)})`;
                    ctx.lineWidth = 0.5;
                    ctx.stroke();
                }
            }
        }

        requestAnimationFrame(animate);
    }

    window.addEventListener('resize', resize);
    init();
    animate();
}

// ─── Initialize ─────────────────────────────────────────────────────────────

document.addEventListener('DOMContentLoaded', () => {
    initParticles();
    checkServerHealth();
    DOM.urlInput.focus();
});
