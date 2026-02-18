// ═══════════════════════════════════════════════════════════════════════════
// FRONTEND-ONLY TOOLS — QR Code, Base64, JSON Formatter, Color Converter,
//                       Markdown Preview, Hash Results
// ═══════════════════════════════════════════════════════════════════════════

// ── Hash results ─────────────────────────────────────────────────────────

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

// ── QR Code ───────────────────────────────────────────────────────────────

function generateQR() {
    const text = document.getElementById('qrInput').value.trim();
    if (!text) { showToast('Please enter some text', 'error'); return; }
    const size = parseInt(document.getElementById('qrSize').value) || 6;
    try {
        const qr = qrcode(0, 'M');
        qr.addData(text);
        qr.make();
        const cellSize = size, margin = 4;
        const moduleCount = qr.getModuleCount();
        const totalSize = moduleCount * cellSize + margin * 2;
        const wrap = document.getElementById('qrCanvasWrap');
        wrap.innerHTML = '';
        const canvas = document.createElement('canvas');
        canvas.width = totalSize; canvas.height = totalSize; canvas.id = 'qrCanvas';
        const ctx = canvas.getContext('2d');
        ctx.fillStyle = '#ffffff'; ctx.fillRect(0, 0, totalSize, totalSize);
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

// ── Base64 ────────────────────────────────────────────────────────────────

function processBase64() {
    const mode = getSelectedFmt('base64-mode') || 'encode';
    const input = document.getElementById('base64Input').value;
    if (!input) { showToast('Please enter some text', 'error'); return; }
    try {
        const result = mode === 'encode'
            ? btoa(unescape(encodeURIComponent(input)))
            : decodeURIComponent(escape(atob(input.trim())));
        document.getElementById('base64Output').value = result;
        document.getElementById('base64CopyBtn').classList.remove('hidden');
    } catch (e) { showToast('Conversion failed — invalid input', 'error'); }
}

// ── JSON Formatter ────────────────────────────────────────────────────────

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

// ── Color Converter ───────────────────────────────────────────────────────

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
        if (s === 0) { r = g = b = Math.round(l * 255); } else {
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
    if (max === min) { h = s = 0; } else {
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
        const sx = x / rect.width * canvas.width, sy = y / rect.height * canvas.height;
        const dx = sx - cx, dy = sy - cy;
        if (Math.sqrt(dx * dx + dy * dy) > radius) return;
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
        // Sync to main inputs tab
        document.getElementById('colorHex').value = hex;
        document.getElementById('colorRgb').value = `${r}, ${g}, ${b}`;
        document.getElementById('colorPreview').style.background = hex;
        const rr = r/255, gg = g/255, bb = b/255;
        const max = Math.max(rr, gg, bb), min = Math.min(rr, gg, bb);
        let hh, ss, ll = (max + min) / 2;
        if (max === min) { hh = ss = 0; } else {
            const d = max - min; ss = ll > 0.5 ? d / (2 - max - min) : d / (max + min);
            switch (max) { case rr: hh = ((gg - bb) / d + (gg < bb ? 6 : 0)) / 6; break; case gg: hh = ((bb - rr) / d + 2) / 6; break; case bb: hh = ((rr - gg) / d + 4) / 6; break; }
        }
        document.getElementById('colorHsl').value = `${Math.round(hh*360)}, ${Math.round(ss*100)}%, ${Math.round(ll*100)}%`;
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
            const dx = x - cx, dy = y - cy, dist = Math.sqrt(dx * dx + dy * dy);
            const i = (y * img.width + x) * 4;
            if (dist <= radius) {
                let angle = Math.atan2(dy, dx) / (2 * Math.PI);
                if (angle < 0) angle += 1;
                const sat = dist / radius;
                const h = angle, s = sat, l = brightness * 0.5;
                let r, g, b;
                if (s === 0) { r = g = b = l; } else {
                    const hue2rgb = (p, q, t) => { if (t < 0) t += 1; if (t > 1) t -= 1; if (t < 1/6) return p + (q-p)*6*t; if (t < 1/2) return q; if (t < 2/3) return p + (q-p)*(2/3-t)*6; return p; };
                    const q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
                    r = hue2rgb(p, q, h + 1/3); g = hue2rgb(p, q, h); b = hue2rgb(p, q, h - 1/3);
                }
                img.data[i] = Math.round(r * 255); img.data[i+1] = Math.round(g * 255); img.data[i+2] = Math.round(b * 255); img.data[i+3] = 255;
            } else { img.data[i+3] = 0; }
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

    // Compute color mathematically from cursor angle (hue) + distance (saturation) + new brightness.
    // This avoids pixel-sampling inaccuracies caused by CSS scaling vs canvas resolution.
    const cursor = document.getElementById('wheelCursor');
    if (!cursor) return;
    const cursorPctX = parseFloat(cursor.style.left) / 100;
    const cursorPctY = parseFloat(cursor.style.top) / 100;
    if (isNaN(cursorPctX) || isNaN(cursorPctY)) return;

    // Convert % position back to canvas-space coordinates
    const sx = cursorPctX * canvas.width;
    const sy = cursorPctY * canvas.height;
    const dx = sx - cx, dy = sy - cy;
    let angle = Math.atan2(dy, dx) / (2 * Math.PI);
    if (angle < 0) angle += 1;
    const sat = Math.min(Math.sqrt(dx * dx + dy * dy) / radius, 1);

    // HSL → RGB (same formula as drawWheel)
    const h = angle, s = sat, l = val * 0.5;
    let r, g, b;
    if (s === 0) {
        r = g = b = Math.round(l * 255);
    } else {
        const hue2rgb = (p, q, t) => { if (t < 0) t += 1; if (t > 1) t -= 1; if (t < 1/6) return p + (q-p)*6*t; if (t < 1/2) return q; if (t < 2/3) return p + (q-p)*(2/3-t)*6; return p; };
        const q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
        r = Math.round(hue2rgb(p, q, h + 1/3) * 255);
        g = Math.round(hue2rgb(p, q, h) * 255);
        b = Math.round(hue2rgb(p, q, h - 1/3) * 255);
    }
    r = Math.max(0, Math.min(255, r));
    g = Math.max(0, Math.min(255, g));
    b = Math.max(0, Math.min(255, b));

    const hex = '#' + [r, g, b].map(v => v.toString(16).padStart(2, '0')).join('');
    cursor.style.background = hex;
    const previewWheel = document.getElementById('colorPreviewWheel');
    if (previewWheel) previewWheel.style.background = hex;
    const hexWheel = document.getElementById('colorHexWheel');
    if (hexWheel) hexWheel.value = hex;
    const rgbWheel = document.getElementById('colorRgbWheel');
    if (rgbWheel) rgbWheel.value = `${r}, ${g}, ${b}`;
    // Sync to inputs tab
    const colorHex = document.getElementById('colorHex');
    if (colorHex) colorHex.value = hex;
    const colorRgb = document.getElementById('colorRgb');
    if (colorRgb) colorRgb.value = `${r}, ${g}, ${b}`;
    const colorPreview = document.getElementById('colorPreview');
    if (colorPreview) colorPreview.style.background = hex;
    const colorHsl = document.getElementById('colorHsl');
    if (colorHsl) colorHsl.value = `${Math.round(h*360)}, ${Math.round(s*100)}%, ${Math.round(l*100)}%`;
}

// ── Markdown Preview ──────────────────────────────────────────────────────

function renderMarkdownPreview() {
    const input = $('markdownInput')?.value || '';
    const output = $('markdownOutput');
    if (!output) return;
    if (!input.trim()) { output.innerHTML = '<p class="text-muted">Preview will appear here...</p>'; return; }
    output.innerHTML = parseMarkdown(input);
}

function parseMarkdown(md) {
    let html = md
        .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
        .replace(/```([\s\S]*?)```/g, (_, code) => '<pre><code>' + code.trim() + '</code></pre>')
        .replace(/`([^`]+)`/g, '<code>$1</code>')
        .replace(/^### (.+)$/gm, '<h3>$1</h3>')
        .replace(/^## (.+)$/gm, '<h2>$1</h2>')
        .replace(/^# (.+)$/gm, '<h1>$1</h1>')
        .replace(/^---$/gm, '<hr>')
        .replace(/\*\*\*(.+?)\*\*\*/g, '<strong><em>$1</em></strong>')
        .replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>')
        .replace(/\*(.+?)\*/g, '<em>$1</em>')
        .replace(/~~(.+?)~~/g, '<del>$1</del>')
        .replace(/!\[([^\]]*)\]\(([^)]+)\)/g, '<img src="$2" alt="$1">')
        .replace(/\[([^\]]+)\]\(([^)]+)\)/g, '<a href="$2" target="_blank" rel="noopener">$1</a>')
        .replace(/^&gt; (.+)$/gm, '<blockquote>$1</blockquote>')
        .replace(/^[*-] (.+)$/gm, '<li>$1</li>')
        .replace(/\n\n/g, '</p><p>')
        .replace(/\n/g, '<br>');
    html = html.replace(/(<li>.*?<\/li>)/gs, '<ul>$1</ul>');
    html = html.replace(/<\/ul>\s*<ul>/g, '');
    return '<p>' + html + '</p>';
}

function copyMarkdownHTML() {
    const output = $('markdownOutput');
    if (!output || !output.textContent.trim()) { showToast('Nothing to copy', 'error'); return; }
    navigator.clipboard.writeText(output.innerHTML).then(() => showToast('HTML copied to clipboard!', 'success'));
}

// ── Word Counter ──────────────────────────────────────────────────────────

function updateWordCount() {
    const text = ($('wcInput') || {}).value || '';
    const words = text.trim() === '' ? 0 : text.trim().split(/\s+/).length;
    const chars = text.length;
    const charsNoSpace = text.replace(/\s/g, '').length;
    const lines = text === '' ? 0 : text.split('\n').length;
    const sentences = text.trim() === '' ? 0 : (text.match(/[^.!?]*[.!?]+/g) || []).length;
    const paragraphs = text.trim() === '' ? 0 : text.split(/\n\s*\n/).filter(p => p.trim()).length || (text.trim() ? 1 : 0);

    // Average reading speed ~238 wpm, speaking ~130 wpm
    const readSecs = Math.round(words / 238 * 60);
    const speakSecs = Math.round(words / 130 * 60);

    function fmtTime(s) {
        if (s < 60) return s + ' sec';
        const m = Math.floor(s / 60), rs = s % 60;
        return rs > 0 ? `${m} min ${rs} sec` : `${m} min`;
    }

    const set = (id, val) => { const el = $(id); if (el) el.textContent = val; };
    set('wcWords', words.toLocaleString());
    set('wcChars', chars.toLocaleString());
    set('wcCharsNoSpace', charsNoSpace.toLocaleString());
    set('wcLines', lines.toLocaleString());
    set('wcSentences', sentences.toLocaleString());
    set('wcParagraphs', paragraphs.toLocaleString());
    set('wcReadTime', fmtTime(readSecs));
    set('wcSpeakTime', fmtTime(speakSecs));
}

// ── Diff Checker ──────────────────────────────────────────────────────────

let _diffView = 'split';

function setDiffView(view) {
    _diffView = view;
    document.getElementById('diffViewSplit')?.classList.toggle('active', view === 'split');
    document.getElementById('diffViewUnified')?.classList.toggle('active', view === 'unified');
    renderDiff();
}

function runDiff() {
    const a = ($('diffOriginal') || {}).value || '';
    const b = ($('diffChanged') || {}).value || '';
    if (!a && !b) {
        $('diffStats')?.classList.add('hidden');
        $('diffOutput')?.classList.add('hidden');
        return;
    }
    renderDiff();
}

function computeLineDiff(aLines, bLines) {
    // Myers-style LCS-based line diff
    const n = aLines.length, m = bLines.length;
    const max = n + m;
    const v = new Array(2 * max + 1).fill(0);
    const trace = [];

    for (let d = 0; d <= max; d++) {
        trace.push([...v]);
        for (let k = -d; k <= d; k += 2) {
            let x;
            if (k === -d || (k !== d && v[k - 1 + max] < v[k + 1 + max])) {
                x = v[k + 1 + max];
            } else {
                x = v[k - 1 + max] + 1;
            }
            let y = x - k;
            while (x < n && y < m && aLines[x] === bLines[y]) { x++; y++; }
            v[k + max] = x;
            if (x >= n && y >= m) {
                // backtrack
                const ops = [];
                let cx = x, cy = y;
                for (let dd = d; dd > 0; dd--) {
                    const tv = trace[dd];
                    let ck = cx - cy;
                    let prevK;
                    if (ck === -dd || (ck !== dd && tv[ck - 1 + max] < tv[ck + 1 + max])) {
                        prevK = ck + 1;
                    } else {
                        prevK = ck - 1;
                    }
                    const prevX = tv[prevK + max];
                    const prevY = prevX - prevK;
                    while (cx > prevX + (prevK === ck - 1 ? 1 : 0) && cy > prevY + (prevK === ck + 1 ? 1 : 0)) {
                        ops.push({ type: 'equal', a: cx - 1, b: cy - 1 }); cx--; cy--;
                    }
                    if (prevK === ck + 1) { ops.push({ type: 'insert', b: cy - 1 }); cy--; }
                    else { ops.push({ type: 'delete', a: cx - 1 }); cx--; }
                }
                while (cx > 0 && cy > 0) { ops.push({ type: 'equal', a: cx - 1, b: cy - 1 }); cx--; cy--; }
                return ops.reverse();
            }
        }
    }
    // Fallback: all changed
    const ops = [];
    aLines.forEach((_, i) => ops.push({ type: 'delete', a: i }));
    bLines.forEach((_, i) => ops.push({ type: 'insert', b: i }));
    return ops;
}

function esc(s) {
    return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

function renderDiff() {
    const aText = ($('diffOriginal') || {}).value || '';
    const bText = ($('diffChanged') || {}).value || '';
    const aLines = aText.split('\n');
    const bLines = bText.split('\n');

    const ops = computeLineDiff(aLines, bLines);

    let added = 0, removed = 0, unchanged = 0;
    ops.forEach(op => { if (op.type === 'insert') added++; else if (op.type === 'delete') removed++; else unchanged++; });

    const set = (id, val) => { const el = $(id); if (el) el.textContent = val; };
    set('diffAddedCount', added);
    set('diffRemovedCount', removed);
    set('diffUnchangedCount', unchanged);
    $('diffStats')?.classList.remove('hidden');
    $('diffOutput')?.classList.remove('hidden');

    const result = $('diffResult');
    if (!result) return;

    if (_diffView === 'unified') {
        let html = '<div class="diff-unified">';
        let lineNumA = 1, lineNumB = 1;
        ops.forEach(op => {
            if (op.type === 'equal') {
                html += `<div class="diff-line diff-equal"><span class="diff-ln">${lineNumA}</span><span class="diff-ln">${lineNumB}</span><span class="diff-sign"> </span><code>${esc(aLines[op.a])}</code></div>`;
                lineNumA++; lineNumB++;
            } else if (op.type === 'delete') {
                html += `<div class="diff-line diff-del"><span class="diff-ln">${lineNumA}</span><span class="diff-ln"></span><span class="diff-sign">−</span><code>${esc(aLines[op.a])}</code></div>`;
                lineNumA++;
            } else {
                html += `<div class="diff-line diff-ins"><span class="diff-ln"></span><span class="diff-ln">${lineNumB}</span><span class="diff-sign">+</span><code>${esc(bLines[op.b])}</code></div>`;
                lineNumB++;
            }
        });
        result.innerHTML = html + '</div>';
    } else {
        // Split view
        let leftHtml = '<div class="diff-split-pane">';
        let rightHtml = '<div class="diff-split-pane">';
        let lineNumA = 1, lineNumB = 1;
        ops.forEach(op => {
            if (op.type === 'equal') {
                const row = `<div class="diff-line diff-equal"><span class="diff-ln">${lineNumA++}</span><code>${esc(aLines[op.a])}</code></div>`;
                const rowB = `<div class="diff-line diff-equal"><span class="diff-ln">${lineNumB++}</span><code>${esc(bLines[op.b])}</code></div>`;
                leftHtml += row; rightHtml += rowB;
            } else if (op.type === 'delete') {
                leftHtml += `<div class="diff-line diff-del"><span class="diff-ln">${lineNumA++}</span><code>${esc(aLines[op.a])}</code></div>`;
                rightHtml += `<div class="diff-line diff-placeholder"></div>`;
            } else {
                leftHtml += `<div class="diff-line diff-placeholder"></div>`;
                rightHtml += `<div class="diff-line diff-ins"><span class="diff-ln">${lineNumB++}</span><code>${esc(bLines[op.b])}</code></div>`;
            }
        });
        result.innerHTML = `<div class="diff-split-wrap">${leftHtml}</div>${rightHtml}</div></div>`;
    }
}

