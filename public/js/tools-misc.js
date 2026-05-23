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

// ─ Colour math helpers ───────────────────────────────────────────────────

function _hue2rgb(p, q, t) {
    if (t < 0) t += 1; if (t > 1) t -= 1;
    if (t < 1/6) return p + (q - p) * 6 * t;
    if (t < 1/2) return q;
    if (t < 2/3) return p + (q - p) * (2/3 - t) * 6;
    return p;
}

function rgbToHsl(r, g, b) {
    const rr = r/255, gg = g/255, bb = b/255;
    const max = Math.max(rr,gg,bb), min = Math.min(rr,gg,bb);
    let h = 0, s = 0, l = (max + min) / 2;

    if (max !== min) {
        const d = max - min;
        s = l > 0.5 ? d / (2 - max - min) : d / (max + min);

        switch (max) {
            case rr: h = ((gg - bb) / d + (gg < bb ? 6 : 0)) / 6; break;
            case gg: h = ((bb - rr) / d + 2) / 6; break;
            case bb: h = ((rr - gg) / d + 4) / 6; break;
        }
    }

    return [Math.round(h*360), Math.round(s*100), Math.round(l*100)];
}

function hslToRgb(h, s, l) {
    h /= 360; s /= 100; l /= 100;

    if (s === 0) { const v = Math.round(l*255); return [v, v, v]; }
    const q = l < 0.5 ? l*(1+s) : l+s-l*s, p = 2*l - q;
    return [
        Math.round(_hue2rgb(p, q, h + 1/3) * 255),
        Math.round(_hue2rgb(p, q, h) * 255),
        Math.round(_hue2rgb(p, q, h - 1/3) * 255)
    ];
}

function rgbToHsv(r, g, b) {
    const rr = r/255, gg = g/255, bb = b/255;
    const max = Math.max(rr,gg,bb), min = Math.min(rr,gg,bb), d = max - min;
    let h = 0, s = max === 0 ? 0 : d / max, v = max;

    if (d !== 0) {
        switch (max) {
            case rr: h = ((gg - bb) / d + (gg < bb ? 6 : 0)) / 6; break;
            case gg: h = ((bb - rr) / d + 2) / 6; break;
            case bb: h = ((rr - gg) / d + 4) / 6; break;
        }
    }

    return [Math.round(h*360), Math.round(s*100), Math.round(v*100)];
}

function hsvToRgb(h, s, v) {
    h /= 360; s /= 100; v /= 100;
    let r, g, b;
    const i = Math.floor(h * 6), f = h * 6 - i;
    const p = v*(1-s), q = v*(1-f*s), t = v*(1-(1-f)*s);

    switch (i % 6) {
        case 0: r=v; g=t; b=p; break; case 1: r=q; g=v; b=p; break;
        case 2: r=p; g=v; b=t; break; case 3: r=p; g=q; b=v; break;
        case 4: r=t; g=p; b=v; break; case 5: r=v; g=p; b=q; break;
        default: r=g=b=0;
    }

    return [Math.round(r*255), Math.round(g*255), Math.round(b*255)];
}

function rgbToCmyk(r, g, b) {
    const rr = r/255, gg = g/255, bb = b/255;
    const k = 1 - Math.max(rr, gg, bb);

    if (k === 1) return [0, 0, 0, 100];
    return [
        Math.round((1 - rr - k) / (1 - k) * 100),
        Math.round((1 - gg - k) / (1 - k) * 100),
        Math.round((1 - bb - k) / (1 - k) * 100),
        Math.round(k * 100)
    ];
}

function cmykToRgb(c, m, y, k) {
    c /= 100; m /= 100; y /= 100; k /= 100;
    return [
        Math.round(255 * (1 - c) * (1 - k)),
        Math.round(255 * (1 - m) * (1 - k)),
        Math.round(255 * (1 - y) * (1 - k))
    ];
}

// ─ Sync all fields from r,g,b  ────────────────────────────────────────────

function syncAllColorFields(r, g, b, skipSource) {
    r = Math.max(0, Math.min(255, r));
    g = Math.max(0, Math.min(255, g));
    b = Math.max(0, Math.min(255, b));
    const hex = '#' + [r,g,b].map(v => v.toString(16).padStart(2,'0')).join('');
    const [hl, sl, ll] = rgbToHsl(r, g, b);
    const [hv, sv, vv] = rgbToHsv(r, g, b);
    const [c, m, y, k] = rgbToCmyk(r, g, b);

    const setVal = (id, v) => { const el = $(id); if (el) el.value = v; };
    const setBg  = (id)  => { const el = $(id); if (el) el.style.background = hex; };

    // Values tab
    if (skipSource !== 'hex')  setVal('colorHex',  hex);
    if (skipSource !== 'rgb')  setVal('colorRgb',  `${r}, ${g}, ${b}`);
    if (skipSource !== 'hsl')  setVal('colorHsl',  `${hl}, ${sl}%, ${ll}%`);
    if (skipSource !== 'hsv')  setVal('colorHsv',  `${hv}, ${sv}%, ${vv}%`);
    if (skipSource !== 'cmyk') setVal('colorCmyk', `${c}%, ${m}%, ${y}%, ${k}%`);
    setBg('colorPreview');

    // Picker tab outputs
    setVal('colorHexPicker',  hex);
    setVal('colorRgbPicker',  `${r}, ${g}, ${b}`);
    setVal('colorHslPicker',  `${hl}, ${sl}%, ${ll}%`);
    setVal('colorHsvPicker',  `${hv}, ${sv}%, ${vv}%`);
    setVal('colorCmykPicker', `${c}%, ${m}%, ${y}%, ${k}%`);
    setBg('colorPreviewPicker');
}

// ─ Parse & dispatch from any input field ─────────────────────────────────

function updateColorFrom(source) {
    let r, g, b;

    try {
        if (source === 'hex') {
            const hex = $('colorHex').value.trim();
            const m = hex.match(/^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i);

            if (!m) return;
            r = parseInt(m[1],16); g = parseInt(m[2],16); b = parseInt(m[3],16);
        } else if (source === 'rgb') {
            const pts = $('colorRgb').value.split(',').map(s => parseInt(s.trim()));

            if (pts.length < 3 || pts.some(isNaN)) return;
            [r, g, b] = pts;
        } else if (source === 'hsl') {
            const pts = $('colorHsl').value.replace(/%/g,'').split(',').map(s => parseFloat(s.trim()));

            if (pts.length < 3 || pts.some(isNaN)) return;
            [r, g, b] = hslToRgb(pts[0], pts[1], pts[2]);
        } else if (source === 'hsv') {
            const pts = $('colorHsv').value.replace(/%/g,'').split(',').map(s => parseFloat(s.trim()));

            if (pts.length < 3 || pts.some(isNaN)) return;
            [r, g, b] = hsvToRgb(pts[0], pts[1], pts[2]);
        } else if (source === 'cmyk') {
            const pts = $('colorCmyk').value.replace(/%/g,'').split(',').map(s => parseFloat(s.trim()));

            if (pts.length < 4 || pts.some(isNaN)) return;
            [r, g, b] = cmykToRgb(pts[0], pts[1], pts[2], pts[3]);
        }
    } catch { return; }
    syncAllColorFields(r, g, b, source);
}

function copyColorValue(format, fromPicker) {
    const id = fromPicker
        ? { hex:'colorHexPicker', rgb:'colorRgbPicker', hsl:'colorHslPicker', hsv:'colorHsvPicker', cmyk:'colorCmykPicker' }[format]
        : { hex:'colorHex', rgb:'colorRgb', hsl:'colorHsl', hsv:'colorHsv', cmyk:'colorCmyk' }[format];
    const el = $(id);

    if (!el) return;
    navigator.clipboard.writeText(el.value).then(() => showToast('Copied!', 'success'));
}

// ─ Tab switching ─────────────────────────────────────────────────────────

function switchColorTab(tab) {
    $$('.color-tab').forEach((t, i) => t.classList.toggle('active', (tab === 'inputs' ? i === 0 : i === 1)));
    $('colorTabInputs')?.classList.toggle('active', tab === 'inputs');
    $('colorTabPicker')?.classList.toggle('active', tab === 'picker');

    if (tab === 'picker') initPickerTab();
}

// ─ Picker type toggle ────────────────────────────────────────────────────

let _pickerType = 'wheel';   // 'wheel' | 'rect'
let _pickerInited = false;

function setPickerType(type) {
    _pickerType = type;
    $('pickerBtnWheel')?.classList.toggle('active', type === 'wheel');
    $('pickerBtnRect')?.classList.toggle('active', type === 'rect');
    $('pickerWheelSection')?.classList.toggle('hidden', type !== 'wheel');
    $('pickerRectSection')?.classList.toggle('hidden', type !== 'rect');

    if (type === 'wheel') initColorWheel();
    else initRectPicker();
}

function initPickerTab() {
    if (!_pickerInited) { _pickerInited = true; initColorWheel(); }
}

// ─ Wheel picker ──────────────────────────────────────────────────────────

let wheelInited = false;
function initColorWheel() {
    if (wheelInited) return;
    wheelInited = true;
    const canvas = $('colorWheel');

    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const cx = canvas.width / 2, cy = canvas.height / 2, radius = cx - 4;
    drawWheel(ctx, cx, cy, radius, 1.0);

    let dragging = false;
    function pickWheel(e) {
        const rect = canvas.getBoundingClientRect();
        const clientX = e.touches ? e.touches[0].clientX : e.clientX;
        const clientY = e.touches ? e.touches[0].clientY : e.clientY;
        let x = clientX - rect.left, y = clientY - rect.top;
        let sx = x / rect.width * canvas.width, sy = y / rect.height * canvas.height;
        const dx = sx - cx, dy = sy - cy;
        const dist = Math.sqrt(dx*dx + dy*dy);
        // Clamp to circle edge so dragging outside still tracks the nearest hue/sat
        if (dist > radius) {
            const angle = Math.atan2(dy, dx);
            sx = cx + Math.cos(angle) * radius;
            sy = cy + Math.sin(angle) * radius;
            x = sx / canvas.width * rect.width;
            y = sy / canvas.height * rect.height;
        }

        const cursor = $('wheelCursor');

        if (cursor) { cursor.style.left = (x/rect.width*100)+'%'; cursor.style.top = (y/rect.height*100)+'%'; }
        updatePickerFromWheelCoords(sx, sy, cx, cy, radius, parseInt($('wheelBrightness')?.value ?? 100) / 100);
    }

    canvas.addEventListener('mousedown', e => { dragging = true; pickWheel(e); });
    window.addEventListener('mousemove', e => { if (dragging) pickWheel(e); });
    window.addEventListener('mouseup', () => { dragging = false; });
    canvas.addEventListener('touchstart', e => { e.preventDefault(); dragging = true; pickWheel(e); }, { passive: false });
    window.addEventListener('touchmove',  e => { e.preventDefault(); if (dragging) pickWheel(e); }, { passive: false });
    canvas.addEventListener('touchend', () => { dragging = false; });
}

function updatePickerFromWheelCoords(sx, sy, cx, cy, radius, brightness) {
    const dx = sx - cx, dy = sy - cy;
    let angle = Math.atan2(dy, dx) / (2 * Math.PI);

    if (angle < 0) angle += 1;
    const sat = Math.min(Math.sqrt(dx*dx + dy*dy) / radius, 1);
    const h = angle, s = sat, l = brightness * 0.5;
    let [r, g, b] = hslToRgb(h * 360, s * 100, l * 100);
    const cursor = $('wheelCursor');

    if (cursor) cursor.style.background = '#' + [r,g,b].map(v=>v.toString(16).padStart(2,'0')).join('');
    syncAllColorFields(r, g, b, null);
}

function drawWheel(ctx, cx, cy, radius, brightness) {
    const img = ctx.createImageData(ctx.canvas.width, ctx.canvas.height);

    for (let y = 0; y < img.height; y++) {
        for (let x = 0; x < img.width; x++) {
            const dx = x-cx, dy = y-cy, dist = Math.sqrt(dx*dx+dy*dy);
            const i = (y * img.width + x) * 4;

            if (dist <= radius) {
                let angle = Math.atan2(dy, dx) / (2*Math.PI);

                if (angle < 0) angle += 1;
                const sat = dist / radius;
                const l = brightness * 0.5;
                const [r, g, b] = hslToRgb(angle*360, sat*100, l*100);
                img.data[i]=r; img.data[i+1]=g; img.data[i+2]=b; img.data[i+3]=255;
            } else { img.data[i+3]=0; }
        }
    }

    ctx.putImageData(img, 0, 0);
}

function updateWheelBrightness() {
    const val = parseInt($('wheelBrightness').value) / 100;
    const canvas = $('colorWheel');

    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const cx = canvas.width/2, cy = canvas.height/2, radius = cx - 4;
    drawWheel(ctx, cx, cy, radius, val);
    const cursor = $('wheelCursor');

    if (!cursor) return;
    const cursorPctX = parseFloat(cursor.style.left) / 100;
    const cursorPctY = parseFloat(cursor.style.top) / 100;

    if (isNaN(cursorPctX) || isNaN(cursorPctY)) return;
    updatePickerFromWheelCoords(cursorPctX * canvas.width, cursorPctY * canvas.height, cx, cy, radius, val);
}

// ─ Rectangle (SV square + hue strip) picker ──────────────────────────────

let rectInited = false;
let _rectHue = 240; // current hue for the SV square

function initRectPicker() {
    if (rectInited) return;
    rectInited = true;
    drawHueStrip();
    drawSVRect(_rectHue);
    initHueStripEvents();
    initSVRectEvents();
}

function drawHueStrip() {
    const canvas = $('colorHueStrip');

    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const grad = ctx.createLinearGradient(0, 0, canvas.width, 0);

    for (let i = 0; i <= 360; i += 30) grad.addColorStop(i/360, `hsl(${i},100%,50%)`);
    ctx.fillStyle = grad;
    ctx.fillRect(0, 0, canvas.width, canvas.height);
}

function drawSVRect(hue) {
    const canvas = $('colorRectSV');

    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const w = canvas.width, h = canvas.height;
    // Base: solid hue
    const base = ctx.createLinearGradient(0, 0, w, 0);
    base.addColorStop(0, '#fff');
    base.addColorStop(1, `hsl(${hue},100%,50%)`);
    ctx.fillStyle = base;
    ctx.fillRect(0, 0, w, h);
    // Overlay: black → transparent (top to bottom)
    const overlay = ctx.createLinearGradient(0, 0, 0, h);
    overlay.addColorStop(0, 'rgba(0,0,0,0)');
    overlay.addColorStop(1, '#000');
    ctx.fillStyle = overlay;
    ctx.fillRect(0, 0, w, h);
}

function initHueStripEvents() {
    const canvas = $('colorHueStrip');

    if (!canvas) return;
    let dragging = false;
    function pick(e) {
        const rect = canvas.getBoundingClientRect();
        const clientX = e.touches ? e.touches[0].clientX : e.clientX;
        const x = Math.max(0, Math.min(rect.width, clientX - rect.left));
        const pct = x / rect.width;
        _rectHue = Math.round(pct * 360);
        // Move hue cursor
        const cur = $('hueCursor');

        if (cur) cur.style.left = (pct * 100) + '%';
        drawSVRect(_rectHue);
        pickSVRectFromCursor();
    }

    canvas.addEventListener('mousedown', e => { dragging = true; pick(e); });
    canvas.addEventListener('mousemove', e => { if (dragging) pick(e); });
    window.addEventListener('mouseup', () => { dragging = false; });
    canvas.addEventListener('touchstart', e => { e.preventDefault(); dragging = true; pick(e); }, { passive: false });
    canvas.addEventListener('touchmove',  e => { e.preventDefault(); if (dragging) pick(e); }, { passive: false });
    canvas.addEventListener('touchend', () => { dragging = false; });
}

function initSVRectEvents() {
    const canvas = $('colorRectSV');

    if (!canvas) return;
    let dragging = false;
    function pick(e) {
        const rect = canvas.getBoundingClientRect();
        const clientX = e.touches ? e.touches[0].clientX : e.clientX;
        const clientY = e.touches ? e.touches[0].clientY : e.clientY;
        const x = Math.max(0, Math.min(rect.width,  clientX - rect.left));
        const y = Math.max(0, Math.min(rect.height, clientY - rect.top));
        const cur = $('rectCursor');

        if (cur) { cur.style.left = (x/rect.width*100)+'%'; cur.style.top = (y/rect.height*100)+'%'; }
        const sat = x / rect.width * 100;
        const val = (1 - y / rect.height) * 100;
        const [r, g, b] = hsvToRgb(_rectHue, sat, val);

        if (cur) cur.style.background = '#' + [r,g,b].map(v=>v.toString(16).padStart(2,'0')).join('');
        syncAllColorFields(r, g, b, null);
    }

    canvas.addEventListener('mousedown', e => { dragging = true; pick(e); });
    window.addEventListener('mousemove', e => { if (dragging) pick(e); });
    window.addEventListener('mouseup', () => { dragging = false; });
    canvas.addEventListener('touchstart', e => { e.preventDefault(); dragging = true; pick(e); }, { passive: false });
    window.addEventListener('touchmove',  e => { e.preventDefault(); if (dragging) pick(e); }, { passive: false });
    canvas.addEventListener('touchend', () => { dragging = false; });
}

function pickSVRectFromCursor() {
    const cur = $('rectCursor');
    const canvas = $('colorRectSV');

    if (!cur || !canvas) return;
    const pctX = parseFloat(cur.style.left) / 100 || 0;
    const pctY = parseFloat(cur.style.top) / 100 || 0;
    const sat = pctX * 100, val = (1 - pctY) * 100;
    const [r, g, b] = hsvToRgb(_rectHue, sat, val);
    cur.style.background = '#' + [r,g,b].map(v=>v.toString(16).padStart(2,'0')).join('');
    syncAllColorFields(r, g, b, null);
}

// ── Markdown Preview ──────────────────────────────────────────────────────

function renderMarkdownPreview() {
    const input = $('markdownInput')?.value || '';
    const output = $('markdownOutput');

    if (!output) return;
    if (!input.trim()) { output.innerHTML = '<p class="text-muted">Preview will appear here...</p>'; return; }
    output.innerHTML = parseMarkdown(input);
    if (typeof renderMathInEl === 'function') renderMathInEl(output);
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

// ══════════════════════════════════════════════════════════════════════════
// OCR RESULT
// ══════════════════════════════════════════════════════════════════════════

function showOCRResult(data) {
    const result = document.getElementById('ocrResult');
    const output = document.getElementById('ocrOutput');
    if (!result || !output) return;
    const text = data.text || '';
    output.value = text;
    const words = text.trim() ? text.trim().split(/\s+/).length : 0;
    document.getElementById('ocrCharCount').textContent = text.length.toLocaleString();
    document.getElementById('ocrWordCount').textContent = words.toLocaleString();
    result.classList.remove('hidden');
}

function copyOCRText() {
    const text = document.getElementById('ocrOutput')?.value || '';
    if (!text) return;
    navigator.clipboard.writeText(text).then(() => showToast('Text copied!', 'success'));
}

function downloadOCRText() {
    const text = document.getElementById('ocrOutput')?.value || '';
    if (!text) { showToast('No text to download', 'error'); return; }
    const blob = new Blob([text], { type: 'text/plain' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'extracted_text.txt';
    a.click();
    URL.revokeObjectURL(a.href);
}

// ══════════════════════════════════════════════════════════════════════════
// PASSWORD GENERATOR
// ══════════════════════════════════════════════════════════════════════════

const WORDLIST = [
    'apple','brave','crane','diver','eagle','flame','grape','hotel','igloo','joker',
    'kite','lemon','mango','noble','olive','pizza','queen','river','storm','tiger',
    'ultra','vivid','water','xenon','yacht','zebra','amber','blend','cubic','dusty',
    'ember','frost','glass','haste','ivory','jelly','knack','lunar','maple','nerve',
    'ozone','pearl','quart','ridge','solar','trend','unity','vapor','whale','xylem',
    'yearn','zonal','abbey','birch','cedar','delta','elite','flint','guava','hyper',
    'index','joist','karma','laser','merit','nexus','orbit','proxy','quota','relay',
    'scout','token','urban','venom','wrist','xerox','yield','zingy','atlas','blaze',
    'chess','drain','envy','flair','gloom','hippo','input','jumbo','kneel','lance',
    'marsh','nylon','optic','plumb','quest','roost','swift','twine','umbra','vista',
    'woven','oxide','yucca','zesty','acorn','biome','clone','drove','epoch','fjord',
    'grove','helix','indie','joint','kazoo','lyric','mocha','notch','opera','pixel'
];

let _pwMode = 'password';

function togglePWMode(mode) {
    _pwMode = mode;
    document.getElementById('pw-password-opts').classList.toggle('hidden', mode !== 'password');
    document.getElementById('pw-passphrase-opts').classList.toggle('hidden', mode !== 'passphrase');
    generatePassword();
}

function generatePassword() {
    const output = document.getElementById('pwOutput');
    if (!output) return;

    let pw = '';
    if (_pwMode === 'passphrase') {
        const count = parseInt(document.getElementById('ppWords')?.value || 4);
        const sep = document.querySelector('.format-select-grid[data-tool="pp-sep"] .fmt-btn.active')?.dataset?.fmt ?? '-';
        const words = [];
        for (let i = 0; i < count; i++) {
            words.push(WORDLIST[Math.floor(Math.random() * WORDLIST.length)]);
        }
        pw = words.join(sep);
    } else {
        const len = parseInt(document.getElementById('pwLength')?.value || 20);
        const useUpper   = document.getElementById('pwUpper')?.checked ?? true;
        const useLower   = document.getElementById('pwLower')?.checked ?? true;
        const useNumbers = document.getElementById('pwNumbers')?.checked ?? true;
        const useSymbols = document.getElementById('pwSymbols')?.checked ?? false;
        const noAmbig    = document.getElementById('pwNoAmbig')?.checked ?? false;

        let charset = '';
        if (useUpper)   charset += noAmbig ? 'ABCDEFGHJKLMNPQRSTUVWXYZ' : 'ABCDEFGHIJKLMNOPQRSTUVWXYZ';
        if (useLower)   charset += noAmbig ? 'abcdefghjkmnpqrstuvwxyz'  : 'abcdefghijklmnopqrstuvwxyz';
        if (useNumbers) charset += noAmbig ? '23456789' : '0123456789';
        if (useSymbols) charset += '!@#$%^&*()-_=+[]{}|;:,.<>?';
        if (!charset)   charset = 'abcdefghijklmnopqrstuvwxyz';

        const arr = new Uint32Array(len);
        crypto.getRandomValues(arr);
        pw = Array.from(arr).map(v => charset[v % charset.length]).join('');
    }

    output.value = pw;
    document.getElementById('pwMultiList')?.classList.add('hidden');
    updatePasswordStrength(pw);
}

function generatePasswordBatch() {
    const list = document.getElementById('pwMultiList');
    if (!list) return;
    list.innerHTML = '';
    for (let i = 0; i < 10; i++) {
        generatePassword();
        const pw = document.getElementById('pwOutput')?.value || '';
        const row = document.createElement('div');
        row.className = 'pw-batch-row';
        row.innerHTML = `<code>${pw}</code><button class="btn-icon" onclick="navigator.clipboard.writeText('${pw.replace(/'/g,"\\'")}').then(()=>showToast('Copied!','success'))" title="Copy"><i class="fas fa-copy"></i></button>`;
        list.appendChild(row);
    }
    generatePassword();
    list.classList.remove('hidden');
}

function updatePasswordStrength(pw) {
    const fill = document.getElementById('pwStrengthFill');
    const label = document.getElementById('pwStrengthLabel');
    if (!fill || !label) return;
    let score = 0;
    if (pw.length >= 12) score++;
    if (pw.length >= 20) score++;
    if (/[A-Z]/.test(pw)) score++;
    if (/[a-z]/.test(pw)) score++;
    if (/[0-9]/.test(pw)) score++;
    if (/[^A-Za-z0-9]/.test(pw)) score++;
    const levels = ['','Weak','Fair','Good','Strong','Very Strong','Excellent'];
    const colors = ['','#ef4444','#f97316','#eab308','#22c55e','#10b981','#6366f1'];
    const idx = Math.min(score, 6);
    fill.style.width = (idx / 6 * 100) + '%';
    fill.style.background = colors[idx];
    label.textContent = levels[idx] || '';
    label.style.color = colors[idx];
}

function copyPassword() {
    const pw = document.getElementById('pwOutput')?.value || '';
    if (!pw) return;
    navigator.clipboard.writeText(pw).then(() => showToast('Password copied!', 'success'));
}

// Auto-generate on page load when tool is active
document.getElementById('tool-password-gen')?.addEventListener('transitionend', () => {
    if (document.getElementById('tool-password-gen').classList.contains('active')) generatePassword();
});

// ══════════════════════════════════════════════════════════════════════════
// JWT DECODER
// ══════════════════════════════════════════════════════════════════════════

function decodeJWT() {
    const input = (document.getElementById('jwtInput')?.value || '').trim();
    const result  = document.getElementById('jwtResult');
    const errBox  = document.getElementById('jwtError');
    const errText = document.getElementById('jwtErrorText');
    const warn    = document.getElementById('jwtWarning');
    const warnTxt = document.getElementById('jwtWarningText');

    if (!input) {
        result?.classList.add('hidden');
        errBox?.classList.add('hidden');
        return;
    }

    const parts = input.split('.');
    if (parts.length !== 3) {
        result?.classList.add('hidden');
        errBox?.classList.remove('hidden');
        if (errText) errText.textContent = 'Invalid JWT — must have exactly 3 parts separated by dots.';
        return;
    }

    const decode = (b64url) => {
        try {
            const b64 = b64url.replace(/-/g, '+').replace(/_/g, '/');
            const padded = b64 + '=='.slice((b64.length + 3) % 4);
            return JSON.parse(atob(padded));
        } catch { return null; }
    };

    const header  = decode(parts[0]);
    const payload = decode(parts[1]);

    if (!header || !payload) {
        result?.classList.add('hidden');
        errBox?.classList.remove('hidden');
        if (errText) errText.textContent = 'Failed to decode JWT — the token may be malformed or corrupted.';
        return;
    }

    errBox?.classList.add('hidden');
    result?.classList.remove('hidden');

    document.getElementById('jwtHeader').textContent  = JSON.stringify(header, null, 2);
    document.getElementById('jwtPayload').textContent = JSON.stringify(payload, null, 2);
    document.getElementById('jwtSig').textContent     = parts[2];

    // Build meta info
    const meta = document.getElementById('jwtMeta');
    if (meta) {
        const rows = [];
        if (header.alg) rows.push(`<span><strong>Algorithm:</strong> ${header.alg}</span>`);
        if (header.typ) rows.push(`<span><strong>Type:</strong> ${header.typ}</span>`);
        if (payload.iss) rows.push(`<span><strong>Issuer:</strong> ${payload.iss}</span>`);
        if (payload.sub) rows.push(`<span><strong>Subject:</strong> ${payload.sub}</span>`);
        if (payload.aud) rows.push(`<span><strong>Audience:</strong> ${Array.isArray(payload.aud) ? payload.aud.join(', ') : payload.aud}</span>`);
        if (payload.iat) rows.push(`<span><strong>Issued at:</strong> ${new Date(payload.iat * 1000).toLocaleString()}</span>`);
        if (payload.exp) rows.push(`<span><strong>Expires:</strong> ${new Date(payload.exp * 1000).toLocaleString()}</span>`);
        meta.innerHTML = rows.join('');
    }

    // Expiry warning
    if (warn && warnTxt) {
        if (payload.exp) {
            const now = Math.floor(Date.now() / 1000);
            if (payload.exp < now) {
                const ago = Math.round((now - payload.exp) / 60);
                warnTxt.textContent = `This token expired ${ago} minute${ago !== 1 ? 's' : ''} ago.`;
                warn.classList.remove('hidden');
            } else {
                warn.classList.add('hidden');
            }
        } else {
            warnTxt.textContent = 'No expiration (exp) claim — this token never expires.';
            warn.classList.remove('hidden');
        }
    }
}

function copyJWTPart(part) {
    const el = document.getElementById(part === 'header' ? 'jwtHeader' : 'jwtPayload');
    if (!el) return;
    navigator.clipboard.writeText(el.textContent).then(() => showToast('Copied!', 'success'));
}

// ═══════════════════════════════════════════════════════════════════════════
// BULK PROGRAM INSTALLER
// ═══════════════════════════════════════════════════════════════════════════

const BULK_PROGRAMS = [
    // Browsers
    { id:'chrome',       name:'Google Chrome',       cat:'browsers',  icon:'fab fa-chrome',        winget:'Google.Chrome' },
    { id:'firefox',      name:'Mozilla Firefox',     cat:'browsers',  icon:'fab fa-firefox',       winget:'Mozilla.Firefox' },
    { id:'brave',        name:'Brave Browser',       cat:'browsers',  icon:'fab fa-brave',         winget:'Brave.Brave' },
    { id:'edge',         name:'Microsoft Edge',      cat:'browsers',  icon:'fab fa-edge',          winget:'Microsoft.Edge' },
    { id:'opera',        name:'Opera Browser',       cat:'browsers',  icon:'fab fa-opera',         winget:'Opera.Opera' },
    { id:'operagx',      name:'Opera GX',            cat:'browsers',  icon:'fab fa-opera',         winget:'Opera.OperaGX' },
    { id:'vivaldi',      name:'Vivaldi',             cat:'browsers',  icon:'fas fa-globe',         winget:'VivaldiTechnologies.Vivaldi' },
    { id:'tor',          name:'Tor Browser',         cat:'browsers',  icon:'fas fa-user-secret',   winget:'TorProject.TorBrowser' },
    // Messaging
    { id:'discord',      name:'Discord',             cat:'messaging', icon:'fab fa-discord',       winget:'Discord.Discord' },
    { id:'slack',        name:'Slack',               cat:'messaging', icon:'fab fa-slack',         winget:'SlackTechnologies.Slack' },
    { id:'telegram',     name:'Telegram',            cat:'messaging', icon:'fab fa-telegram',      winget:'Telegram.TelegramDesktop' },
    { id:'whatsapp',     name:'WhatsApp',            cat:'messaging', icon:'fab fa-whatsapp',      winget:'WhatsApp.WhatsApp' },
    { id:'signal',       name:'Signal',              cat:'messaging', icon:'fas fa-comment-dots',  winget:'OpenWhisperSystems.Signal' },
    { id:'teams',        name:'Microsoft Teams',     cat:'messaging', icon:'fas fa-users',         winget:'Microsoft.Teams' },
    { id:'zoom',         name:'Zoom',                cat:'messaging', icon:'fas fa-video',         winget:'Zoom.Zoom' },
    // Media
    { id:'vlc',          name:'VLC Media Player',    cat:'media',     icon:'fas fa-play-circle',   winget:'VideoLAN.VLC' },
    { id:'spotify',      name:'Spotify',             cat:'media',     icon:'fab fa-spotify',       winget:'Spotify.Spotify' },
    { id:'obs',          name:'OBS Studio',          cat:'media',     icon:'fas fa-circle',        winget:'OBSProject.OBSStudio' },
    { id:'audacity',     name:'Audacity',            cat:'media',     icon:'fas fa-headphones',    winget:'Audacity.Audacity' },
    { id:'handbrake',    name:'HandBrake',           cat:'media',     icon:'fas fa-film',          winget:'HandBrake.HandBrake' },
    { id:'gimp',         name:'GIMP',                cat:'media',     icon:'fas fa-paint-brush',   winget:'GIMP.GIMP' },
    { id:'inkscape',     name:'Inkscape',            cat:'media',     icon:'fas fa-pen-nib',       winget:'Inkscape.Inkscape' },
    { id:'itunes',       name:'iTunes',              cat:'media',     icon:'fab fa-itunes',        winget:'Apple.iTunes' },
    { id:'imageglass',   name:'ImageGlass',          cat:'media',     icon:'fas fa-image',         winget:'DuongDieuPhap.ImageGlass' },
    // Developer
    { id:'vscode',       name:'VS Code',             cat:'dev',       icon:'fas fa-code',          winget:'Microsoft.VisualStudioCode' },
    { id:'git',          name:'Git',                 cat:'dev',       icon:'fab fa-git-alt',       winget:'Git.Git' },
    { id:'nodejs',       name:'Node.js (LTS)',       cat:'dev',       icon:'fab fa-node-js',       winget:'OpenJS.NodeJS.LTS' },
    { id:'python',       name:'Python 3',            cat:'dev',       icon:'fab fa-python',        winget:'Python.Python.3.12' },
    { id:'docker',       name:'Docker Desktop',      cat:'dev',       icon:'fab fa-docker',        winget:'Docker.DockerDesktop' },
    { id:'postman',      name:'Postman',             cat:'dev',       icon:'fas fa-paper-plane',   winget:'Postman.Postman' },
    { id:'wt',           name:'Windows Terminal',    cat:'dev',       icon:'fas fa-terminal',      winget:'Microsoft.WindowsTerminal' },
    { id:'notepadpp',    name:'Notepad++',           cat:'dev',       icon:'fas fa-file-code',     winget:'Notepad++.Notepad++' },
    { id:'sublimetext',  name:'Sublime Text',        cat:'dev',       icon:'fas fa-feather-alt',   winget:'SublimeHQ.SublimeText.4' },
    { id:'filezilla',    name:'FileZilla',           cat:'dev',       icon:'fas fa-server',        winget:'TimKosse.FileZilla.Client' },
    { id:'putty',        name:'PuTTY',               cat:'dev',       icon:'fas fa-plug',          winget:'PuTTY.PuTTY' },
    { id:'cursor',       name:'Cursor',              cat:'dev',       icon:'fas fa-i-cursor',      winget:'Anysphere.Cursor' },
    // Gaming
    { id:'steam',        name:'Steam',               cat:'gaming',    icon:'fab fa-steam',         winget:'Valve.Steam' },
    { id:'epicgames',    name:'Epic Games Launcher', cat:'gaming',    icon:'fas fa-gamepad',       winget:'EpicGames.EpicGamesLauncher' },
    { id:'ea',           name:'EA App',              cat:'gaming',    icon:'fas fa-futbol',        winget:'ElectronicArts.EADesktop' },
    { id:'gog',          name:'GOG Galaxy',          cat:'gaming',    icon:'fas fa-rocket',        winget:'GOG.Galaxy' },
    { id:'battle',       name:'Battle.net',          cat:'gaming',    icon:'fas fa-dragon',        winget:'Blizzard.BattleNet' },
    // Utilities
    { id:'7zip',         name:'7-Zip',               cat:'utilities', icon:'fas fa-file-archive',  winget:'7zip.7zip' },
    { id:'qbittorrent',  name:'qBittorrent',         cat:'utilities', icon:'fas fa-magnet',        winget:'qBittorrent.qBittorrent' },
    { id:'sharex',       name:'ShareX',              cat:'utilities', icon:'fas fa-camera',        winget:'ShareX.ShareX' },
    { id:'everything',   name:'Everything',          cat:'utilities', icon:'fas fa-search',        winget:'voidtools.Everything' },
    { id:'powertoys',    name:'PowerToys',           cat:'utilities', icon:'fas fa-bolt',          winget:'Microsoft.PowerToys' },
    { id:'winrar',       name:'WinRAR',              cat:'utilities', icon:'fas fa-box-open',      winget:'RARLab.WinRAR' },
    { id:'bleachbit',    name:'BleachBit',           cat:'utilities', icon:'fas fa-broom',         winget:'BleachBit.BleachBit' },
    { id:'cpuz',         name:'CPU-Z',               cat:'utilities', icon:'fas fa-microchip',     winget:'CPUID.CPU-Z' },
    { id:'hwinfo',       name:'HWiNFO',              cat:'utilities', icon:'fas fa-thermometer',   winget:'REALiX.HWiNFO' },
    { id:'treesizefree', name:'TreeSize Free',       cat:'utilities', icon:'fas fa-tree',          winget:'JAMSoftware.TreeSize.Free' },
    // Security
    { id:'bitwarden',    name:'Bitwarden',           cat:'security',  icon:'fas fa-shield-alt',    winget:'Bitwarden.Bitwarden' },
    { id:'keepassxc',    name:'KeePassXC',           cat:'security',  icon:'fas fa-key',           winget:'KeePassXCTeam.KeePassXC' },
    { id:'malwarebytes', name:'Malwarebytes',        cat:'security',  icon:'fas fa-bug',           winget:'Malwarebytes.Malwarebytes' },
    { id:'nordvpn',      name:'NordVPN',             cat:'security',  icon:'fas fa-lock',          winget:'NordSecurity.NordVPN' },
    { id:'protonvpn',    name:'ProtonVPN',           cat:'security',  icon:'fas fa-user-shield',   winget:'ProtonTechnologies.ProtonVPN' },
    // Documents
    { id:'libreoffice',  name:'LibreOffice',         cat:'documents', icon:'fas fa-file-alt',      winget:'TheDocumentFoundation.LibreOffice' },
    { id:'obsidian',     name:'Obsidian',            cat:'documents', icon:'fas fa-gem',           winget:'Obsidian.Obsidian' },
    { id:'notion',       name:'Notion',              cat:'documents', icon:'fas fa-sticky-note',   winget:'Notion.Notion' },
    { id:'adobereader',  name:'Adobe Acrobat Reader',cat:'documents', icon:'fas fa-file-pdf',      winget:'Adobe.Acrobat.Reader.64-bit' },
    { id:'sumatrapdf',   name:'SumatraPDF',          cat:'documents', icon:'fas fa-book-open',     winget:'SumatraPDF.SumatraPDF' },
    // Runtimes
    { id:'dotnet8',      name:'.NET 8 Runtime',      cat:'runtimes',  icon:'fas fa-cogs',          winget:'Microsoft.DotNet.Runtime.8' },
    { id:'vcredist',     name:'VC++ Redistributable',cat:'runtimes',  icon:'fas fa-wrench',        winget:'Microsoft.VCRedist.2015+.x64' },
    { id:'directx',      name:'DirectX Runtime',     cat:'runtimes',  icon:'fas fa-cube',          winget:'Microsoft.DirectX' },
    { id:'java',         name:'Java (Adoptium 21)',  cat:'runtimes',  icon:'fab fa-java',          winget:'EclipseAdoptium.Temurin.21.JRE' },
];

let bulkSelected = new Set();

function renderBulkGrid(filter = 'all', search = '') {
    const grid = document.getElementById('bulkInstallGrid');
    if (!grid) return;
    const s = search.toLowerCase();
    const progs = BULK_PROGRAMS.filter(p =>
        (filter === 'all' || p.cat === filter) &&
        (!s || p.name.toLowerCase().includes(s) || p.cat.includes(s))
    );
    grid.innerHTML = progs.map(p => `
        <div class="bulk-prog-card ${bulkSelected.has(p.id) ? 'selected' : ''}" data-id="${p.id}" onclick="toggleBulkProg('${p.id}')">
            <div class="bulk-prog-check"><i class="fas ${bulkSelected.has(p.id) ? 'fa-check-circle' : 'fa-circle'}"></i></div>
            <i class="${p.icon} bulk-prog-icon"></i>
            <span class="bulk-prog-name">${p.name}</span>
        </div>
    `).join('');
}

function toggleBulkProg(id) {
    if (bulkSelected.has(id)) bulkSelected.delete(id);
    else bulkSelected.add(id);
    renderBulkGrid(
        document.querySelector('.bulk-cat-btn.active')?.dataset.cat || 'all',
        document.getElementById('bulkInstallSearch')?.value || ''
    );
    document.getElementById('bulkSelectedCount').textContent = bulkSelected.size;
}

function filterBulkCategory(cat) {
    document.querySelectorAll('.bulk-cat-btn').forEach(b => b.classList.toggle('active', b.dataset.cat === cat));
    renderBulkGrid(cat, document.getElementById('bulkInstallSearch')?.value || '');
}

function filterBulkPrograms() {
    const cat = document.querySelector('.bulk-cat-btn.active')?.dataset.cat || 'all';
    renderBulkGrid(cat, document.getElementById('bulkInstallSearch')?.value || '');
}

function clearBulkSelection() {
    bulkSelected.clear();
    document.getElementById('bulkSelectedCount').textContent = '0';
    renderBulkGrid(
        document.querySelector('.bulk-cat-btn.active')?.dataset.cat || 'all',
        document.getElementById('bulkInstallSearch')?.value || ''
    );
}

function generateBulkInstallScript() {
    if (bulkSelected.size === 0) { showToast('Select at least one program', 'error'); return; }
    const selected = BULK_PROGRAMS.filter(p => bulkSelected.has(p.id));
    const lines = [
        '# Luma Tools — Bulk Program Installer',
        '# Generated: ' + new Date().toISOString().slice(0, 19).replace('T', ' '),
        '# Programs: ' + selected.map(p => p.name).join(', '),
        '#',
        '# This script uses winget (Windows Package Manager) to install programs.',
        '# Run this script in PowerShell as Administrator.',
        '',
        '$ErrorActionPreference = "Continue"',
        '',
        '# Check for winget',
        'if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {',
        '    Write-Host "ERROR: winget is not installed. Please install App Installer from the Microsoft Store." -ForegroundColor Red',
        '    Read-Host "Press Enter to exit"',
        '    exit 1',
        '}',
        '',
        'Write-Host ""',
        'Write-Host "========================================" -ForegroundColor Cyan',
        'Write-Host "  Luma Tools - Bulk Program Installer" -ForegroundColor Cyan',
        'Write-Host "  Installing ' + selected.length + ' program(s)..." -ForegroundColor Cyan',
        'Write-Host "========================================" -ForegroundColor Cyan',
        'Write-Host ""',
        '',
        '$total = ' + selected.length,
        '$current = 0',
        '$failed = @()',
        '',
    ];
    for (const p of selected) {
        lines.push(`$current++`);
        lines.push(`Write-Host "[$current/$total] Installing ${p.name}..." -ForegroundColor Yellow`);
        lines.push(`winget install --id ${p.winget} --accept-source-agreements --accept-package-agreements -e -h`);
        lines.push(`if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335189) { $failed += "${p.name}" }`);
        lines.push(`Write-Host ""`);
    }
    lines.push('Write-Host "========================================" -ForegroundColor Cyan');
    lines.push('if ($failed.Count -eq 0) {');
    lines.push('    Write-Host "  All ' + selected.length + ' programs installed successfully!" -ForegroundColor Green');
    lines.push('} else {');
    lines.push('    Write-Host "  Completed with $($failed.Count) failure(s):" -ForegroundColor Yellow');
    lines.push('    $failed | ForEach-Object { Write-Host "    - $_" -ForegroundColor Red }');
    lines.push('}');
    lines.push('Write-Host "========================================" -ForegroundColor Cyan');
    lines.push('Write-Host ""');
    lines.push('Read-Host "Press Enter to exit"');

    const blob = new Blob([lines.join('\r\n')], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'luma-install.ps1';
    a.click();
    URL.revokeObjectURL(url);
    showToast(`Install script downloaded (${selected.length} programs)`, 'success');
}
