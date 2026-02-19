// ═══════════════════════════════════════════════════════════════════════════
// FFMPEG.WASM — client-side processing
// ═══════════════════════════════════════════════════════════════════════════

// Persists for the session — set true once WASM fails so subsequent tool opens
// immediately show the correct badge without waiting for a failed attempt.
window._wasmFailed = false;

const WasmProcessor = {
    ffmpeg: null,
    loaded: false,
    loading: false,
    _loadPromise: null,
    _queue: Promise.resolve(), // serialises all process() calls

    async load() {
        if (this.loaded) return;
        if (this.loading) return this._loadPromise;
        this.loading = true;
        this._loadPromise = (async () => {
            try {
                console.log('[WASM] crossOriginIsolated =', crossOriginIsolated);
                console.log('[WASM] SharedArrayBuffer available =', typeof SharedArrayBuffer !== 'undefined');
                showToast('Loading processing engine...', 'info');
                const { FFmpeg } = FFmpegWASM;
                const { toBlobURL } = FFmpegUtil;
                this.ffmpeg = new FFmpeg();
                this.ffmpeg.on('log', ({ type, message }) => console.log(`[WASM:${type}]`, message));
                // Must use toBlobURL for both core JS + WASM — when ffmpeg creates its
                // internal worker from a blob URL, relative paths inside the core script
                // would resolve to the page origin (not the CDN), causing the .wasm fetch
                // to 404. toBlobURL pre-fetches each file and re-hosts it as a blob so the
                // paths stay consistent.
                const BASE = 'https://cdn.jsdelivr.net/npm/@ffmpeg/core@0.12.6/dist/umd';
                console.log('[WASM] Fetching core JS from CDN...');
                const coreURL = await toBlobURL(`${BASE}/ffmpeg-core.js`, 'text/javascript');
                console.log('[WASM] Core JS blob ready:', coreURL.slice(0, 40));
                console.log('[WASM] Fetching core WASM from CDN...');
                const wasmURL = await toBlobURL(`${BASE}/ffmpeg-core.wasm`, 'application/wasm');
                console.log('[WASM] Core WASM blob ready:', wasmURL.slice(0, 40));
                console.log('[WASM] Calling ffmpeg.load()...');
                await this.ffmpeg.load({ coreURL, wasmURL });
                this.loaded = true;
                console.log('[WASM] Load succeeded!');
                showToast('Processing engine ready!', 'success');
            } catch (err) {
                console.error('[WASM] load() failed:', err);
                this.loading = false;
                this._loadPromise = null;
                throw err;
            } finally {
                this.loading = false;
            }
        })();
        return this._loadPromise;
    },

    process(file, args, outputName, progressCb) {
        // Serialise: each call waits for the previous one to finish.
        // The queue itself always resolves (never rejects) so a failed file
        // doesn't poison every file that follows it.
        const result = this._queue.then(() => this._processOne(file, args, outputName, progressCb));
        this._queue = result.catch(() => {}); // recover chain regardless of outcome
        return result;
    },

    async _processOne(file, args, outputName, progressCb) {
        await this.load();
        const { fetchFile } = FFmpegUtil;
        const inputName = 'input' + getExtension(file.name);
        const progressHandler = progressCb
            ? ({ progress }) => progressCb(Math.max(0, Math.min(1, progress)))
            : null;

        if (progressHandler) this.ffmpeg.on('progress', progressHandler);

        try {
            await this.ffmpeg.writeFile(inputName, await fetchFile(file));
            const fullArgs = args.map(a => a === '__INPUT__' ? inputName : a);
            await this.ffmpeg.exec(fullArgs);
            const data = await this.ffmpeg.readFile(outputName);
            return new Blob([data.buffer], { type: mimeFromExt(outputName) });
        } finally {
            if (progressHandler) this.ffmpeg.off('progress', progressHandler);
            await this.ffmpeg.deleteFile(inputName).catch(() => {});
            await this.ffmpeg.deleteFile(outputName).catch(() => {});
        }
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

// ─── Badge helper ────────────────────────────────────────────────────────────
// Updates the location badge to "On our server" and inserts the orange warning
// badge between the location badge and the fav button.
function _applyWasmFallbackBadge(toolId, reason) {
    const panel = document.getElementById('tool-' + toolId);
    const lb    = panel?.querySelector('.tool-location-badge');
    if (!lb) return;

    lb.className = 'tool-location-badge loc-server';
    lb.title     = 'Running on our server — browser WASM unavailable';
    lb.innerHTML = '<i class="fas fa-server"></i> On our server';

    // Remove any stale warning badge, then insert a fresh one
    panel.querySelector('.tool-wasm-warn-badge')?.remove();
    const warn = document.createElement('span');
    warn.className = 'tool-wasm-warn-badge';
    warn.title     = reason || (crossOriginIsolated
        ? 'Browser WASM processing failed — using server fallback'
        : 'Browser WASM unavailable: page is not cross-origin isolated');
    warn.innerHTML = '<i class="fas fa-triangle-exclamation"></i> Browser WASM failed';
    lb.insertAdjacentElement('afterend', warn);
}

// Tool command builders — return { args, output } or null (falls back to server)
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

        if (method === 'auto') return null; // AI removal → server
        const color = method === 'black' ? 'black' : 'white';
        return { args: ['-i', '__INPUT__', '-vf', `colorkey=${color}:0.3:0.15,format=rgba`, 'output.png'], output: 'output.png' };
    },
    'metadata-strip': (file) => {
        const ext = getExtension(file.name) || '.jpg';
        return { args: ['-i', '__INPUT__', '-map_metadata', '-1', '-c', 'copy', 'output' + ext], output: 'output' + ext };
    },
    'audio-convert': (file, opts) => {
        if (file.size > 50 * 1024 * 1024) return null; // >50 MB → server
        const fmt = opts.format || 'mp3';
        return { args: ['-i', '__INPUT__', 'output.' + fmt], output: 'output.' + fmt };
    },
    'favicon-generate': () => null, // multi-output → always server
};

function getWasmOpts(toolId) {
    switch (toolId) {
        case 'image-compress':  return { quality: parseInt($('imageCompressQuality')?.value || '75') };
        case 'image-resize':    return { width: $('resizeWidth')?.value || '', height: $('resizeHeight')?.value || '' };
        case 'image-convert':   return { format: getSelectedFmt('image-convert') || 'png' };
        case 'image-crop':      return state.cropRect || { x: 0, y: 0, w: 100, h: 100 };
        case 'image-bg-remove': return { method: getSelectedFmt('bg-remove-method') || 'auto' };
        case 'metadata-strip':  return {};
        case 'audio-convert':   return { format: getSelectedFmt('audio-convert') || 'mp3' };
        default:                return {};
    }
}

async function processFileWasm(toolId) {
    const file = state.files[toolId];

    if (!file) { showToast('Please select a file first', 'error'); return; }

    const opts = getWasmOpts(toolId);
    const builder = WASM_TOOLS[toolId];
    const cmd = builder ? builder(file, opts) : null;

    if (!cmd) return processFileServer(toolId);

    // If the page is not cross-origin isolated, SharedArrayBuffer is unavailable
    // and ffmpeg.wasm will fail immediately — skip straight to the server fallback.
    if (!crossOriginIsolated || window._wasmFailed) {
        const reason = !crossOriginIsolated
            ? 'Page is not cross-origin isolated — WASM requires COOP/COEP headers'
            : 'WASM failed earlier this session — using server fallback';
        console.warn('[WASM] Skipping WASM for', toolId, '—', reason);
        showProcessing(toolId, true);
        try {
            await processFileServer(toolId);
            window._wasmFailed = true;
            _applyWasmFallbackBadge(toolId,
                !crossOriginIsolated
                    ? 'Page is not cross-origin isolated — WASM requires COOP/COEP headers'
                    : 'WASM failed earlier this session — using server fallback');
        } catch (e) { showToast(e.message, 'error'); }
        finally    { showProcessing(toolId, false); }
        return;
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
        console.error('[WASM] Processing failed:', err);
        showToast('Client processing failed, trying server...', 'info');
        window._wasmFailed = true;
        try {
            await processFileServer(toolId);
            _applyWasmFallbackBadge(toolId, 'Browser WASM error: ' + (err?.message || 'unknown error'));
            return;
        }
        catch (serverErr) { showToast(serverErr.message, 'error'); }
    } finally {
        showProcessing(toolId, false);
    }
}

async function processFileWasmDirect(toolId, file) {
    const opts = getWasmOpts(toolId);
    const builder = WASM_TOOLS[toolId];
    const cmd = builder ? builder(file, opts) : null;

    if (!cmd) return processFileServerDirect(toolId, file);

    try {
        const blob = await WasmProcessor.process(file, cmd.args, cmd.output);
        blob._filename = file.name.replace(/\.[^.]+$/, '') + '_LumaTools.' + cmd.output.split('.').pop();
        return blob;
    } catch (wasmErr) {
        // WASM failed — fall back to the server exactly like the single-file path does
        window._wasmFailed = true;
        return processFileServerDirect(toolId, file);
    }
}

// ─── switchTool hook — apply fallback badge immediately on tool open ─────────
// wasm.js loads after ui.js, so switchTool is already defined. We wrap it so
// that whenever a WASM tool is opened and WASM is known to be unavailable (either
// because crossOriginIsolated is false or because it failed earlier this session),
// the badge shows "On our server" with the warning pill before the user even
// clicks the process button.
(function () {
    const _orig = window.switchTool;
    if (typeof _orig !== 'function') return;

    window.switchTool = function (toolId) {
        _orig(toolId);

        // Only decorate tools that have a WASM builder (not pure-server tools)
        if (!(toolId in WASM_TOOLS)) return;

        if (!crossOriginIsolated || window._wasmFailed) {
            // Defer by one tick so switchTool's DOM mutations are fully applied
            setTimeout(() => _applyWasmFallbackBadge(toolId,
                !crossOriginIsolated
                    ? 'Page is not cross-origin isolated \u2014 WASM requires COOP/COEP headers'
                    : 'WASM failed earlier this session \u2014 using server fallback'
            ), 0);
        }
    };
}());
