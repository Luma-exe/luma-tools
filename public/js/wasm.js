// ═══════════════════════════════════════════════════════════════════════════
// BROWSER-SIDE PROCESSING
//
// Image tools: native Canvas API — zero dependencies, no special headers.
// Audio tools: ffmpeg.wasm — requires crossOriginIsolated (COOP/COEP headers).
// SVG / unsupported formats: always route to server with an explanatory badge.
// ═══════════════════════════════════════════════════════════════════════════

// ─── Discord reporters ───────────────────────────────────────────────────
// Log a successful browser-side tool use (Canvas / audio WASM).
async function reportBrowserToolUse(tool, filename) {
    try {
        await fetch('/api/browser-tool', {
            method:  'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ tool, filename: filename || 'unknown' }),
        });
    } catch (_) {}
}

// Log a browser-side processing failure to Discord for debugging.
async function reportWasmError(tool, errorMsg) {
    try {
        await fetch('/api/wasm/error', {
            method:  'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                tool,
                error:               String(errorMsg || 'unknown').slice(0, 300),
                crossOriginIsolated: !!crossOriginIsolated,
                sharedArrayBuffer:   typeof SharedArrayBuffer !== 'undefined',
                ua:                  navigator.userAgent.slice(0, 200),
            }),
        });
    } catch (_) {}
}

// ─── Helpers ──────────────────────────────────────────────────────────────
function getExtension(filename) {
    const dot = filename.lastIndexOf('.');
    return dot > 0 ? filename.slice(dot) : '';
}

function mimeFromExt(filename) {
    const ext = filename.split('.').pop().toLowerCase();
    return ({
        jpg:'image/jpeg', jpeg:'image/jpeg', png:'image/png',
        webp:'image/webp', bmp:'image/bmp', tiff:'image/tiff',
        ico:'image/x-icon', gif:'image/gif', avif:'image/avif',
        svg:'image/svg+xml',
        mp3:'audio/mpeg', wav:'audio/wav', flac:'audio/flac',
        ogg:'audio/ogg', aac:'audio/aac', m4a:'audio/mp4',
        mp4:'video/mp4', webm:'video/webm',
    })[ext] || 'application/octet-stream';
}

// Ext string for a blob (.jpg / .png / …)
function extFromBlob(blob) {
    const sub = blob.type.split('/')[1] || 'png';
    return '.' + (sub === 'jpeg' ? 'jpg' : sub);
}

// MIME types Canvas.toBlob can reliably produce
const CANVAS_MIMES = { png:'image/png', jpg:'image/jpeg', jpeg:'image/jpeg', webp:'image/webp' };

// Load a File/Blob → HTMLImageElement
function loadImageFile(file) {
    return new Promise((resolve, reject) => {
        const url = URL.createObjectURL(file);
        const img = new Image();
        img.onload  = () => { URL.revokeObjectURL(url); resolve(img); };
        img.onerror = () => { URL.revokeObjectURL(url); reject(new Error('Image failed to decode — format may be unsupported')); };
        img.src = url;
    });
}

// canvas → Promise<Blob>
function canvasToBlob(canvas, mime, quality) {
    return new Promise((resolve, reject) => {
        canvas.toBlob(b => {
            if (b) resolve(b);
            else reject(new Error(`canvas.toBlob returned null for type "${mime}" — format unsupported in this browser`));
        }, mime, quality);
    });
}

// ─── Canvas image processing ───────────────────────────────────────────────
// Returns a Blob, or null when the format/method requires the server instead.
async function processImageCanvas(toolId, file, opts) {
    // SVG cannot be reliably decoded by Canvas — always server
    if (/\.svg$/i.test(file.name) || file.type === 'image/svg+xml') return null;

    const img    = await loadImageFile(file);
    const canvas = document.createElement('canvas');
    const ctx    = canvas.getContext('2d');
    const srcW   = img.naturalWidth;
    const srcH   = img.naturalHeight;

    switch (toolId) {

        case 'image-compress': {
            // Always output JPEG (lossy compression)
            canvas.width = srcW; canvas.height = srcH;
            ctx.drawImage(img, 0, 0);
            const q = Math.max(0.05, Math.min(1, (opts.quality || 75) / 100));
            return canvasToBlob(canvas, 'image/jpeg', q);
        }

        case 'image-resize': {
            let tW = parseInt(opts.width)  || 0;
            let tH = parseInt(opts.height) || 0;
            if (!tW && !tH)  { tW = srcW; tH = srcH; }
            else if (!tW)    tW = Math.round(srcW * tH / srcH);
            else if (!tH)    tH = Math.round(srcH * tW / srcW);
            canvas.width = tW; canvas.height = tH;
            ctx.drawImage(img, 0, 0, tW, tH);
            const mime = CANVAS_MIMES[file.type?.split('/')[1]] || 'image/png';
            return canvasToBlob(canvas, mime, mime === 'image/jpeg' ? 0.92 : undefined);
        }

        case 'image-convert': {
            const fmt    = opts.format || 'png';
            const outMime = {
                png:'image/png', jpg:'image/jpeg', jpeg:'image/jpeg', webp:'image/webp',
            }[fmt];
            if (!outMime) return null; // avif/bmp/tiff/gif/ico → server handles these
            canvas.width = srcW; canvas.height = srcH;
            ctx.drawImage(img, 0, 0);
            return canvasToBlob(canvas, outMime, outMime === 'image/jpeg' ? 0.92 : undefined);
        }

        case 'image-crop': {
            const { x = 0, y = 0, w = srcW, h = srcH } = opts;
            const cW = Math.max(1, Math.round(w));
            const cH = Math.max(1, Math.round(h));
            canvas.width = cW; canvas.height = cH;
            ctx.drawImage(img, Math.round(x), Math.round(y), Math.round(w), Math.round(h), 0, 0, cW, cH);
            const cropMime = CANVAS_MIMES[file.type?.split('/')[1]] || 'image/png';
            return canvasToBlob(canvas, cropMime, cropMime === 'image/jpeg' ? 0.92 : undefined);
        }

        case 'image-bg-remove': {
            if (opts.method === 'auto') return null; // AI removal → server
            canvas.width = srcW; canvas.height = srcH;
            ctx.drawImage(img, 0, 0);
            const d       = ctx.getImageData(0, 0, srcW, srcH);
            const px      = d.data;
            const tgt     = opts.method === 'black' ? [0,0,0] : [255,255,255];
            const thresh  = 0.3;
            for (let i = 0; i < px.length; i += 4) {
                const dr = (px[i]   - tgt[0]) / 255;
                const dg = (px[i+1] - tgt[1]) / 255;
                const db = (px[i+2] - tgt[2]) / 255;
                if (Math.sqrt(dr*dr + dg*dg + db*db) < thresh) px[i+3] = 0;
            }
            ctx.putImageData(d, 0, 0);
            return canvasToBlob(canvas, 'image/png');
        }

        case 'metadata-strip': {
            // Re-encoding through Canvas strips all EXIF/XMP/IPTC metadata
            const inMime = file.type || mimeFromExt(file.name);
            if (!inMime.startsWith('image/') || inMime === 'image/svg+xml') return null;
            const stripMime = CANVAS_MIMES[inMime.split('/')[1]] || 'image/png';
            canvas.width = srcW; canvas.height = srcH;
            ctx.drawImage(img, 0, 0);
            return canvasToBlob(canvas, stripMime, stripMime === 'image/jpeg' ? 0.92 : undefined);
        }
    }
    return null;
}

// ─── ffmpeg.wasm (audio only) ──────────────────────────────────────────────
const WasmProcessor = {
    ffmpeg: null,
    loaded: false,
    loading: false,
    _loadPromise: null,

    async load() {
        if (this.loaded)   return;
        if (this.loading)  return this._loadPromise;
        this.loading = true;
        this._loadPromise = (async () => {
            try {
                console.log('[WASM] crossOriginIsolated =', crossOriginIsolated);
                console.log('[WASM] SharedArrayBuffer   =', typeof SharedArrayBuffer !== 'undefined');
                if (!crossOriginIsolated) {
                    const msg = 'crossOriginIsolated is false — COOP/COEP headers must be served';
                    await reportWasmError('wasm-load', msg);
                    throw new Error(msg);
                }
                const { FFmpeg }     = FFmpegWASM;
                const { toBlobURL }  = FFmpegUtil;
                this.ffmpeg = new FFmpeg();
                this.ffmpeg.on('log', ({ type, message }) => console.log(`[WASM:${type}]`, message));
                const BASE = 'https://cdn.jsdelivr.net/npm/@ffmpeg/core@0.12.6/dist/umd';
                console.log('[WASM] Fetching core files from CDN...');
                const coreURL = await toBlobURL(`${BASE}/ffmpeg-core.js`,   'text/javascript');
                const wasmURL = await toBlobURL(`${BASE}/ffmpeg-core.wasm`, 'application/wasm');
                console.log('[WASM] Calling ffmpeg.load()...');
                await this.ffmpeg.load({ coreURL, wasmURL });
                this.loaded = true;
                console.log('[WASM] Load succeeded');
            } catch (err) {
                this.loading      = false;
                this._loadPromise = null;
                throw err;
            } finally {
                this.loading = false;
            }
        })();
        return this._loadPromise;
    },

    async processAudio(file, args, outputName) {
        await this.load();
        const { fetchFile } = FFmpegUtil;
        const inputName = 'input' + getExtension(file.name);
        try {
            await this.ffmpeg.writeFile(inputName, await fetchFile(file));
            await this.ffmpeg.exec(args.map(a => a === '__INPUT__' ? inputName : a));
            const data = await this.ffmpeg.readFile(outputName);
            return new Blob([data.buffer], { type: mimeFromExt(outputName) });
        } finally {
            await this.ffmpeg.deleteFile(inputName).catch(() => {});
            await this.ffmpeg.deleteFile(outputName).catch(() => {});
        }
    },
};

// ─── Badge helpers ─────────────────────────────────────────────────────────

// Orange warning badge — used when audio WASM fails / is unavailable.
function _applyWasmFallbackBadge(toolId, reason) {
    const panel = document.getElementById('tool-' + toolId);
    const lb    = panel?.querySelector('.tool-location-badge');
    if (!lb) return;

    lb.className = 'tool-location-badge loc-server';
    lb.title     = 'Running on our server — browser WASM unavailable';
    lb.innerHTML = '<i class="fas fa-server"></i> On our server';

    panel.querySelector('.tool-wasm-warn-badge')?.remove();
    const warn = document.createElement('span');
    warn.className = 'tool-wasm-warn-badge';
    warn.title     = reason || 'Browser WASM unavailable — using server fallback';
    warn.innerHTML = '<i class="fas fa-triangle-exclamation"></i> Browser WASM failed';
    lb.insertAdjacentElement('afterend', warn);
}

// Reset a Canvas tool's location badge back to "In Browser" (e.g. after file removal).
function _resetCanvasBadge(toolId) {
    const panel = document.getElementById('tool-' + toolId);
    const lb    = panel?.querySelector('.tool-location-badge');
    if (!lb) return;
    // Only reset if we previously changed it — don't clobber server-only tools
    lb.className = 'tool-location-badge loc-browser';
    lb.title     = 'Processed entirely in your browser — no upload required';
    lb.innerHTML = '<i class="fas fa-microchip"></i> In browser';
    panel.querySelector('.tool-wasm-warn-badge')?.remove();
}

// Blue info badge — shown immediately when user picks an SVG file.
function _handleSvgServerNotice(toolId) {
    const panel = document.getElementById('tool-' + toolId);
    const lb    = panel?.querySelector('.tool-location-badge');
    if (!lb) return;

    lb.className = 'tool-location-badge loc-server';
    lb.title     = 'SVG files cannot be processed in the browser — will use our server';
    lb.innerHTML = '<i class="fas fa-server"></i> On our server';

    panel.querySelector('.tool-wasm-warn-badge')?.remove();
    const note = document.createElement('span');
    note.className = 'tool-wasm-warn-badge';
    note.style.cssText = 'background:var(--blue,#3b82f6);border-color:var(--blue-dark,#1d4ed8);color:#fff';
    note.title     = 'SVG processing requires the server — your file is processed then immediately deleted';
    note.innerHTML = '<i class="fas fa-info-circle"></i> SVG uses server';
    lb.insertAdjacentElement('afterend', note);
}

// ─── WASM_TOOLS (public API — checked by file-tools.js & batch.js) ─────────
// Each value is a function that: (a) proves the tool can run in the browser,
// (b) returns a truthy cmd-hint so callers know to invoke processFileWasm.
// Return null to let the caller fall straight through to the server.
const CANVAS_TOOLS     = new Set(['image-compress','image-resize','image-convert','image-crop','image-bg-remove','metadata-strip']);
const WASM_AUDIO_TOOLS = new Set(['audio-convert']);

const WASM_TOOLS = {
    'image-compress':  () => ({ canvas: true }),
    'image-resize':    () => ({ canvas: true }),
    'image-convert':   () => ({ canvas: true }),
    'image-crop':      () => ({ canvas: true }),
    'image-bg-remove': (_file, opts) => (opts?.method === 'auto' ? null : { canvas: true }),
    'metadata-strip':  () => ({ canvas: true }),
    'audio-convert':   (file) => (file.size > 50 * 1024 * 1024 ? null : { audio: true }),
    'favicon-generate': () => null, // always server
};

// ─── Option reader ──────────────────────────────────────────────────────────
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

// ─── Main entry (single file) ───────────────────────────────────────────────
async function processFileWasm(toolId) {
    const file = state.files[toolId];
    if (!file) { showToast('Please select a file first', 'error'); return; }
    showProcessing(toolId, true);
    try {

        // ── SVG ──────────────────────────────────────────────────────────────
        if (/\.svg$/i.test(file.name) || file.type === 'image/svg+xml') {
            _handleSvgServerNotice(toolId);
            await processFileServer(toolId);
            return;
        }

        const opts = getWasmOpts(toolId);

        // ── Canvas image tools ───────────────────────────────────────────────
        if (CANVAS_TOOLS.has(toolId)) {
            let blob = null;
            try {
                blob = await processImageCanvas(toolId, file, opts);
            } catch (canvasErr) {
                console.error('[Canvas]', toolId, canvasErr);
                await reportWasmError(toolId, canvasErr.message || String(canvasErr));
            }
            if (blob) {
                const stem   = file.name.replace(/\.[^.]+$/, '');
                const outExt = extFromBlob(blob);
                showResult(toolId, blob, stem + '>_LumaTools' + outExt);
                reportBrowserToolUse(toolId, file.name); // Discord log
                return;
            }
            // null = format/method not handled by Canvas → route to server silently
            await processFileServer(toolId);
            return;
        }

        // ── Audio (ffmpeg.wasm) ───────────────────────────────────────────────
        if (WASM_AUDIO_TOOLS.has(toolId)) {
            if (!crossOriginIsolated) {
                const msg = 'crossOriginIsolated=false — COOP/COEP headers required for audio WASM';
                console.warn('[WASM audio]', msg);
                await reportWasmError(toolId, msg);
                _applyWasmFallbackBadge(toolId, 'Audio WASM unavailable: page not cross-origin isolated');
                await processFileServer(toolId);
                return;
            }
            const fmt    = opts.format || 'mp3';
            const args   = ['-i', '__INPUT__', 'output.' + fmt];
            const output = 'output.' + fmt;
            showToast('Loading audio processing engine…', 'info');
            try {
                const blob   = await WasmProcessor.processAudio(file, args, output);
                const stem   = file.name.replace(/\.[^.]+$/, '');
                showResult(toolId, blob, stem + '>_LumaTools.' + fmt);
                reportBrowserToolUse(toolId, file.name); // Discord log
            } catch (wasmErr) {
                console.error('[WASM audio]', wasmErr);
                await reportWasmError(toolId, wasmErr.message || String(wasmErr));
                _applyWasmFallbackBadge(toolId, 'Audio WASM error — see Discord log');
                showToast('Browser processing failed, using server…', 'info');
                await processFileServer(toolId);
            }
            return;
        }

        await processFileServer(toolId);

    } catch (err) {
        showToast(err.message || 'Processing failed', 'error');
    } finally {
        showProcessing(toolId, false);
    }
}

// ─── Batch entry ────────────────────────────────────────────────────────────
async function processFileWasmDirect(toolId, file) {
    // SVG always goes to server
    if (/\.svg$/i.test(file.name) || file.type === 'image/svg+xml') {
        return processFileServerDirect(toolId, file);
    }

    const opts = getWasmOpts(toolId);

    if (CANVAS_TOOLS.has(toolId)) {
        try {
            const blob = await processImageCanvas(toolId, file, opts);
            if (blob) {
                blob._filename = file.name.replace(/\.[^.]+$/, '') + '>_LumaTools' + extFromBlob(blob);
                return blob;
            }
        } catch (err) {
            await reportWasmError(toolId, err.message || String(err));
        }
        return processFileServerDirect(toolId, file);
    }

    if (WASM_AUDIO_TOOLS.has(toolId) && crossOriginIsolated && file.size <= 50 * 1024 * 1024) {
        const fmt    = opts.format || 'mp3';
        const args   = ['-i', '__INPUT__', 'output.' + fmt];
        const output = 'output.' + fmt;
        try {
            const blob = await WasmProcessor.processAudio(file, args, output);
            blob._filename = file.name.replace(/\.[^.]+$/, '') + '>_LumaTools.' + fmt;
            return blob;
        } catch (err) {
            await reportWasmError(toolId, err.message || String(err));
        }
    }

    return processFileServerDirect(toolId, file);
}

// ─── switchTool hook ─────────────────────────────────────────────────────────
// Canvas tools never need COOP/COEP — only show the warning badge for audio tools.
(function () {
    const _orig = window.switchTool;
    if (typeof _orig !== 'function') return;

    window.switchTool = function (toolId) {
        _orig(toolId);
        if (WASM_AUDIO_TOOLS.has(toolId) && !crossOriginIsolated) {
            setTimeout(() => _applyWasmFallbackBadge(
                toolId,
                'Audio WASM unavailable \u2014 page not cross-origin isolated'
            ), 0);
        }
    };
}());
