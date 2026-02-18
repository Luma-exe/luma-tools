// ═══════════════════════════════════════════════════════════════════════════
// FFMPEG.WASM — client-side processing
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
            this.ffmpeg.on('progress', ({ progress }) => { progressCb(Math.max(0, Math.min(1, progress))); });
        }

        const inputName = 'input' + getExtension(file.name);
        await this.ffmpeg.writeFile(inputName, await fetchFile(file));
        const fullArgs = args.map(a => a === '__INPUT__' ? inputName : a);
        await this.ffmpeg.exec(fullArgs);
        const data = await this.ffmpeg.readFile(outputName);
        await this.ffmpeg.deleteFile(inputName).catch(() => {});
        await this.ffmpeg.deleteFile(outputName).catch(() => {});

        if (progressCb) this.ffmpeg.off('progress');
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

        try { await processFileServer(toolId); return; }
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
    const blob = await WasmProcessor.process(file, cmd.args, cmd.output);
    blob._filename = file.name.replace(/\.[^.]+$/, '') + '_LumaTools.' + cmd.output.split('.').pop();
    return blob;
}
