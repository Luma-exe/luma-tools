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

        renderMediaInfo(data);
        showSection('media');
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

    switch (section) {
        case 'loading':  DOM.loadingSection.classList.remove('hidden'); break;
        case 'error':    DOM.errorSection.classList.remove('hidden'); break;
        case 'media':    DOM.mediaSection.classList.remove('hidden'); break;
        case 'progress': DOM.progressSection.classList.remove('hidden'); break;
        case 'complete': DOM.completeSection.classList.remove('hidden'); break;
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
