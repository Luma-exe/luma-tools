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
    if (toolId === 'audio-trim') initWaveform('audio-trim', file);
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

    // If the user configured settings on a pending batch, launch it now
    if (state.pendingBatch[toolId] && state.pendingBatch[toolId].length > 1) {
        const batchFiles = state.pendingBatch[toolId];
        delete state.pendingBatch[toolId];
        batchQueue.start(toolId, batchFiles);
        return;
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
        case 'audio-trim':
            formData.append('start', $('audioTrimStart').value || '00:00:00');
            formData.append('end', $('audioTrimEnd').value || '');
            formData.append('mode', getSelectedPreset('audio-trim-mode') || 'fast');
            if (!$('audioTrimEnd').value) { showToast('Please enter an end time', 'error'); return; }
            break;
        case 'pdf-split':
            formData.append('from', $('pdfSplitFrom').value || '1');
            formData.append('to', $('pdfSplitTo').value || '');
            break;
        case 'image-watermark': {
            const text = $('wmText').value.trim();
            if (!text) { showToast('Please enter watermark text', 'error'); return; }
            formData.append('text', text);
            formData.append('fontsize', $('wmFontSize').value || '36');
            formData.append('opacity', $('wmOpacity').value || '0.6');
            formData.append('color', getSelectedPreset('wm-color') || 'white');
            const posBtn = document.querySelector('#wmPositionGrid .wm-pos-btn.active');
            formData.append('position', posBtn?.dataset.pos || 'bottom-right');
            break;
        }
        case 'markdown-to-pdf':
            // no extra params needed — file is the only input
            break;
        case 'csv-json': {
            const dirPill = document.querySelector('.preset-grid[data-tool="csv-json-direction"] .preset-btn.active');
            formData.append('direction', dirPill?.dataset.val || 'csv-to-json');
            break;
        }
        case 'ai-study-notes': {
            const fmtPill = document.querySelector('.preset-grid[data-tool="study-notes-format"] .preset-btn.active');
            formData.append('format', fmtPill?.dataset.val || 'markdown');
            break;
        }
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
        'video-to-gif','gif-to-video','video-remove-audio','video-speed','video-stabilize','audio-normalize',
        'audio-trim','ai-study-notes'];
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

    // Clean up any existing SSE connection for this tool
    if (state.jobPolls[toolId]) {
        try { state.jobPolls[toolId].close(); } catch (_) {}
        delete state.jobPolls[toolId];
    }

    const es = new EventSource(`/api/tools/progress/${jobId}`);
    state.jobPolls[toolId] = es;

    es.onmessage = async (evt) => {
        let data;
        try { data = JSON.parse(evt.data); } catch (_) { return; }

        if (data.status === 'completed') {
            es.close(); delete state.jobPolls[toolId];
            if (progressBar) progressBar.style.width = '100%';
            if (progressPct) progressPct.textContent = '100%';
            try {
                const fileRes = await fetch(`/api/tools/result/${jobId}`);
                if (!fileRes.ok) throw new Error('Failed to download result');
                const blob = await fileRes.blob();
                const filename = getFilenameFromResponse(fileRes) || 'processed_file';
                showResult(toolId, blob, filename, jobId);
            } catch (err) { showToast(err.message, 'error'); }
            showProcessing(toolId, false);
        } else if (data.status === 'error' || data.status === 'not_found' || data.status === 'timeout') {
            es.close(); delete state.jobPolls[toolId];
            showProcessing(toolId, false);
            showToast(data.error || 'Processing failed', 'error');
        } else {
            const pct = data.progress || 0;
            if (progressBar) progressBar.style.width = pct + '%';
            if (progressPct) progressPct.textContent = pct > 0 ? Math.round(pct) + '%' : '';
            if (procText && data.stage) procText.textContent = data.stage;
        }
    };

    es.onerror = () => {
        es.close(); delete state.jobPolls[toolId];
        showProcessing(toolId, false);
        showToast('Lost connection to server', 'error');
    };
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

function showResult(toolId, blob, filename, jobId = null) {
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
    result.querySelectorAll('.result-preview, .result-actions, .multi-result-list, .result-zip-btn, .notes-preview-pane').forEach(el => el.remove());

    // wrap download button + optional preview in a side-by-side container
    const actions = document.createElement('div');
    actions.className = 'result-actions';
    downloadLink.parentNode.insertBefore(actions, downloadLink);
    actions.appendChild(downloadLink);

    // Send-to-tool button
    const sendBtn = document.createElement('button');
    sendBtn.className = 'result-send-btn';
    sendBtn.innerHTML = '<i class="fas fa-share-square"></i> Send to Tool';
    sendBtn.addEventListener('click', () => {
        const fakeFile = new File([blob], filename, { type: blob.type || 'application/octet-stream' });
        const category = detectFileCategory(fakeFile);
        if (!category || !FILE_TOOL_MAP[category]) { showToast('No compatible tools for this file type', 'error'); return; }
        state.droppedFiles = [fakeFile];
        state.droppedCategory = category;
        showQuickActionModal(category, [fakeFile]);
    });
    actions.appendChild(sendBtn);

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
    } else if (toolId === 'ai-study-notes' && /\.(md|txt)$/i.test(filename)) {
        // Async: read blob text then build preview
        blob.text().then(text => {
            const isMarkdown = /\.md$/i.test(filename);
            const pane = document.createElement('div');
            pane.className = 'notes-preview-pane';

            // Helper: copy button
            const makeCopyBtn = (getText) => {
                const btn = document.createElement('button');
                btn.className = 'notes-toggle-btn notes-copy-btn';
                btn.innerHTML = '<i class="fas fa-copy"></i> Copy Text';
                btn.addEventListener('click', () => {
                    navigator.clipboard.writeText(getText()).then(() => {
                        btn.innerHTML = '<i class="fas fa-check"></i> Copied!';
                        setTimeout(() => { btn.innerHTML = '<i class="fas fa-copy"></i> Copy Text'; }, 2000);
                    }).catch(() => showToast('Copy failed', 'error'));
                });
                return btn;
            };

            // Helper: compare button (only if jobId available)
            const makeCompareBtn = (notesText) => {
                if (!jobId) return null;
                const btn = document.createElement('button');
                btn.className = 'notes-toggle-btn notes-compare-btn';
                btn.innerHTML = '<i class="fas fa-columns"></i> Compare';
                btn.addEventListener('click', async () => {
                    // Remove any existing compare view
                    pane.querySelectorAll('.notes-compare-split').forEach(el => el.remove());
                    const existingSplit = pane.querySelector('.notes-compare-split');
                    if (existingSplit) { existingSplit.remove(); return; }

                    btn.disabled = true;
                    btn.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Loading...';
                    try {
                        const resp = await fetch(`/api/tools/raw-text/${jobId}`);
                        if (!resp.ok) throw new Error('Source text not available');
                        const srcText = await resp.text();

                        const split = document.createElement('div');
                        split.className = 'notes-compare-split';

                        // Left: AI notes
                        const leftCol = document.createElement('div');
                        leftCol.className = 'notes-compare-col';
                        const leftHdr = document.createElement('div');
                        leftHdr.className = 'notes-compare-header';
                        leftHdr.innerHTML = '<i class="fas fa-robot"></i> AI Notes';
                        const leftBody = document.createElement('div');
                        leftBody.className = isMarkdown ? 'notes-rendered notes-compare-body' : 'notes-compare-body';
                        if (isMarkdown) leftBody.innerHTML = parseMarkdown(notesText);
                        else leftBody.textContent = notesText;
                        leftCol.appendChild(leftHdr);
                        leftCol.appendChild(leftBody);

                        // Right: source PDF text
                        const rightCol = document.createElement('div');
                        rightCol.className = 'notes-compare-col';
                        const rightHdr = document.createElement('div');
                        rightHdr.className = 'notes-compare-header';
                        rightHdr.innerHTML = '<i class="fas fa-file-pdf"></i> Source PDF Text';
                        const rightBody = document.createElement('pre');
                        rightBody.className = 'notes-raw notes-compare-body';
                        rightBody.textContent = srcText;
                        rightCol.appendChild(rightHdr);
                        rightCol.appendChild(rightBody);

                        split.appendChild(leftCol);
                        split.appendChild(rightCol);
                        pane.appendChild(split);

                        btn.innerHTML = '<i class="fas fa-times"></i> Close Compare';
                        btn.disabled = false;
                        btn.onclick = () => { split.remove(); btn.innerHTML = '<i class="fas fa-columns"></i> Compare'; btn.onclick = null; };
                    } catch (err) {
                        showToast(err.message, 'error');
                        btn.innerHTML = '<i class="fas fa-columns"></i> Compare';
                        btn.disabled = false;
                    }
                });
                return btn;
            };

            if (isMarkdown) {
                // Toggle bar with view buttons + copy + compare pushed right
                const toggleBar = document.createElement('div');
                toggleBar.className = 'notes-preview-toggle';
                toggleBar.innerHTML =
                    '<button class="notes-toggle-btn active" data-view="rendered"><i class="fas fa-eye"></i> Rendered</button>' +
                    '<button class="notes-toggle-btn" data-view="raw"><i class="fas fa-code"></i> Raw</button>';

                const spacer = document.createElement('span');
                spacer.style.marginLeft = 'auto';
                toggleBar.appendChild(spacer);

                const compareBtn = makeCompareBtn(text);
                if (compareBtn) toggleBar.appendChild(compareBtn);
                const copyBtn = makeCopyBtn(() => text);
                toggleBar.appendChild(copyBtn);
                pane.appendChild(toggleBar);

                const renderedEl = document.createElement('div');
                renderedEl.className = 'notes-rendered';
                renderedEl.innerHTML = parseMarkdown(text);

                const rawEl = document.createElement('pre');
                rawEl.className = 'notes-raw hidden';
                rawEl.textContent = text;

                pane.appendChild(renderedEl);
                pane.appendChild(rawEl);

                toggleBar.addEventListener('click', e => {
                    const btn = e.target.closest('.notes-toggle-btn[data-view]');
                    if (!btn) return;
                    toggleBar.querySelectorAll('.notes-toggle-btn[data-view]').forEach(b => b.classList.remove('active'));
                    btn.classList.add('active');
                    if (btn.dataset.view === 'rendered') {
                        renderedEl.classList.remove('hidden');
                        rawEl.classList.add('hidden');
                    } else {
                        renderedEl.classList.add('hidden');
                        rawEl.classList.remove('hidden');
                    }
                });
            } else {
                // Plain text — toolbar with compare + copy buttons
                const toggleBar = document.createElement('div');
                toggleBar.className = 'notes-preview-toggle';
                toggleBar.innerHTML = '<span class="notes-toggle-label"><i class="fas fa-align-left"></i> Plain Text</span>';

                const spacer = document.createElement('span');
                spacer.style.marginLeft = 'auto';
                toggleBar.appendChild(spacer);

                const compareBtn = makeCompareBtn(text);
                if (compareBtn) toggleBar.appendChild(compareBtn);
                const copyBtn = makeCopyBtn(() => text);
                toggleBar.appendChild(copyBtn);
                pane.appendChild(toggleBar);

                const rawEl = document.createElement('pre');
                rawEl.className = 'notes-raw';
                rawEl.textContent = text;
                pane.appendChild(rawEl);
            }

            result.appendChild(pane);
        });
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

    // ── right-side actions wrapper (ZIP button only) ────────────────
    const actions = document.createElement('div');
    actions.className = 'result-actions result-actions--multi';

    const zipBtn = document.createElement('button');
    zipBtn.className = 'result-zip-btn';
    zipBtn.innerHTML = '<i class="fas fa-file-zipper"></i> Download All as ZIP';
    zipBtn.addEventListener('click', () => downloadMultiAsZip(pages, toolId));
    actions.appendChild(zipBtn);

    result.appendChild(actions);

    // ── individual download links with inline thumbnail (full width below) ───
    result.classList.add('has-multi');
    const listEl = document.createElement('div');
    listEl.className = 'multi-result-list';
    pages.forEach(p => {
        const tagged = lumaTag(p.name);
        const row = document.createElement('a');
        row.href = escapeHTML(p.url);
        row.download = tagged;
        row.className = 'result-page-link';
        row.innerHTML = `<i class="fas fa-download"></i><span class="result-page-name">${escapeHTML(tagged)}</span>`;
        if (isImageSet) {
            const thumb = document.createElement('img');
            thumb.className = 'result-page-thumb';
            thumb.src = p.url;
            thumb.alt = '';
            row.appendChild(thumb);
        }
        listEl.appendChild(row);
    });
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
        // Load the first file into the tool so the user can configure settings,
        // then store the full list as a pending batch — it launches when they click Process.
        switchTool(toolId);
        setTimeout(() => {
            handleFileSelect(toolId, files[0]);
            state.pendingBatch[toolId] = files;
            showToast(`${files.length} files queued — configure settings then press the button to batch process`, 'info');
        }, 100);
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
