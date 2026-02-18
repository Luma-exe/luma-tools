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

        const overlay = $('batchOverlay');
        overlay.classList.remove('hidden');
        $('batchOvTitle').textContent = 'Batch Processing...';
        $('batchOvBar').style.width = '0%';
        $('batchOvTotal').textContent = this.total;
        $('batchOvCurrent').textContent = '0';
        $('batchOvDownloadAll').classList.add('hidden');
        $('batchOvNewBtn').classList.add('hidden');
        const spinner = overlay.querySelector('.progress-icon');

        if (spinner) spinner.classList.add('spinning');

        this.renderFileList();
        await this.processNext();
    },

    renderFileList() {
        const list = $('batchOvFileList');
        list.innerHTML = '';
        this.files.forEach((f, i) => {
            const entry = document.createElement('div');
            entry.className = 'batch-file-entry';
            entry.id = 'batchEntry-' + i;
            entry.innerHTML = `<span class="file-status-icon pending"><i class="fas fa-clock"></i></span>`
                + `<span class="batch-file-name" style="flex:1;margin-left:8px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">${escapeHTML(f.name)}</span>`
                + `<span style="font-size:0.72rem;color:var(--text-muted);font-family:var(--font-mono)">${formatBytes(f.size)}</span>`;
            list.appendChild(entry);
        });
    },

    updateEntry(index, status) {
        const entry = document.getElementById('batchEntry-' + index);

        if (!entry) return;
        const icon = entry.querySelector('.file-status-icon');
        const map = {
            done:   { cls: 'file-status-icon done',   html: '<i class="fas fa-check-circle"></i>' },
            error:  { cls: 'file-status-icon error',  html: '<i class="fas fa-times-circle"></i>' },
            active: { cls: 'file-status-icon active', html: '<i class="fas fa-circle-notch fa-spin"></i>' },
        };

        if (map[status]) { icon.className = map[status].cls; icon.innerHTML = map[status].html; }
    },

    async processNext() {
        if (this.current >= this.total) { this.finish(); return; }
        const file = this.files[this.current];
        this.updateEntry(this.current, 'active');
        $('batchOvStatus').textContent = `Processing ${this.current + 1} of ${this.total}: ${file.name}`;
        $('batchOvCurrent').textContent = this.current;

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
        $('batchOvBar').style.width = pct + '%';
        $('batchOvCurrent').textContent = this.current;
    },

    async finish() {
        this.running = false;
        const overlay = $('batchOverlay');
        const spinner = overlay.querySelector('.progress-icon');

        if (spinner) spinner.classList.remove('spinning');
        const successCount = this.results.filter(r => r.status === 'done').length;
        const failCount = this.results.filter(r => r.status === 'error').length;
        $('batchOvTitle').textContent = failCount === 0 ? 'Batch Complete!' : `${successCount} done, ${failCount} failed`;
        $('batchOvStatus').textContent = `Processed ${this.total} files`;
        $('batchOvBar').style.width = '100%';

        if (successCount > 0) $('batchOvDownloadAll').classList.remove('hidden');
        $('batchOvNewBtn').classList.remove('hidden');
    },

    reset() {
        this.files = []; this.toolId = null; this.results = [];
        this.current = 0; this.total = 0; this.running = false;
    },
};

async function processFileServerDirect(toolId, file) {
    const formData = new FormData();

    formData.append('file', file);

    switch (toolId) {
        case 'image-compress':   formData.append('quality', $('imageCompressQuality')?.value || '75'); break;
        case 'image-resize':     formData.append('width', $('resizeWidth')?.value || ''); formData.append('height', $('resizeHeight')?.value || ''); break;
        case 'image-convert':    formData.append('format', getSelectedFmt('image-convert') || 'png'); break;
        case 'video-compress':   formData.append('preset', getSelectedPreset('video-compress') || 'medium'); break;
        case 'video-convert':    formData.append('format', getSelectedFmt('video-convert') || 'mp4'); break;
        case 'audio-convert':    formData.append('format', getSelectedFmt('audio-convert') || 'mp3'); break;
        case 'pdf-compress':     formData.append('level', getSelectedPreset('pdf-compress') || 'ebook'); break;
        case 'image-bg-remove':  formData.append('method', getSelectedFmt('bg-remove-method') || 'auto'); break;
        default: break;
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

    if (contentType.includes('application/json')) throw new Error('Batch not supported for this tool');
    const blob = await res.blob();
    blob._filename = getFilenameFromResponse(res) || file.name;
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

    const btn = $('batchOvDownloadAll');
    btn.disabled = true;
    btn.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Creating zip...';

    try {
        const zip = new JSZip();

        for (const r of successResults) zip.file(r.name, r.blob);
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
    $('batchOverlay').classList.add('hidden');
    batchQueue.reset();
}
