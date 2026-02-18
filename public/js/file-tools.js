// ═══════════════════════════════════════════════════════════════════════════
// FILE UPLOAD HANDLING
// ═══════════════════════════════════════════════════════════════════════════

function initUploadZones() {
    document.querySelectorAll('.upload-zone').forEach(zone => {
        const input = zone.querySelector('.upload-input');
        const toolId = zone.id.replace('uz-', '');
        const isMulti = zone.classList.contains('multi');

        zone.addEventListener('dragover', e => { e.preventDefault(); zone.classList.add('dragover'); });
        zone.addEventListener('dragleave', () => zone.classList.remove('dragover'));
        zone.addEventListener('drop', e => {
            e.preventDefault(); zone.classList.remove('dragover');
            const files = [...e.dataTransfer.files];

            if (isMulti) handleMultiFiles(toolId, files);
            else if (files.length > 0) handleFileSelect(toolId, files[0]);
        });

        input.addEventListener('change', e => {
            if (isMulti) handleMultiFiles(toolId, [...e.target.files]);
            else if (e.target.files.length > 0) handleFileSelect(toolId, e.target.files[0]);
            e.target.value = '';
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

    const preview = document.querySelector(`.file-preview[data-tool="${toolId}"]`);

    if (preview) {
        preview.classList.remove('hidden');
        preview.querySelector('.file-name').textContent = file.name;
        preview.querySelector('.file-size').textContent = formatBytes(file.size);
    }

    const zone = document.getElementById('uz-' + toolId);

    if (zone) zone.classList.add('hidden');

    hideResult(toolId);

    if (toolId === 'image-crop') initCropCanvas(file);
    if (toolId === 'redact') initRedactCanvas(file);

    if (toolId === 'audio-convert') initWaveform('audio-convert', file);
    if (toolId === 'video-extract-audio') initWaveform('video-extract-audio', file);
    if (toolId === 'video-trim') initWaveform('video-trim', file);
}

function handleMultiFiles(toolId, files) {
    if (!state.multiFiles[toolId]) state.multiFiles[toolId] = [];
    for (const file of files) { state.multiFiles[toolId].push(file); }
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
// FILE PROCESSING — routes to server or WASM
// ═══════════════════════════════════════════════════════════════════════════

async function processFile(toolId) {
    if (toolId === 'pdf-merge' || toolId === 'images-to-pdf') {
        const files = state.multiFiles[toolId];

        if (toolId === 'pdf-merge' && (!files || files.length < 2)) { showToast('Please select at least 2 PDF files', 'error'); return; }
        if (toolId === 'images-to-pdf' && (!files || files.length < 1)) { showToast('Please select at least 1 image', 'error'); return; }
        return processMultiFile(toolId, files);
    }

    if (WASM_TOOLS[toolId]) {
        const file = state.files[toolId];

        if (!file) { showToast('Please select a file first', 'error'); return; }
        const cmd = WASM_TOOLS[toolId](file, getWasmOpts(toolId));

        if (cmd) return processFileWasm(toolId);
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

            formData.append('quality', $('imageCompressQuality').value);
            break;
        case 'image-resize':

            formData.append('width', $('resizeWidth').value || '');
            formData.append('height', $('resizeHeight').value || '');
            break;
        case 'image-convert':

            formData.append('format', getSelectedFmt('image-convert') || 'png');
            break;
        case 'video-compress':

            formData.append('preset', getSelectedPreset('video-compress') || 'medium');
            break;
        case 'video-trim':

            formData.append('start', $('trimStart').value || '00:00:00');
            formData.append('end', $('trimEnd').value || '');
            formData.append('mode', getSelectedPreset('video-trim-mode') || 'fast');

            if (!$('trimEnd').value) { showToast('Please enter an end time', 'error'); return; }
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
            formData.append('dpi', $('pdfDpi').value || '200');
            break;
        case 'video-to-gif':

            formData.append('fps', $('gifFps').value || '15');
            formData.append('width', $('gifWidth').value || '480');
            break;
        case 'video-speed':

            formData.append('speed', $('videoSpeed').value || '2');
            break;
        case 'video-frame':

            formData.append('timestamp', $('frameTimestamp').value || '00:00:00');
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
            if (file.type.startsWith('image/')) {
                const canvas = $('redactCanvas');

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

            if (redactRegions.length === 0) { showToast('Please draw at least one redaction region', 'error'); return; }
            formData.append('regions', JSON.stringify(redactRegions));
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
        const res = await fetch('/api/tools/' + toolId, { method: 'POST', body: formData });

        if (!res.ok) {
            const err = await res.json().catch(() => ({ error: 'Processing failed' }));
            throw new Error(err.error || 'Processing failed');
        }

        if (isAsync) {
            const data = await res.json();

            if (data.job_id) { pollJobStatus(toolId, data.job_id); }
            else { throw new Error('No job ID returned'); }
        } else {
            const contentType = res.headers.get('content-type') || '';

            if (contentType.includes('application/json')) {
                const data = await res.json();

                if (data.hashes) { showHashResults(toolId, data); }
                else if (data.pages && data.pages.length > 0) { showMultiResult(toolId, data.pages); }
                else { throw new Error('No output files'); }
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
                const pct = data.progress || 0;

                if (progressBar) progressBar.style.width = pct + '%';
                if (progressPct) progressPct.textContent = pct > 0 ? Math.round(pct) + '%' : '';
                if (procText && data.stage) procText.textContent = data.stage;
            }
        } catch (err) { /* keep polling */ }
    }, 1000);
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
    const panel = document.getElementById('tool-' + toolId);
    const btn = panel?.querySelector('.process-btn');

    if (btn) btn.disabled = show;
}

const IMAGE_EXTS = /\.(png|jpe?g|webp|gif|bmp|tiff?|ico|avif)$/i;
const VIDEO_EXTS = /\.(mp4|webm|mov|avi|mkv|ogv|ogg)$/i;

function showResult(toolId, blob, filename) {
    const result = document.querySelector(`.result-section[data-tool="${toolId}"]`);

    if (!result) return;
    result.classList.remove('hidden');
    result.classList.remove('has-multi');
    const tagged = lumaTag(filename);
    result.querySelector('.result-name').textContent = tagged;
    result.querySelector('.result-size').textContent = formatBytes(blob.size);
    const downloadLink = result.querySelector('.result-download');

    if (downloadLink._objectUrl) URL.revokeObjectURL(downloadLink._objectUrl);
    const objectUrl = URL.createObjectURL(blob);
    downloadLink._objectUrl = objectUrl;
    downloadLink.href = objectUrl;
    downloadLink.download = tagged;
    downloadLink.style.display = '';

    // clear any previous preview / multi list
    result.querySelectorAll('.result-preview, .result-actions, .multi-result-list, .result-zip-btn').forEach(el => el.remove());

    // wrap download button + optional preview in a side-by-side container
    const actions = document.createElement('div');
    actions.className = 'result-actions';
    downloadLink.parentNode.insertBefore(actions, downloadLink);
    actions.appendChild(downloadLink);

    if (IMAGE_EXTS.test(filename)) {
        const preview = document.createElement('img');
        preview.className = 'result-preview result-preview--image';
        preview.src = objectUrl;
        preview.alt = 'Preview';
        actions.appendChild(preview);
    } else if (VIDEO_EXTS.test(filename)) {
        const preview = document.createElement('video');
        preview.className = 'result-preview result-preview--video';
        preview.src = objectUrl;
        preview.preload = 'metadata';
        preview.muted = true;
        preview.playsInline = true;
        // seek to first frame so poster is visible
        preview.addEventListener('loadedmetadata', () => { preview.currentTime = 0.1; }, { once: true });
        actions.appendChild(preview);
    }
}

function showMultiResult(toolId, pages) {
    const result = document.querySelector(`.result-section[data-tool="${toolId}"]`);

    if (!result) return;
    result.classList.remove('hidden');
    result.classList.remove('has-multi');

    const isImageSet = pages.length > 0 && IMAGE_EXTS.test(pages[0].name);
    result.querySelector('.result-name').textContent = `${pages.length} file${pages.length > 1 ? 's' : ''} generated`;
    result.querySelector('.result-size').textContent = '';

    const downloadLink = result.querySelector('.result-download');
    downloadLink.style.display = 'none';

    // clear stale elements
    result.querySelectorAll('.result-preview, .result-actions, .multi-result-list, .result-zip-btn, .multi-thumb-strip').forEach(el => el.remove());

    // ── right-side actions wrapper (ZIP button + optional thumb strip) ───────
    const actions = document.createElement('div');
    actions.className = 'result-actions result-actions--multi';

    const zipBtn = document.createElement('button');
    zipBtn.className = 'result-zip-btn';
    zipBtn.innerHTML = '<i class="fas fa-file-zipper"></i> Download All as ZIP';
    zipBtn.addEventListener('click', () => downloadMultiAsZip(pages, toolId));
    actions.appendChild(zipBtn);

    if (isImageSet) {
        const strip = document.createElement('div');
        strip.className = 'multi-thumb-strip';
        pages.slice(0, 8).forEach(p => {
            const img = document.createElement('img');
            img.className = 'multi-thumb';
            img.src = p.url;
            img.alt = p.name;
            img.title = p.name;
            strip.appendChild(img);
        });

        if (pages.length > 8) {
            const more = document.createElement('div');
            more.className = 'multi-thumb-more';
            more.textContent = `+${pages.length - 8}`;
            strip.appendChild(more);
        }

        actions.appendChild(strip);
    }

    result.appendChild(actions);

    // ── individual download links (full width below) ─────────────────────────
    result.classList.add('has-multi');
    const listEl = document.createElement('div');
    listEl.className = 'multi-result-list';
    listEl.innerHTML = pages.map(p => {
        const tagged = lumaTag(p.name);
        return `<a href="${escapeHTML(p.url)}" download="${escapeHTML(tagged)}" class="result-page-link"><i class="fas fa-download"></i> ${escapeHTML(tagged)}</a>`;
    }).join('');
    result.appendChild(listEl);
}

async function downloadMultiAsZip(pages, toolId) {
    const btn = document.querySelector(`.result-section[data-tool="${toolId}"] .result-zip-btn`);

    if (btn) { btn.disabled = true; btn.innerHTML = '<i class="fas fa-circle-notch fa-spin"></i> Zipping…'; }

    try {
        const zip = new JSZip();
        await Promise.all(pages.map(async p => {
            const res = await fetch(p.url);
            const ab = await res.arrayBuffer();
            zip.file(lumaTag(p.name), ab);
        }));
        const blob = await zip.generateAsync({ type: 'blob', compression: 'DEFLATE', compressionOptions: { level: 6 } });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `${toolId}-output.zip`;
        a.click();
        setTimeout(() => URL.revokeObjectURL(url), 10000);
    } catch (e) {
        showToast('ZIP download failed: ' + e.message, 'error');
    } finally {
        if (btn) { btn.disabled = false; btn.innerHTML = '<i class="fas fa-file-zipper"></i> Download All as ZIP'; }
    }
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
    const overlay = $('dropOverlay');

    document.addEventListener('dragenter', (e) => {
        e.preventDefault();

        if (e.dataTransfer?.types?.includes('Files')) { _dragCounter++; overlay.classList.add('visible'); }
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

        if (!category) { showToast('Unsupported file type', 'error'); return; }
        state.droppedFiles = files;
        state.droppedCategory = category;
        showQuickActionModal(category, files);
    });
}

function showQuickActionModal(category, files) {
    const backdrop = $('quickActionBackdrop');
    const grid = $('qaGrid');
    const title = $('qaTitle');
    const fileInfo = $('qaFileInfo');
    const batchHint = $('qaBatchHint');
    const batchText = $('qaBatchText');
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
    $('quickActionBackdrop').classList.remove('visible');
    state.droppedFiles = [];
    state.droppedCategory = null;
}

function onQuickActionSelect(toolId) {
    const files = state.droppedFiles;
    $('quickActionBackdrop').classList.remove('visible');

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
