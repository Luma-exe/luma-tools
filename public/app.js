/**
 * Luma Tools — Frontend Application
 * Handles sidebar navigation, media downloading, and file processing tools
 */

// ═══════════════════════════════════════════════════════════════════════════
// STATE & CONFIG
// ═══════════════════════════════════════════════════════════════════════════

const state = {
    currentTool: 'landing',
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
    // Drop/batch state
    droppedFiles: [],   // Files held temporarily after global drop
    droppedCategory: null,
};

const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => document.querySelectorAll(sel);

const FILE_TOOL_MAP = {
    'image': [
        { id: 'image-compress', label: 'Compress', icon: 'fas fa-compress-alt' },
        { id: 'image-resize', label: 'Resize', icon: 'fas fa-expand-arrows-alt' },
        { id: 'image-convert', label: 'Convert', icon: 'fas fa-exchange-alt' },
        { id: 'image-crop', label: 'Crop', icon: 'fas fa-crop-alt' },
        { id: 'image-bg-remove', label: 'Remove BG', icon: 'fas fa-user-alt' },
        { id: 'favicon-generate', label: 'Favicon', icon: 'fas fa-star' },
        { id: 'metadata-strip', label: 'Strip Metadata', icon: 'fas fa-eraser' },
    ],
    'video': [
        { id: 'video-compress', label: 'Compress', icon: 'fas fa-file-video' },
        { id: 'video-trim', label: 'Trim / Cut', icon: 'fas fa-cut' },
        { id: 'video-convert', label: 'Convert', icon: 'fas fa-film' },
        { id: 'video-extract-audio', label: 'Extract Audio', icon: 'fas fa-volume-up' },
        { id: 'video-to-gif', label: 'To GIF', icon: 'fas fa-magic' },
        { id: 'video-speed', label: 'Speed', icon: 'fas fa-tachometer-alt' },
        { id: 'video-remove-audio', label: 'Remove Audio', icon: 'fas fa-volume-mute' },
    ],
    'audio': [
        { id: 'audio-convert', label: 'Convert', icon: 'fas fa-headphones' },
        { id: 'audio-normalize', label: 'Normalize', icon: 'fas fa-balance-scale-right' },
    ],
    'pdf': [
        { id: 'pdf-compress', label: 'Compress', icon: 'fas fa-file-pdf' },
        { id: 'pdf-to-images', label: 'To Images', icon: 'fas fa-images' },
    ],
};

// ═══════════════════════════════════════════════════════════════════════════
// SIDEBAR & NAVIGATION
// ═══════════════════════════════════════════════════════════════════════════

function switchTool(toolId) {
    state.currentTool = toolId;
    $$('.nav-item').forEach(el => el.classList.toggle('active', el.dataset.tool === toolId));
    $$('.tool-panel').forEach(el => el.classList.toggle('active', el.id === 'tool-' + toolId));

    // Show browser/server badge in the tool header
    const navItem = document.querySelector(`.nav-item[data-tool="${toolId}"]`);
    const panel = document.getElementById('tool-' + toolId);
    if (navItem && panel) {
        const loc = navItem.dataset.location; // 'browser' | 'server'
        // Remove old badge
        panel.querySelectorAll('.tool-location-badge').forEach(b => b.remove());
        if (loc) {
            const badge = document.createElement('span');
            badge.className = 'tool-location-badge loc-' + loc;
            badge.title = loc === 'browser' ? 'Runs entirely in your browser — files never leave your device' : 'Processed on our server — file is uploaded, then deleted';
            badge.innerHTML = loc === 'browser'
                ? '<i class="fas fa-lock"></i> In your browser'
                : '<i class="fas fa-server"></i> On our server';
            const h2 = panel.querySelector('.tool-header h2');
            if (h2) h2.appendChild(badge);
        }
    }
    if (window.innerWidth <= 768) toggleSidebar(false);
    window.scrollTo(0, 0);
    document.documentElement.scrollTop = 0;
    document.body.scrollTop = 0;
    const mc = $('.main-content');
    if (mc) mc.scrollTop = 0;
    const si = $('#sidebarSearch');
    if (si && si.value) { si.value = ''; filterSidebarTools(); }
}

function toggleSidebar(forceState) {
    const sidebar = $('#sidebar');
    const overlay = $('#sidebarOverlay');
    const isOpen = typeof forceState === 'boolean' ? forceState : !sidebar.classList.contains('open');
    sidebar.classList.toggle('open', isOpen);
    overlay.classList.toggle('open', isOpen);
    // Prevent body scroll when sidebar is open on mobile
    if (window.innerWidth <= 768) {
        document.body.style.overflow = isOpen ? 'hidden' : '';
    }
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

    // If this is the crop tool, initialize the crop canvas
    if (toolId === 'image-crop') initCropCanvas(file);
    if (toolId === 'redact') initRedactCanvas(file);

    // Audio/video waveform tools
    if (toolId === 'audio-convert') initWaveform('audio-convert', file);
    if (toolId === 'video-extract-audio') initWaveform('video-extract-audio', file);
    if (toolId === 'video-trim') initWaveform('video-trim', file);
}

// ═══════════════════════════════════════════════════════════════════════════
// AUDIO/VIDEO WAVEFORM VISUALIZATION
// ═══════════════════════════════════════════════════════════════════════════

let waveSurfers = {};

function initWaveform(toolId, file) {
    // Clean up previous instance
    if (waveSurfers[toolId]) { try { waveSurfers[toolId].destroy(); } catch(e) {} waveSurfers[toolId] = null; }

    let wrap, el;
    if (toolId === 'audio-convert') {
        wrap = $('#waveform-audio-convert'); el = $('#waveformAudioConvert');
    } else if (toolId === 'video-extract-audio') {
        wrap = $('#waveform-video-extract-audio'); el = $('#waveformVideoExtract');
    } else if (toolId === 'video-trim') {
        wrap = $('#waveform-video-trim'); el = $('#waveformVideoTrim');
    }
    if (!wrap || !el) return;

    wrap.style.display = '';
    el.innerHTML = '';

    // WaveSurfer v7 API: use load(url) not loadBlob()
    let ws;
    try {
        ws = WaveSurfer.create({
            container: el,
            waveColor: '#7c5cff',
            progressColor: '#6366f1',
            height: 80,
            barWidth: 2,
            barGap: 1,
            cursorColor: '#a78bfa',
        });
    } catch (e) {
        console.error('WaveSurfer create failed:', e);
        wrap.style.display = 'none';
        return;
    }

    waveSurfers[toolId] = ws;

    const url = URL.createObjectURL(file);
    ws.load(url).catch(err => {
        console.error('WaveSurfer load error:', err);
        wrap.style.display = 'none';
    });

    // Show trim time range on ready (no regions plugin needed)
    if (toolId === 'video-trim') {
        ws.on('ready', () => {
            const dur = ws.getDuration();
            const startEl = $('#waveformTrimStart');
            const endEl = $('#waveformTrimEnd');
            if (startEl) startEl.textContent = '00:00:00.00';
            if (endEl) endEl.textContent = formatTime(dur);
            // Sync waveform seek to trim inputs
            ws.on('seek', progress => {
                const t = formatTime(progress * dur);
                if ($('#trimStart') && !$('#trimStart').value) $('#trimStart').value = t;
            });
        });
    }

    ws.on('error', err => {
        console.error('WaveSurfer error:', err);
        wrap.style.display = 'none';
    });
}

function formatTime(sec) {
    sec = Math.max(0, sec);
    const h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60), s = sec % 60;
    return (h > 0 ? String(h).padStart(2, '0') + ':' : '') + String(m).padStart(2, '0') + ':' + String(s.toFixed(2)).padStart(5, '0');
}

// ═══════════════════════════════════════════════════════════════════════════
// PRIVACY REDACTION TOOL
// ═══════════════════════════════════════════════════════════════════════════

let redactImg = null;
let redactVideo = null;
let redactRegions = [];
let redactMode = 'box'; // 'box' or 'blur'

function initRedactCanvas(file) {
    const wrap = $('#redactCanvasWrap');
    const canvas = $('#redactCanvas');
    if (!wrap || !canvas) return;

    redactImg = null;
    redactVideo = null;
    redactRegions = [];

    $('#redactBoxBtn')?.classList.add('active');
    $('#redactBlurBtn')?.classList.remove('active');
    redactMode = 'box';

    if (file.type.startsWith('image/')) {
        const img = new Image();
        img.onload = () => {
            redactImg = img;
            canvas.width = img.naturalWidth;
            canvas.height = img.naturalHeight;
            wrap.classList.remove('hidden');
            drawRedactCanvas();
            initRedactDraw(canvas);
        };
        img.onerror = () => showToast('Could not load image', 'error');
        img.src = URL.createObjectURL(file);
    } else if (file.type.startsWith('video/')) {
        const video = document.createElement('video');
        video.preload = 'auto';
        video.muted = true;
        video.src = URL.createObjectURL(file);
        video.addEventListener('seeked', () => {
            canvas.width = video.videoWidth;
            canvas.height = video.videoHeight;
            wrap.classList.remove('hidden');
            redactVideo = video;
            drawRedactCanvas();
            initRedactDraw(canvas);
        }, { once: true });
        video.addEventListener('loadedmetadata', () => { video.currentTime = 0.1; });
    }
}

function drawRedactCanvas() {
    const canvas = $('#redactCanvas');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (redactImg) ctx.drawImage(redactImg, 0, 0, canvas.width, canvas.height);
    else if (redactVideo) ctx.drawImage(redactVideo, 0, 0, canvas.width, canvas.height);
    for (const reg of redactRegions) {
        if (reg.type === 'box') {
            ctx.save(); ctx.globalAlpha = 1.0; ctx.fillStyle = '#111';
            ctx.fillRect(reg.x, reg.y, reg.w, reg.h); ctx.restore();
        } else if (reg.type === 'blur') {
            try {
                const imgData = ctx.getImageData(reg.x, reg.y, reg.w, reg.h);
                ctx.putImageData(blurImageData(imgData, 10), reg.x, reg.y);
            } catch {}
        }
    }
}

function initRedactDraw(canvas) {
    let drawing = false, startX, startY;
    const scaleX = () => canvas.width / canvas.getBoundingClientRect().width;
    const scaleY = () => canvas.height / canvas.getBoundingClientRect().height;
    function getPos(e) {
        const rect = canvas.getBoundingClientRect();
        const cx = (e.clientX ?? e.touches?.[0]?.clientX ?? 0) - rect.left;
        const cy = (e.clientY ?? e.touches?.[0]?.clientY ?? 0) - rect.top;
        return { x: cx * scaleX(), y: cy * scaleY() };
    }
    canvas.onmousedown = canvas.ontouchstart = e => { e.preventDefault(); drawing = true; const p = getPos(e); startX = p.x; startY = p.y; };
    canvas.onmousemove = canvas.ontouchmove = e => {
        if (!drawing) return; e.preventDefault();
        const p = getPos(e);
        drawRedactCanvas();
        const ctx = canvas.getContext('2d');
        ctx.save(); ctx.strokeStyle = redactMode === 'box' ? '#fff' : '#7c5cff';
        ctx.lineWidth = 2; ctx.setLineDash([6, 4]);
        ctx.strokeRect(startX, startY, p.x - startX, p.y - startY); ctx.restore();
    };
    window.onmouseup = canvas.ontouchend = e => {
        if (!drawing) return; drawing = false;
        const p = getPos(e);
        const x = Math.round(Math.min(startX, p.x)), y = Math.round(Math.min(startY, p.y));
        const w = Math.round(Math.abs(p.x - startX)), h = Math.round(Math.abs(p.y - startY));
        if (w < 4 || h < 4) return;
        redactRegions.push({ type: redactMode, x, y, w, h });
        drawRedactCanvas();
    };
}

function blurImageData(imgData, radius) {
    const { data, width, height } = imgData;
    const out = new Uint8ClampedArray(data);
    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            let r=0,g=0,b=0,a=0,n=0;
            for (let dy = -radius; dy <= radius; dy++) {
                for (let dx = -radius; dx <= radius; dx++) {
                    const nx = x+dx, ny = y+dy;
                    if (nx>=0 && nx<width && ny>=0 && ny<height) {
                        const i=(ny*width+nx)*4; r+=data[i];g+=data[i+1];b+=data[i+2];a+=data[i+3];n++;
                    }
                }
            }
            const i=(y*width+x)*4; out[i]=r/n;out[i+1]=g/n;out[i+2]=b/n;out[i+3]=a/n;
        }
    }
    return new ImageData(out, width, height);
}

document.addEventListener('DOMContentLoaded', () => {
    $('#redactBoxBtn')?.addEventListener('click', () => {
        redactMode = 'box';
        $('#redactBoxBtn').classList.add('active');
        $('#redactBlurBtn').classList.remove('active');
    });
    $('#redactBlurBtn')?.addEventListener('click', () => {
        redactMode = 'blur';
        $('#redactBlurBtn').classList.add('active');
        $('#redactBoxBtn').classList.remove('active');
    });
    $('#redactUndoBtn')?.addEventListener('click', () => { redactRegions.pop(); drawRedactCanvas(); });
    $('#redactClearBtn')?.addEventListener('click', () => { redactRegions = []; drawRedactCanvas(); });
});

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
    if (toolId === 'pdf-merge' || toolId === 'images-to-pdf') {
        const files = state.multiFiles[toolId];
        if (toolId === 'pdf-merge' && (!files || files.length < 2)) { showToast('Please select at least 2 PDF files', 'error'); return; }
        if (toolId === 'images-to-pdf' && (!files || files.length < 1)) { showToast('Please select at least 1 image', 'error'); return; }
        return processMultiFile(toolId, files);
    }

    // Route to WASM for supported tools
    if (WASM_TOOLS[toolId]) {
        const file = state.files[toolId];
        if (!file) { showToast('Please select a file first', 'error'); return; }
        const builder = WASM_TOOLS[toolId];
        const opts = getWasmOpts(toolId);
        const cmd = builder(file, opts);
        if (cmd) {
            return processFileWasm(toolId);
        }
    }

    return processFileServer(toolId);
}

async function processFileServer(toolId) {
    const file = state.files[toolId];
    if (!file) { showToast('Please select a file first', 'error'); return; }

    const formData = new FormData();
    formData.append('file', file);

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
            formData.append('mode', getSelectedPreset('video-trim-mode') || 'fast');
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
        case 'video-to-gif':
            formData.append('fps', $('#gifFps').value || '15');
            formData.append('width', $('#gifWidth').value || '480');
            break;
        case 'video-speed':
            formData.append('speed', $('#videoSpeed').value || '2');
            break;
        case 'video-frame':
            formData.append('timestamp', $('#frameTimestamp').value || '00:00:00');
            break;
        case 'subtitle-extract':
            formData.append('format', getSelectedFmt('subtitle-extract') || 'srt');
            break;
        case 'image-crop':
            if (!state.cropRect) { showToast('Please select a crop region', 'error'); return; }
            formData.append('x', Math.round(state.cropRect.x).toString());
            formData.append('y', Math.round(state.cropRect.y).toString());
            formData.append('w', Math.round(state.cropRect.w).toString());
            formData.append('h', Math.round(state.cropRect.h).toString());
            break;
        case 'image-bg-remove':
            formData.append('method', getSelectedFmt('bg-remove-method') || 'auto');
            break;
        case 'redact': {
            // Images: export canvas directly — no server needed
            if (file.type.startsWith('image/')) {
                const canvas = $('#redactCanvas');
                if (!canvas || canvas.width === 0) { showToast('Please draw at least one redaction region', 'error'); return; }
                showProcessing(toolId, true);
                canvas.toBlob(blob => {
                    if (!blob) { showProcessing(toolId, false); showToast('Failed to export image', 'error'); return; }
                    const ext = file.name.endsWith('.png') ? '.png' : '.jpg';
                    const baseName = file.name.replace(/\.[^.]+$/, '') + '_redacted' + ext;
                    showResult(toolId, blob, baseName);
                    showProcessing(toolId, false);
                }, file.type.startsWith('image/png') ? 'image/png' : 'image/jpeg', 0.95);
                return;
            }
            // Videos: send to backend with regions JSON
            if (redactRegions.length === 0) { showToast('Please draw at least one redaction region', 'error'); return; }
            formData.append('regions', JSON.stringify(redactRegions));
            // Route to redact-video endpoint instead of /api/tools/redact
            showProcessing(toolId, true);
            try {
                const res = await fetch('/api/tools/redact-video', { method: 'POST', body: formData });
                if (!res.ok) { const e = await res.json().catch(() => ({})); throw new Error(e.error || 'Redaction failed'); }
                const blob = await res.blob();
                showResult(toolId, blob, file.name.replace(/\.[^.]+$/, '') + '_redacted' + (file.name.match(/\.[^.]+$/)?.[0] || '.mp4'));
            } catch (err) { showToast(err.message, 'error'); } finally { showProcessing(toolId, false); }
            return;
        }
    }

    showProcessing(toolId, true);

    const asyncTools = ['video-compress','video-trim','video-convert','video-extract-audio',
        'video-to-gif','gif-to-video','video-remove-audio','video-speed','video-stabilize','audio-normalize'];
    const isAsync = asyncTools.includes(toolId);

    try {
        const endpoint = '/api/tools/' + toolId;
        const res = await fetch(endpoint, { method: 'POST', body: formData });

        if (!res.ok) {
            const err = await res.json().catch(() => ({ error: 'Processing failed' }));
            throw new Error(err.error || 'Processing failed');
        }

        if (isAsync) {
            const data = await res.json();
            if (data.job_id) {
                pollJobStatus(toolId, data.job_id);
            } else {
                throw new Error('No job ID returned');
            }
        } else {
            const contentType = res.headers.get('content-type') || '';
            if (contentType.includes('application/json')) {
                const data = await res.json();
                if (data.hashes) {
                    showHashResults(toolId, data);
                } else if (data.pages && data.pages.length > 0) {
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

    const tagged = lumaTag(filename);
    result.querySelector('.result-name').textContent = tagged;
    result.querySelector('.result-size').textContent = formatBytes(blob.size);

    const downloadLink = result.querySelector('.result-download');
    if (downloadLink._objectUrl) URL.revokeObjectURL(downloadLink._objectUrl);
    const objectUrl = URL.createObjectURL(blob);
    downloadLink._objectUrl = objectUrl;
    downloadLink.href = objectUrl;
    downloadLink.download = tagged;
}

function showMultiResult(toolId, pages) {
    const result = document.querySelector(`.result-section[data-tool="${toolId}"]`);
    if (!result) return;
    result.classList.remove('hidden');
    result.classList.add('has-multi');

    result.querySelector('.result-name').textContent = `${pages.length} files generated`;
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
    listEl.innerHTML = pages.map(p => {
        const tagged = lumaTag(p.name);
        return `<a href="${p.url}" download="${escapeHTML(tagged)}" class="result-page-link"><i class="fas fa-download"></i> ${escapeHTML(tagged)}</a>`;
    }).join('');
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
                setTimeout(() => { $('#saveBtn').href = data.download_url; $('#saveBtn').download = lumaTag(data.filename || 'download'); showDlSection('complete'); }, 600);
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
        if (result.status === 'completed') { row.innerHTML = `<span class="batch-file-name">${escapeHTML(result.title)}</span><a class="batch-file-save" href="${result.download_url}" download="${escapeHTML(lumaTag(result.filename || 'download'))}"><i class="fas fa-save"></i> Save</a>`; }
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

function lumaTag(filename) {
    if (!filename) return 'file_LumaTools';
    const dot = filename.lastIndexOf('.');
    if (dot <= 0) return filename + '_LumaTools';
    const name = filename.slice(0, dot), ext = filename.slice(dot);
    if (name.endsWith('_LumaTools')) return filename;
    return name + '_LumaTools' + ext;
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
    const tickerTrack = $('#tickerTrack');
    try {
        const res = await fetch('/api/health');
        const data = await res.json();
        if (data.status === 'ok') {
            // Build ticker items
            const items = [];
            items.push({ label: 'Server', value: data.server || 'Online', ok: true });
            items.push({ label: 'FFmpeg', value: data.ffmpeg_available ? 'Available' : 'Missing', ok: data.ffmpeg_available });
            items.push({ label: 'yt-dlp', value: data.yt_dlp_available ? ('v' + data.yt_dlp_version) : 'Missing', ok: data.yt_dlp_available });
            items.push({ label: 'Ghostscript', value: data.ghostscript_available ? 'Available' : 'Not Found', ok: data.ghostscript_available });
            if (data.git_commit && data.git_commit !== 'unknown') {
                const branch = data.git_branch && data.git_branch !== 'unknown' ? data.git_branch : '';
                items.push({ label: 'Version', value: branch ? `${branch}@${data.git_commit}` : data.git_commit, ver: true });
            }

            // Render ticker HTML (duplicate for seamless loop)
            const renderItems = (arr) => arr.map(i =>
                `<span class="ticker-item"><span class="ticker-label">${i.label}:</span> ` +
                `<span class="${i.ver ? 'ticker-ver' : (i.ok ? 'ticker-ok' : 'ticker-err')}">${i.value}</span></span>`
            ).join('<span class="ticker-sep">•</span>');

            const once = renderItems(items);
            const html = once + '<span class="ticker-sep">│</span>' + once + '<span class="ticker-sep">│</span>';

            if (tickerTrack) {
                tickerTrack.innerHTML = html;
                initTickerDrag(tickerTrack);
            }
        } else {
            if (tickerTrack) tickerTrack.innerHTML = '<span class="status-text">Server error</span>';
        }
    } catch {
        if (tickerTrack) tickerTrack.innerHTML = '<span class="status-text">Server offline</span>';
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// TICKER DRAG-TO-SCROLL
// ═══════════════════════════════════════════════════════════════════════════

function initTickerDrag(track) {
    if (track._dragInit) return;
    track._dragInit = true;

    let isDragging = false;
    let startX = 0;
    let scrollOffset = 0;
    const DURATION = 40; // must match CSS animation-duration

    function getCurrentTranslateX() {
        const style = getComputedStyle(track);
        const matrix = new DOMMatrix(style.transform);
        return matrix.m41;
    }

    function onPointerDown(e) {
        isDragging = true;
        startX = e.clientX;
        // Capture current animated position BEFORE killing animation
        scrollOffset = getCurrentTranslateX();
        // Kill the animation completely so inline transform takes effect
        track.style.animation = 'none';
        track.style.transform = `translateX(${scrollOffset}px)`;
        track.classList.add('dragging');
        track.setPointerCapture(e.pointerId);
        e.preventDefault();
    }

    function onPointerMove(e) {
        if (!isDragging) return;
        const dx = e.clientX - startX;
        track.style.transform = `translateX(${scrollOffset + dx}px)`;
    }

    function onPointerUp() {
        if (!isDragging) return;
        isDragging = false;
        track.classList.remove('dragging');

        // Figure out where we are and resume the CSS animation from that point
        const currentX = getCurrentTranslateX();
        const totalWidth = track.scrollWidth / 2; // content is duplicated
        let progress = ((-currentX) % totalWidth) / totalWidth;
        if (progress < 0) progress += 1;
        if (isNaN(progress)) progress = 0;

        // Clear inline transform, restart animation from current progress
        track.style.transform = '';
        track.offsetHeight; // force reflow
        track.style.animation = `ticker-scroll ${DURATION}s linear infinite`;
        track.style.animationDelay = `-${progress * DURATION}s`;
    }

    track.addEventListener('pointerdown', onPointerDown);
    track.addEventListener('pointermove', onPointerMove);
    track.addEventListener('pointerup', onPointerUp);
    track.addEventListener('pointercancel', onPointerUp);
}

// ═══════════════════════════════════════════════════════════════════════════
// FRONTEND-ONLY TOOLS (QR, Base64, JSON, Color)
// ═══════════════════════════════════════════════════════════════════════════

function showHashResults(toolId, data) {
    showProcessing(toolId, false);
    const container = document.getElementById('hashResults');
    if (!container) return;
    let html = `<div class="hash-result-header"><i class="fas fa-file"></i> <strong>${data.filename}</strong> <span>(${formatSize(data.size)})</span></div>`;
    for (const [algo, hash] of Object.entries(data.hashes)) {
        html += `<div class="hash-row"><span class="hash-algo">${algo}</span><code class="hash-value">${hash}</code><button class="hash-copy" onclick="navigator.clipboard.writeText('${hash}');showToast('Copied!','success')"><i class="fas fa-copy"></i></button></div>`;
    }
    container.innerHTML = html;
    container.classList.remove('hidden');
}

function generateQR() {
    const text = document.getElementById('qrInput').value.trim();
    if (!text) { showToast('Please enter some text', 'error'); return; }
    const size = parseInt(document.getElementById('qrSize').value) || 6;
    try {
        const qr = qrcode(0, 'M');
        qr.addData(text);
        qr.make();
        const cellSize = size;
        const margin = 4;
        const moduleCount = qr.getModuleCount();
        const totalSize = moduleCount * cellSize + margin * 2;
        const wrap = document.getElementById('qrCanvasWrap');
        wrap.innerHTML = '';
        const canvas = document.createElement('canvas');
        canvas.width = totalSize; canvas.height = totalSize;
        canvas.id = 'qrCanvas';
        const ctx = canvas.getContext('2d');
        ctx.fillStyle = '#ffffff';
        ctx.fillRect(0, 0, totalSize, totalSize);
        ctx.fillStyle = '#000000';
        for (let r = 0; r < moduleCount; r++)
            for (let c = 0; c < moduleCount; c++)
                if (qr.isDark(r, c)) ctx.fillRect(c * cellSize + margin, r * cellSize + margin, cellSize, cellSize);
        wrap.appendChild(canvas);
        document.getElementById('qrOutput').classList.remove('hidden');
    } catch (e) { showToast('QR generation failed: ' + e.message, 'error'); }
}

function downloadQR() {
    const canvas = document.getElementById('qrCanvas');
    if (!canvas) return;
    const a = document.createElement('a');
    a.href = canvas.toDataURL('image/png');
    a.download = 'qrcode_LumaTools.png';
    a.click();
}

function processBase64() {
    const mode = getSelectedFmt('base64-mode') || 'encode';
    const input = document.getElementById('base64Input').value;
    if (!input) { showToast('Please enter some text', 'error'); return; }
    try {
        let result;
        if (mode === 'encode') {
            result = btoa(unescape(encodeURIComponent(input)));
        } else {
            result = decodeURIComponent(escape(atob(input.trim())));
        }
        document.getElementById('base64Output').value = result;
        document.getElementById('base64CopyBtn').classList.remove('hidden');
    } catch (e) { showToast('Conversion failed — invalid input', 'error'); }
}

function formatJSON() {
    const input = document.getElementById('jsonInput').value.trim();
    if (!input) { showToast('Please enter some JSON', 'error'); return; }
    try {
        const parsed = JSON.parse(input);
        const indent = getSelectedFmt('json-indent') || '4';
        let result;
        if (indent === '0') result = JSON.stringify(parsed);
        else if (indent === 'tab') result = JSON.stringify(parsed, null, '\t');
        else result = JSON.stringify(parsed, null, parseInt(indent));
        document.getElementById('jsonOutput').value = result;
        document.getElementById('jsonCopyBtn').classList.remove('hidden');
        showToast('JSON formatted successfully', 'success');
    } catch (e) { showToast('Invalid JSON: ' + e.message, 'error'); }
}

function copyToClipboard(elementId) {
    const el = document.getElementById(elementId);
    if (!el) return;
    navigator.clipboard.writeText(el.value).then(() => showToast('Copied to clipboard!', 'success'));
}

function updateColorFrom(source) {
    let r, g, b;
    if (source === 'hex') {
        const hex = document.getElementById('colorHex').value.trim();
        const m = hex.match(/^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i);
        if (!m) return;
        r = parseInt(m[1], 16); g = parseInt(m[2], 16); b = parseInt(m[3], 16);
    } else if (source === 'rgb') {
        const parts = document.getElementById('colorRgb').value.split(',').map(s => parseInt(s.trim()));
        if (parts.length < 3 || parts.some(isNaN)) return;
        [r, g, b] = parts;
    } else if (source === 'hsl') {
        const parts = document.getElementById('colorHsl').value.replace(/%/g, '').split(',').map(s => parseFloat(s.trim()));
        if (parts.length < 3 || parts.some(isNaN)) return;
        const [h, s, l] = [parts[0] / 360, parts[1] / 100, parts[2] / 100];
        if (s === 0) { r = g = b = Math.round(l * 255); }
        else {
            const hue2rgb = (p, q, t) => { if (t < 0) t += 1; if (t > 1) t -= 1; if (t < 1/6) return p + (q-p)*6*t; if (t < 1/2) return q; if (t < 2/3) return p + (q-p)*(2/3-t)*6; return p; };
            const q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
            r = Math.round(hue2rgb(p, q, h + 1/3) * 255); g = Math.round(hue2rgb(p, q, h) * 255); b = Math.round(hue2rgb(p, q, h - 1/3) * 255);
        }
    }
    r = Math.max(0, Math.min(255, r)); g = Math.max(0, Math.min(255, g)); b = Math.max(0, Math.min(255, b));
    const hex = '#' + [r,g,b].map(v => v.toString(16).padStart(2, '0')).join('');
    const rr = r/255, gg = g/255, bb = b/255;
    const max = Math.max(rr, gg, bb), min = Math.min(rr, gg, bb);
    let h, s, l = (max + min) / 2;
    if (max === min) { h = s = 0; }
    else {
        const d = max - min;
        s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
        switch (max) { case rr: h = ((gg - bb) / d + (gg < bb ? 6 : 0)) / 6; break; case gg: h = ((bb - rr) / d + 2) / 6; break; case bb: h = ((rr - gg) / d + 4) / 6; break; }
    }
    if (source !== 'hex') document.getElementById('colorHex').value = hex;
    if (source !== 'rgb') document.getElementById('colorRgb').value = `${r}, ${g}, ${b}`;
    if (source !== 'hsl') document.getElementById('colorHsl').value = `${Math.round(h*360)}, ${Math.round(s*100)}%, ${Math.round(l*100)}%`;
    document.getElementById('colorPreview').style.background = hex;
}

function switchColorTab(tab) {
    document.querySelectorAll('.color-tab').forEach((t, i) => t.classList.toggle('active', (tab === 'inputs' ? i === 0 : i === 1)));
    document.getElementById('colorTabInputs').classList.toggle('active', tab === 'inputs');
    document.getElementById('colorTabWheel').classList.toggle('active', tab === 'wheel');
    if (tab === 'wheel') initColorWheel();
}

let wheelInited = false;
function initColorWheel() {
    if (wheelInited) return;
    wheelInited = true;
    const canvas = document.getElementById('colorWheel');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const cx = canvas.width / 2, cy = canvas.height / 2, radius = cx - 4;
    drawWheel(ctx, cx, cy, radius, 1.0);

    let dragging = false;
    function pickColor(e) {
        const rect = canvas.getBoundingClientRect();
        const x = (e.clientX || e.touches[0].clientX) - rect.left;
        const y = (e.clientY || e.touches[0].clientY) - rect.top;
        const sx = x / rect.width * canvas.width;
        const sy = y / rect.height * canvas.height;
        const dx = sx - cx, dy = sy - cy;
        const dist = Math.sqrt(dx * dx + dy * dy);
        if (dist > radius) return;
        const cursor = document.getElementById('wheelCursor');
        cursor.style.left = (x / rect.width * 100) + '%';
        cursor.style.top = (y / rect.height * 100) + '%';
        const pixel = ctx.getImageData(Math.round(sx), Math.round(sy), 1, 1).data;
        const [r, g, b] = pixel;
        const hex = '#' + [r,g,b].map(v => v.toString(16).padStart(2, '0')).join('');
        document.getElementById('colorPreviewWheel').style.background = hex;
        document.getElementById('colorHexWheel').value = hex;
        document.getElementById('colorRgbWheel').value = `${r}, ${g}, ${b}`;
        cursor.style.background = hex;
        // Sync to main inputs tab too
        document.getElementById('colorHex').value = hex;
        document.getElementById('colorRgb').value = `${r}, ${g}, ${b}`;
        document.getElementById('colorPreview').style.background = hex;
        // Compute HSL
        const rr = r/255, gg = g/255, bb = b/255;
        const max = Math.max(rr, gg, bb), min = Math.min(rr, gg, bb);
        let h, s, l = (max + min) / 2;
        if (max === min) { h = s = 0; } else {
            const d = max - min; s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
            switch (max) { case rr: h = ((gg - bb) / d + (gg < bb ? 6 : 0)) / 6; break; case gg: h = ((bb - rr) / d + 2) / 6; break; case bb: h = ((rr - gg) / d + 4) / 6; break; }
        }
        document.getElementById('colorHsl').value = `${Math.round(h*360)}, ${Math.round(s*100)}%, ${Math.round(l*100)}%`;
    }
    canvas.addEventListener('mousedown', (e) => { dragging = true; pickColor(e); });
    canvas.addEventListener('mousemove', (e) => { if (dragging) pickColor(e); });
    window.addEventListener('mouseup', () => { dragging = false; });
    canvas.addEventListener('touchstart', (e) => { e.preventDefault(); dragging = true; pickColor(e); }, { passive: false });
    canvas.addEventListener('touchmove', (e) => { e.preventDefault(); if (dragging) pickColor(e); }, { passive: false });
    canvas.addEventListener('touchend', () => { dragging = false; });
}

function drawWheel(ctx, cx, cy, radius, brightness) {
    const img = ctx.createImageData(ctx.canvas.width, ctx.canvas.height);
    for (let y = 0; y < img.height; y++) {
        for (let x = 0; x < img.width; x++) {
            const dx = x - cx, dy = y - cy;
            const dist = Math.sqrt(dx * dx + dy * dy);
            const i = (y * img.width + x) * 4;
            if (dist <= radius) {
                let angle = Math.atan2(dy, dx) / (2 * Math.PI);
                if (angle < 0) angle += 1;
                const sat = dist / radius;
                const h = angle, s = sat, l = brightness * 0.5;
                // HSL to RGB
                let r, g, b;
                if (s === 0) { r = g = b = l; } else {
                    const hue2rgb = (p, q, t) => { if (t < 0) t += 1; if (t > 1) t -= 1; if (t < 1/6) return p + (q-p)*6*t; if (t < 1/2) return q; if (t < 2/3) return p + (q-p)*(2/3-t)*6; return p; };
                    const q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
                    r = hue2rgb(p, q, h + 1/3); g = hue2rgb(p, q, h); b = hue2rgb(p, q, h - 1/3);
                }
                img.data[i] = Math.round(r * 255); img.data[i+1] = Math.round(g * 255); img.data[i+2] = Math.round(b * 255); img.data[i+3] = 255;
            } else {
                img.data[i+3] = 0;
            }
        }
    }
    ctx.putImageData(img, 0, 0);
}

function updateWheelBrightness() {
    const val = parseInt(document.getElementById('wheelBrightness').value) / 100;
    const canvas = document.getElementById('colorWheel');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const cx = canvas.width / 2, cy = canvas.height / 2, radius = cx - 4;
    drawWheel(ctx, cx, cy, radius, val);
}

function formatSize(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1048576) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / 1048576).toFixed(1) + ' MB';
}

// ═══════════════════════════════════════════════════════════════════════════
// SIDEBAR SEARCH
// ═══════════════════════════════════════════════════════════════════════════

function filterSidebarTools() {
    const q = ($('#sidebarSearch')?.value || '').toLowerCase().trim();
    const cats = $$('#sidebarNav .nav-category');
    cats.forEach(cat => {
        const items = cat.querySelectorAll('.nav-item');
        let anyVisible = false;
        items.forEach(item => {
            const text = item.textContent.toLowerCase();
            const match = !q || text.includes(q);
            item.classList.toggle('search-hidden', !match);
            if (match) anyVisible = true;
        });
        cat.classList.toggle('search-hidden', !anyVisible);
    });
}


// ═══════════════════════════════════════════════════════════════════════════
// IMAGE CROP TOOL
// ═══════════════════════════════════════════════════════════════════════════

let cropImg = null;
let cropRatio = 'free';

function initCropCanvas(file) {
    const wrap = $('#cropCanvasWrap');
    const canvas = $('#cropCanvas');
    if (!wrap || !canvas) return;
    wrap.classList.remove('hidden');
    const ctx = canvas.getContext('2d');
    const img = new Image();
    img.onload = () => {
        cropImg = img;
        const maxW = wrap.parentElement.clientWidth - 48;
        const scale = Math.min(1, maxW / img.width);
        canvas.width = img.width * scale;
        canvas.height = img.height * scale;
        canvas.dataset.scaleX = img.width / canvas.width;
        canvas.dataset.scaleY = img.height / canvas.height;
        ctx.drawImage(img, 0, 0, canvas.width, canvas.height);
        // Default crop: full image
        state.cropRect = { x: 0, y: 0, w: img.width, h: img.height };
        updateCropSelection(0, 0, canvas.width, canvas.height);
        initCropDrag(canvas);
    };
    img.src = URL.createObjectURL(file);
}

function setCropRatio(ratio) { cropRatio = ratio; }

function initCropDrag(canvas) {
    let dragging = false, startX, startY;
    function getPos(e) {
        const rect = canvas.getBoundingClientRect();
        const x = (e.clientX || e.touches?.[0]?.clientX || 0) - rect.left;
        const y = (e.clientY || e.touches?.[0]?.clientY || 0) - rect.top;
        return { x: Math.max(0, Math.min(canvas.width, x)), y: Math.max(0, Math.min(canvas.height, y)) };
    }
    function onStart(e) {
        e.preventDefault();
        dragging = true;
        const p = getPos(e);
        startX = p.x; startY = p.y;
    }
    function onMove(e) {
        if (!dragging) return;
        e.preventDefault();
        const p = getPos(e);
        let x = Math.min(startX, p.x), y = Math.min(startY, p.y);
        let w = Math.abs(p.x - startX), h = Math.abs(p.y - startY);
        // Enforce ratio
        if (cropRatio !== 'free') {
            const parts = cropRatio.split(':').map(Number);
            const r = parts[0] / parts[1];
            if (w / h > r) w = h * r; else h = w / r;
        }
        w = Math.min(w, canvas.width - x);
        h = Math.min(h, canvas.height - y);
        updateCropSelection(x, y, w, h);
        const sx = parseFloat(canvas.dataset.scaleX);
        const sy = parseFloat(canvas.dataset.scaleY);
        state.cropRect = { x: x * sx, y: y * sy, w: w * sx, h: h * sy };
    }
    function onEnd() { dragging = false; }
    canvas.addEventListener('mousedown', onStart);
    canvas.addEventListener('mousemove', onMove);
    window.addEventListener('mouseup', onEnd);
    canvas.addEventListener('touchstart', onStart, { passive: false });
    canvas.addEventListener('touchmove', onMove, { passive: false });
    canvas.addEventListener('touchend', onEnd);
}

function updateCropSelection(x, y, w, h) {
    const sel = $('#cropSelection');
    if (!sel) return;
    sel.style.left = x + 'px'; sel.style.top = y + 'px';
    sel.style.width = w + 'px'; sel.style.height = h + 'px';
}

// ═══════════════════════════════════════════════════════════════════════════
// MARKDOWN PREVIEW
// ═══════════════════════════════════════════════════════════════════════════

function renderMarkdownPreview() {
    const input = $('#markdownInput')?.value || '';
    const output = $('#markdownOutput');
    if (!output) return;
    if (!input.trim()) { output.innerHTML = '<p class="text-muted">Preview will appear here...</p>'; return; }
    output.innerHTML = parseMarkdown(input);
}

function parseMarkdown(md) {
    let html = md
        .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
        // Code blocks
        .replace(/```([\s\S]*?)```/g, (_, code) => '<pre><code>' + code.trim() + '</code></pre>')
        // Inline code
        .replace(/`([^`]+)`/g, '<code>$1</code>')
        // Headings
        .replace(/^### (.+)$/gm, '<h3>$1</h3>')
        .replace(/^## (.+)$/gm, '<h2>$1</h2>')
        .replace(/^# (.+)$/gm, '<h1>$1</h1>')
        // HR
        .replace(/^---$/gm, '<hr>')
        // Bold & italic
        .replace(/\*\*\*(.+?)\*\*\*/g, '<strong><em>$1</em></strong>')
        .replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>')
        .replace(/\*(.+?)\*/g, '<em>$1</em>')
        // Strikethrough
        .replace(/~~(.+?)~~/g, '<del>$1</del>')
        // Images
        .replace(/!\[([^\]]*)\]\(([^)]+)\)/g, '<img src="$2" alt="$1">')
        // Links
        .replace(/\[([^\]]+)\]\(([^)]+)\)/g, '<a href="$2" target="_blank" rel="noopener">$1</a>')
        // Blockquotes
        .replace(/^&gt; (.+)$/gm, '<blockquote>$1</blockquote>')
        // Unordered lists
        .replace(/^[*-] (.+)$/gm, '<li>$1</li>')
        // Line breaks → paragraphs
        .replace(/\n\n/g, '</p><p>')
        .replace(/\n/g, '<br>');
    // Wrap loose li's in ul
    html = html.replace(/(<li>.*?<\/li>)/gs, '<ul>$1</ul>');
    html = html.replace(/<\/ul>\s*<ul>/g, '');
    return '<p>' + html + '</p>';
}

function copyMarkdownHTML() {
    const output = $('#markdownOutput');
    if (!output || !output.textContent.trim()) { showToast('Nothing to copy', 'error'); return; }
    navigator.clipboard.writeText(output.innerHTML).then(() => showToast('HTML copied to clipboard!', 'success'));
}

// ═══════════════════════════════════════════════════════════════════════════
// GLOBAL DRAG-DROP & QUICK-ACTION MODAL
// ═══════════════════════════════════════════════════════════════════════════

function detectFileCategory(file) {
    const type = (file.type || '').toLowerCase();
    const name = file.name.toLowerCase();
    if (type.startsWith('image/') || /\.(png|jpe?g|webp|bmp|tiff?|ico|gif|svg)$/.test(name)) return 'image';
    if (type.startsWith('video/') || /\.(mp4|webm|avi|mov|mkv|flv|wmv)$/.test(name)) return 'video';
    if (type.startsWith('audio/') || /\.(mp3|wav|flac|ogg|aac|m4a|wma|opus)$/.test(name)) return 'audio';
    if (type === 'application/pdf' || name.endsWith('.pdf')) return 'pdf';
    return null;
}

let _dragCounter = 0;

function initGlobalDrop() {
    const overlay = $('#dropOverlay');

    document.addEventListener('dragenter', (e) => {
        e.preventDefault();
        if (e.dataTransfer?.types?.includes('Files')) {
            _dragCounter++;
            overlay.classList.add('visible');
        }
    });

    document.addEventListener('dragleave', (e) => {
        e.preventDefault();
        _dragCounter--;
        if (_dragCounter <= 0) { _dragCounter = 0; overlay.classList.remove('visible'); }
    });

    document.addEventListener('dragover', (e) => e.preventDefault());

    document.addEventListener('drop', (e) => {
        e.preventDefault();
        _dragCounter = 0;
        overlay.classList.remove('visible');

        if (e.target.closest('.upload-zone')) return;

        const files = [...(e.dataTransfer?.files || [])];
        if (files.length === 0) return;

        const category = detectFileCategory(files[0]);
        if (!category) {
            showToast('Unsupported file type', 'error');
            return;
        }

        state.droppedFiles = files;
        state.droppedCategory = category;
        showQuickActionModal(category, files);
    });
}

function showQuickActionModal(category, files) {
    const backdrop = $('#quickActionBackdrop');
    const grid = $('#qaGrid');
    const title = $('#qaTitle');
    const fileInfo = $('#qaFileInfo');
    const batchHint = $('#qaBatchHint');
    const batchText = $('#qaBatchText');
    const tools = FILE_TOOL_MAP[category] || [];

    title.textContent = category.charAt(0).toUpperCase() + category.slice(1) + ' Tools';
    if (files.length === 1) {
        fileInfo.textContent = `${files[0].name} (${formatBytes(files[0].size)})`;
    } else {
        const totalSize = files.reduce((s, f) => s + f.size, 0);
        fileInfo.textContent = `${files.length} files (${formatBytes(totalSize)} total)`;
    }

    if (files.length > 1) {
        batchHint.classList.remove('hidden');
        batchText.textContent = `${files.length} files detected — choosing a tool will batch-process all files.`;
    } else {
        batchHint.classList.add('hidden');
    }

    grid.innerHTML = '';
    for (const tool of tools) {
        const btn = document.createElement('button');
        btn.className = 'quick-action-btn';
        btn.innerHTML = `<i class="${tool.icon}"></i><span>${tool.label}</span>`;
        btn.onclick = () => onQuickActionSelect(tool.id);
        grid.appendChild(btn);
    }

    backdrop.classList.add('visible');
}

function closeQuickAction(e) {
    if (e && e.target !== e.currentTarget) return;
    $('#quickActionBackdrop').classList.remove('visible');
    state.droppedFiles = [];
    state.droppedCategory = null;
}

function onQuickActionSelect(toolId) {
    const files = state.droppedFiles;
    $('#quickActionBackdrop').classList.remove('visible');

    if (files.length > 1) {
        batchQueue.start(toolId, files);
    } else if (files.length === 1) {
        switchTool(toolId);
        setTimeout(() => {
            handleFileSelect(toolId, files[0]);
            showToast(`File loaded into ${toolId.replace(/-/g, ' ')}`, 'success');
        }, 100);
    }
    state.droppedFiles = [];
    state.droppedCategory = null;
}

// ═══════════════════════════════════════════════════════════════════════════
// BATCH QUEUE MANAGER
// ═══════════════════════════════════════════════════════════════════════════

const batchQueue = {
    files: [],
    toolId: null,
    results: [],
    current: 0,
    total: 0,
    running: false,

    async start(toolId, files) {
        this.files = files;
        this.toolId = toolId;
        this.results = [];
        this.current = 0;
        this.total = files.length;
        this.running = true;

        switchTool(toolId);

        const overlay = $('#batchOverlay');
        overlay.classList.remove('hidden');
        $('#batchOvTitle').textContent = 'Batch Processing...';
        $('#batchOvBar').style.width = '0%';
        $('#batchOvTotal').textContent = this.total;
        $('#batchOvCurrent').textContent = '0';
        $('#batchOvDownloadAll').classList.add('hidden');
        $('#batchOvNewBtn').classList.add('hidden');
        const spinner = overlay.querySelector('.progress-icon');
        if (spinner) spinner.classList.add('spinning');

        this.renderFileList();
        await this.processNext();
    },

    renderFileList() {
        const list = $('#batchOvFileList');
        list.innerHTML = '';
        this.files.forEach((f, i) => {
            const entry = document.createElement('div');
            entry.className = 'batch-file-entry';
            entry.id = 'batchEntry-' + i;
            const statusClass = i < this.current ? 'done' : (i === this.current && this.running ? 'active' : 'pending');
            const statusIcon = i < this.current ? 'fa-check-circle' : (i === this.current && this.running ? 'fa-circle-notch fa-spin' : 'fa-clock');
            entry.innerHTML = `<span class="file-status-icon ${statusClass}"><i class="fas ${statusIcon}"></i></span>`
                + `<span class="batch-file-name" style="flex:1;margin-left:8px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">${escapeHTML(f.name)}</span>`
                + `<span style="font-size:0.72rem;color:var(--text-muted);font-family:var(--font-mono)">${formatBytes(f.size)}</span>`;
            list.appendChild(entry);
        });
    },

    updateEntry(index, status) {
        const entry = document.getElementById('batchEntry-' + index);
        if (!entry) return;
        const icon = entry.querySelector('.file-status-icon');
        if (status === 'done') {
            icon.className = 'file-status-icon done';
            icon.innerHTML = '<i class="fas fa-check-circle"></i>';
        } else if (status === 'error') {
            icon.className = 'file-status-icon error';
            icon.innerHTML = '<i class="fas fa-times-circle"></i>';
        } else if (status === 'active') {
            icon.className = 'file-status-icon active';
            icon.innerHTML = '<i class="fas fa-circle-notch fa-spin"></i>';
        }
    },

    async processNext() {
        if (this.current >= this.total) {
            this.finish();
            return;
        }
        const file = this.files[this.current];
        this.updateEntry(this.current, 'active');
        $('#batchOvStatus').textContent = `Processing ${this.current + 1} of ${this.total}: ${file.name}`;
        $('#batchOvCurrent').textContent = this.current;

        try {
            let resultBlob;
            if (WASM_TOOLS[this.toolId]) {
                resultBlob = await processFileWasmDirect(this.toolId, file);
            } else {
                resultBlob = await processFileServerDirect(this.toolId, file);
            }
            const ext = resultBlob._filename ? resultBlob._filename.split('.').pop() : file.name.split('.').pop();
            const outName = file.name.replace(/\.[^.]+$/, '') + '_LumaTools.' + ext;
            this.results.push({ name: outName, blob: resultBlob, status: 'done' });
            this.updateEntry(this.current, 'done');
        } catch (err) {
            this.results.push({ name: file.name, blob: null, status: 'error', error: err.message });
            this.updateEntry(this.current, 'error');
        }

        this.current++;
        this.updateProgress();
        await this.processNext();
    },

    updateProgress() {
        const pct = (this.current / this.total) * 100;
        $('#batchOvBar').style.width = pct + '%';
        $('#batchOvCurrent').textContent = this.current;
    },

    async finish() {
        this.running = false;
        const overlay = $('#batchOverlay');
        const spinner = overlay.querySelector('.progress-icon');
        if (spinner) spinner.classList.remove('spinning');

        const successCount = this.results.filter(r => r.status === 'done').length;
        const failCount = this.results.filter(r => r.status === 'error').length;
        $('#batchOvTitle').textContent = failCount === 0 ? 'Batch Complete!' : `${successCount} done, ${failCount} failed`;
        $('#batchOvStatus').textContent = `Processed ${this.total} files`;
        $('#batchOvBar').style.width = '100%';

        if (successCount > 0) {
            $('#batchOvDownloadAll').classList.remove('hidden');
        }
        $('#batchOvNewBtn').classList.remove('hidden');
    },

    reset() {
        this.files = [];
        this.toolId = null;
        this.results = [];
        this.current = 0;
        this.total = 0;
        this.running = false;
    },
};

async function processFileServerDirect(toolId, file) {
    const formData = new FormData();
    formData.append('file', file);

    switch (toolId) {
        case 'image-compress':
            formData.append('quality', $('#imageCompressQuality')?.value || '75');
            break;
        case 'image-resize':
            formData.append('width', $('#resizeWidth')?.value || '');
            formData.append('height', $('#resizeHeight')?.value || '');
            break;
        case 'image-convert':
            formData.append('format', getSelectedFmt('image-convert') || 'png');
            break;
        case 'video-compress':
            formData.append('preset', getSelectedPreset('video-compress') || 'medium');
            break;
        case 'video-convert':
            formData.append('format', getSelectedFmt('video-convert') || 'mp4');
            break;
        case 'audio-convert':
            formData.append('format', getSelectedFmt('audio-convert') || 'mp3');
            break;
        case 'pdf-compress':
            formData.append('level', getSelectedPreset('pdf-compress') || 'ebook');
            break;
        case 'metadata-strip':
            break;
        case 'image-bg-remove':
            formData.append('method', getSelectedFmt('bg-remove-method') || 'auto');
            break;
        default:
            break;
    }

    const asyncTools = ['video-compress','video-trim','video-convert','video-extract-audio',
        'video-to-gif','gif-to-video','video-remove-audio','video-speed','video-stabilize','audio-normalize'];
    const isAsync = asyncTools.includes(toolId);

    const res = await fetch('/api/tools/' + toolId, { method: 'POST', body: formData });
    if (!res.ok) {
        const err = await res.json().catch(() => ({ error: 'Processing failed' }));
        throw new Error(err.error || 'Processing failed');
    }

    if (isAsync) {
        const data = await res.json();
        if (!data.job_id) throw new Error('No job ID returned');
        return await pollJobForBlob(data.job_id);
    }

    const contentType = res.headers.get('content-type') || '';
    if (contentType.includes('application/json')) {
        throw new Error('Batch not supported for this tool');
    }
    const blob = await res.blob();
    const filename = getFilenameFromResponse(res) || file.name;
    blob._filename = filename;
    return blob;
}

function pollJobForBlob(jobId) {
    return new Promise((resolve, reject) => {
        const interval = setInterval(async () => {
            try {
                const res = await fetch(`/api/tools/status/${jobId}`);
                const data = await res.json();
                if (data.status === 'completed') {
                    clearInterval(interval);
                    const fileRes = await fetch(`/api/tools/result/${jobId}`);
                    if (!fileRes.ok) { reject(new Error('Failed to download result')); return; }
                    const blob = await fileRes.blob();
                    blob._filename = getFilenameFromResponse(fileRes) || 'processed_file';
                    resolve(blob);
                } else if (data.status === 'error') {
                    clearInterval(interval);
                    reject(new Error(data.error || 'Processing failed'));
                }
            } catch (err) { /* keep polling */ }
        }, 1000);
    });
}

async function batchDownloadAll() {
    const successResults = batchQueue.results.filter(r => r.status === 'done' && r.blob);
    if (successResults.length === 0) return;

    if (successResults.length === 1) {
        const a = document.createElement('a');
        a.href = URL.createObjectURL(successResults[0].blob);
        a.download = successResults[0].name;
        a.click();
        URL.revokeObjectURL(a.href);
        return;
    }

    const btn = $('#batchOvDownloadAll');
    btn.disabled = true;
    btn.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Creating zip...';

    try {
        const zip = new JSZip();
        for (const r of successResults) {
            zip.file(r.name, r.blob);
        }
        const zipBlob = await zip.generateAsync({ type: 'blob' });
        const a = document.createElement('a');
        a.href = URL.createObjectURL(zipBlob);
        a.download = 'LumaTools_batch.zip';
        a.click();
        URL.revokeObjectURL(a.href);
    } catch (err) {
        showToast('Failed to create zip: ' + err.message, 'error');
    } finally {
        btn.disabled = false;
        btn.innerHTML = '<i class="fas fa-file-archive"></i> Download All (.zip)';
    }
}

function closeBatchOverlay() {
    $('#batchOverlay').classList.add('hidden');
    batchQueue.reset();
}

// ═══════════════════════════════════════════════════════════════════════════
// FFMPEG.WASM — CLIENT-SIDE PROCESSING
// ═══════════════════════════════════════════════════════════════════════════

const WasmProcessor = {
    ffmpeg: null,
    loaded: false,
    loading: false,
    _loadPromise: null,

    async load() {
        if (this.loaded) return;
        if (this.loading) return this._loadPromise;

        this.loading = true;
        this._loadPromise = (async () => {
            try {
                showToast('Loading processing engine...', 'info');
                const { FFmpeg } = FFmpegWASM;
                this.ffmpeg = new FFmpeg();
                this.ffmpeg.on('log', ({ message }) => {
                    // Can be used for debugging
                });
                await this.ffmpeg.load({
                    coreURL: 'https://cdn.jsdelivr.net/npm/@ffmpeg/core@0.12.6/dist/umd/ffmpeg-core.js',
                });
                this.loaded = true;
                showToast('Processing engine ready!', 'success');
            } catch (err) {
                this.loading = false;
                this._loadPromise = null;
                throw err;
            } finally {
                this.loading = false;
            }
        })();
        return this._loadPromise;
    },

    async process(file, args, outputName, progressCb) {
        await this.load();
        const { fetchFile } = FFmpegUtil;

        if (progressCb) {
            this.ffmpeg.on('progress', ({ progress }) => {
                progressCb(Math.max(0, Math.min(1, progress)));
            });
        }

        const inputName = 'input' + getExtension(file.name);
        await this.ffmpeg.writeFile(inputName, await fetchFile(file));
        const fullArgs = args.map(a => a === '__INPUT__' ? inputName : a);
        await this.ffmpeg.exec(fullArgs);
        const data = await this.ffmpeg.readFile(outputName);
        await this.ffmpeg.deleteFile(inputName).catch(() => {});
        await this.ffmpeg.deleteFile(outputName).catch(() => {});

        if (progressCb) {
            this.ffmpeg.off('progress');
        }

        return new Blob([data.buffer], { type: mimeFromExt(outputName) });
    },
};

function getExtension(filename) {
    const dot = filename.lastIndexOf('.');
    return dot > 0 ? filename.slice(dot) : '';
}

function mimeFromExt(filename) {
    const ext = filename.split('.').pop().toLowerCase();
    const map = {
        'jpg': 'image/jpeg', 'jpeg': 'image/jpeg', 'png': 'image/png',
        'webp': 'image/webp', 'bmp': 'image/bmp', 'tiff': 'image/tiff',
        'ico': 'image/x-icon', 'gif': 'image/gif',
        'mp3': 'audio/mpeg', 'wav': 'audio/wav', 'flac': 'audio/flac',
        'ogg': 'audio/ogg', 'aac': 'audio/aac', 'm4a': 'audio/mp4',
        'mp4': 'video/mp4', 'webm': 'video/webm',
    };
    return map[ext] || 'application/octet-stream';
}

const WASM_TOOLS = {
    'image-compress': (file, opts) => {
        const q = Math.max(1, Math.min(31, 2 + (100 - (opts.quality || 75)) * 29 / 100));
        return { args: ['-i', '__INPUT__', '-q:v', String(Math.round(q)), 'output.jpg'], output: 'output.jpg' };
    },
    'image-resize': (file, opts) => {
        const w = opts.width || '-1', h = opts.height || '-1';
        const ext = getExtension(file.name) || '.png';
        return { args: ['-i', '__INPUT__', '-vf', `scale=${w}:${h}`, 'output' + ext], output: 'output' + ext };
    },
    'image-convert': (file, opts) => {
        const fmt = opts.format || 'png';
        return { args: ['-i', '__INPUT__', 'output.' + fmt], output: 'output.' + fmt };
    },
    'image-crop': (file, opts) => {
        const ext = getExtension(file.name) || '.png';
        const { x = 0, y = 0, w = 100, h = 100 } = opts;
        return { args: ['-i', '__INPUT__', '-vf', `crop=${w}:${h}:${x}:${y}`, 'output' + ext], output: 'output' + ext };
    },
    'image-bg-remove': (file, opts) => {
        const method = opts.method || 'white';
        if (method === 'auto') return null; // fall back to server for AI removal
        const color = method === 'black' ? 'black' : 'white';
        return { args: ['-i', '__INPUT__', '-vf', `colorkey=${color}:0.3:0.15,format=rgba`, 'output.png'], output: 'output.png' };
    },
    'metadata-strip': (file) => {
        const ext = getExtension(file.name) || '.jpg';
        return { args: ['-i', '__INPUT__', '-map_metadata', '-1', '-c', 'copy', 'output' + ext], output: 'output' + ext };
    },
    'audio-convert': (file, opts) => {
        if (file.size > 50 * 1024 * 1024) return null; // >50MB → server
        const fmt = opts.format || 'mp3';
        return { args: ['-i', '__INPUT__', 'output.' + fmt], output: 'output.' + fmt };
    },
    'favicon-generate': () => null, // multi-output; always use server
};

function getWasmOpts(toolId) {
    switch (toolId) {
        case 'image-compress':
            return { quality: parseInt($('#imageCompressQuality')?.value || '75') };
        case 'image-resize':
            return { width: $('#resizeWidth')?.value || '', height: $('#resizeHeight')?.value || '' };
        case 'image-convert':
            return { format: getSelectedFmt('image-convert') || 'png' };
        case 'image-crop':
            return state.cropRect || { x: 0, y: 0, w: 100, h: 100 };
        case 'image-bg-remove':
            return { method: getSelectedFmt('bg-remove-method') || 'auto' };
        case 'metadata-strip':
            return {};
        case 'audio-convert':
            return { format: getSelectedFmt('audio-convert') || 'mp3' };
        default:
            return {};
    }
}

async function processFileWasm(toolId) {
    const file = state.files[toolId];
    if (!file) { showToast('Please select a file first', 'error'); return; }

    const opts = getWasmOpts(toolId);
    const builder = WASM_TOOLS[toolId];
    const cmd = builder ? builder(file, opts) : null;

    if (!cmd) {
        return processFileServer(toolId);
    }

    showProcessing(toolId, true);
    const procEl = document.querySelector(`.processing-status[data-tool="${toolId}"]`);
    const procText = procEl?.querySelector('.processing-text') || procEl?.querySelector('span');

    try {
        const blob = await WasmProcessor.process(file, cmd.args, cmd.output, (progress) => {
            if (procText) procText.textContent = `Processing... ${Math.round(progress * 100)}%`;
        });
        const ext = cmd.output.split('.').pop();
        const outName = file.name.replace(/\.[^.]+$/, '') + '_LumaTools.' + ext;
        showResult(toolId, blob, outName);
    } catch (err) {
        showToast('Client processing failed, trying server...', 'info');
        try {
            await processFileServer(toolId);
            return;
        } catch (serverErr) {
            showToast(serverErr.message, 'error');
        }
    } finally {
        showProcessing(toolId, false);
    }
}

async function processFileWasmDirect(toolId, file) {
    const opts = getWasmOpts(toolId);
    const builder = WASM_TOOLS[toolId];
    const cmd = builder ? builder(file, opts) : null;

    if (!cmd) {
        return processFileServerDirect(toolId, file);
    }

    const blob = await WasmProcessor.process(file, cmd.args, cmd.output);
    blob._filename = file.name.replace(/\.[^.]+$/, '') + '_LumaTools.' + cmd.output.split('.').pop();
    return blob;
}

// ═══════════════════════════════════════════════════════════════════════════
// PWA INSTALL PROMPT
// ═══════════════════════════════════════════════════════════════════════════

let _deferredInstallPrompt = null;

function initInstallPrompt() {
    window.addEventListener('beforeinstallprompt', (e) => {
        e.preventDefault();
        _deferredInstallPrompt = e;
        const btn = $('#installAppBtn');
        if (btn) btn.classList.remove('hidden');
    });

    window.addEventListener('appinstalled', () => {
        _deferredInstallPrompt = null;
        const btn = $('#installAppBtn');
        if (btn) btn.classList.add('hidden');
        showToast('App installed successfully!', 'success');
    });
}

function installPWA() {
    if (!_deferredInstallPrompt) return;
    _deferredInstallPrompt.prompt();
    _deferredInstallPrompt.userChoice.then((choice) => {
        _deferredInstallPrompt = null;
        const btn = $('#installAppBtn');
        if (btn) btn.classList.add('hidden');
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// PARTICLES
// ═══════════════════════════════════════════════════════════════════════════

function initParticles() {
    const canvas = document.getElementById('particles');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    let width, height;
    const particles = [];
    const isMobile = window.innerWidth <= 768;
    const COUNT = isMobile ? 20 : 50;
    const CONNECT_DIST = isMobile ? 100 : 150;

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
                if (dist < CONNECT_DIST) {
                    ctx.beginPath(); ctx.moveTo(particles[i].x, particles[i].y); ctx.lineTo(particles[j].x, particles[j].y);
                    ctx.strokeStyle = `rgba(124, 92, 255, ${0.05 * (1 - dist / CONNECT_DIST)})`; ctx.lineWidth = 0.5; ctx.stroke();
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
// MOBILE SWIPE SUPPORT
// ═══════════════════════════════════════════════════════════════════════════

function initMobileSwipe() {
    if (window.innerWidth > 768) return;

    let touchStartX = 0, touchStartY = 0, swiping = false;
    const SWIPE_THRESHOLD = 50;
    const EDGE_ZONE = 30; // px from left edge to trigger open

    // Swipe right from left edge to open sidebar
    document.addEventListener('touchstart', (e) => {
        touchStartX = e.touches[0].clientX;
        touchStartY = e.touches[0].clientY;
        swiping = touchStartX < EDGE_ZONE || $('#sidebar').classList.contains('open');
    }, { passive: true });

    document.addEventListener('touchend', (e) => {
        if (!swiping) return;
        const dx = e.changedTouches[0].clientX - touchStartX;
        const dy = Math.abs(e.changedTouches[0].clientY - touchStartY);
        // Only trigger if horizontal swipe (not vertical scroll)
        if (dy > Math.abs(dx)) return;
        const sidebar = $('#sidebar');
        if (dx > SWIPE_THRESHOLD && !sidebar.classList.contains('open') && touchStartX < EDGE_ZONE) {
            toggleSidebar(true);
        } else if (dx < -SWIPE_THRESHOLD && sidebar.classList.contains('open')) {
            toggleSidebar(false);
        }
        swiping = false;
    }, { passive: true });
}

// ═══════════════════════════════════════════════════════════════════════════
// INIT
// ═══════════════════════════════════════════════════════════════════════════

document.addEventListener('DOMContentLoaded', () => {
    initParticles();
    initUploadZones();
    initDownloader();
    checkServerHealth();
    initMobileSwipe();
    initGlobalDrop();
    initInstallPrompt();

    // Show start/landing page on first load (overrides any cached panel state)
    switchTool('landing');

    // Keyboard shortcut: Ctrl+K to search
    document.addEventListener('keydown', (e) => {
        if ((e.ctrlKey || e.metaKey) && e.key === 'k') {
            e.preventDefault();
            const si = $('#sidebarSearch');
            if (si) { si.focus(); si.select(); }
            if (window.innerWidth <= 768 && !$('#sidebar').classList.contains('open')) toggleSidebar(true);
        }
        if (e.key === 'Escape') {
            closeQuickAction();
        }
    });

    // Initialize default output formats and presets
    document.querySelectorAll('.format-select-grid').forEach(grid => {
        const active = grid.querySelector('.fmt-btn.active');
        if (active && grid.dataset.tool) state.outputFormats[grid.dataset.tool] = active.dataset.fmt;
    });
    document.querySelectorAll('.preset-grid').forEach(grid => {
        const active = grid.querySelector('.preset-btn.active');
        if (active && grid.dataset.tool) state.presets[grid.dataset.tool] = active.dataset.val;
    });

});
