// ══════════════════════════════════════════════════════════════════════════
// utility-tools.js — Utility & Business Tools Frontend Logic
// ══════════════════════════════════════════════════════════════════════════

// ══════════════════════════════════════════════════════════════════════════
// MIND MAP GENERATOR
// ══════════════════════════════════════════════════════════════════════════

document.getElementById('mindmap-input')?.addEventListener('input', e => {
    document.getElementById('mindmap-char-count').textContent = e.target.value.length;
});

async function processMindMap() {
    const input = document.getElementById('mindmap-input').value.trim();
    if (!input || input.length < 50) {
        showToast('Please enter at least 50 characters of content.', 'warning');
        return;
    }
    const statusEl = document.querySelector('.processing-status[data-tool="mind-map"]');
    const resultEl = document.querySelector('.mindmap-result[data-tool="mind-map"]');
    statusEl.classList.remove('hidden');
    resultEl.classList.add('hidden');

    try {
        const resp = await fetch('/api/mind-map', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ text: input })
        });
        const data = await resp.json();
        if (!resp.ok) throw new Error(data.error || 'Failed to generate mind map');
        renderMindMap(data, resultEl);
        if (data.model_used) showModelBadge('mind-map', data.model_used);
        resultEl.classList.remove('hidden');
    } catch (err) {
        showToast(err.message, 'error');
    } finally {
        statusEl.classList.add('hidden');
    }
}

function renderMindMap(data, container) {
    // data.nodes = [{id, label, parent}], data.central = "main topic"
    const nodes = data.nodes || [];
    const central = data.central || 'Main Topic';
    
    container.innerHTML = `
        <div class="mindmap-container">
            <div class="mindmap-toolbar">
                <button class="btn-secondary" onclick="mindmapZoom(1.2)"><i class="fas fa-search-plus"></i> Zoom In</button>
                <button class="btn-secondary" onclick="mindmapZoom(0.8)"><i class="fas fa-search-minus"></i> Zoom Out</button>
                <button class="btn-secondary" onclick="mindmapReset()"><i class="fas fa-compress-arrows-alt"></i> Reset</button>
                <button class="btn-secondary" onclick="downloadMindMapSVG()"><i class="fas fa-download"></i> Download SVG</button>
                <button class="btn-secondary" onclick="copyMindMapText()"><i class="fas fa-copy"></i> Copy as Text</button>
            </div>
            <div class="mindmap-hint"><i class="fas fa-info-circle"></i> Drag to pan, scroll to zoom</div>
            <div class="mindmap-canvas" id="mindmap-canvas"></div>
            <div class="mindmap-text-output hidden" id="mindmap-text-output"></div>
        </div>
    `;
    
    const canvas = document.getElementById('mindmap-canvas');
    const width = 1200;
    const height = 800;
    
    // Build tree structure
    const tree = buildMindMapTree(nodes, central);
    
    // Create SVG with viewBox for pan/zoom
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    svg.setAttribute('width', '100%');
    svg.setAttribute('height', '500');
    svg.setAttribute('viewBox', `0 0 ${width} ${height}`);
    svg.setAttribute('id', 'mindmap-svg');
    svg.style.background = 'rgba(15,23,42,0.5)';
    svg.style.cursor = 'grab';
    
    // Draw mind map centered
    drawMindMapNode(svg, tree, width / 2, height / 2, 0, Math.PI * 2, 0, 180);
    
    canvas.appendChild(svg);
    
    // Setup pan/zoom
    initMindMapPanZoom(svg, width, height);
    
    // Store text version for copy
    const textOutput = document.getElementById('mindmap-text-output');
    textOutput.textContent = generateMindMapText(tree, 0);
}

// Mind map pan/zoom state
let mmViewBox = { x: 0, y: 0, w: 1200, h: 800 };
let mmIsPanning = false;
let mmStartPoint = { x: 0, y: 0 };

function initMindMapPanZoom(svg, width, height) {
    mmViewBox = { x: 0, y: 0, w: width, h: height };
    
    svg.addEventListener('mousedown', e => {
        mmIsPanning = true;
        mmStartPoint = { x: e.clientX, y: e.clientY };
        svg.style.cursor = 'grabbing';
    });
    
    svg.addEventListener('mousemove', e => {
        if (!mmIsPanning) return;
        const dx = (e.clientX - mmStartPoint.x) * (mmViewBox.w / svg.clientWidth);
        const dy = (e.clientY - mmStartPoint.y) * (mmViewBox.h / svg.clientHeight);
        mmViewBox.x -= dx;
        mmViewBox.y -= dy;
        mmStartPoint = { x: e.clientX, y: e.clientY };
        svg.setAttribute('viewBox', `${mmViewBox.x} ${mmViewBox.y} ${mmViewBox.w} ${mmViewBox.h}`);
    });
    
    svg.addEventListener('mouseup', () => {
        mmIsPanning = false;
        svg.style.cursor = 'grab';
    });
    
    svg.addEventListener('mouseleave', () => {
        mmIsPanning = false;
        svg.style.cursor = 'grab';
    });
    
    svg.addEventListener('wheel', e => {
        e.preventDefault();
        const scale = e.deltaY > 0 ? 1.1 : 0.9;
        const rect = svg.getBoundingClientRect();
        const mx = (e.clientX - rect.left) / rect.width;
        const my = (e.clientY - rect.top) / rect.height;
        
        const newW = mmViewBox.w * scale;
        const newH = mmViewBox.h * scale;
        mmViewBox.x += (mmViewBox.w - newW) * mx;
        mmViewBox.y += (mmViewBox.h - newH) * my;
        mmViewBox.w = newW;
        mmViewBox.h = newH;
        svg.setAttribute('viewBox', `${mmViewBox.x} ${mmViewBox.y} ${mmViewBox.w} ${mmViewBox.h}`);
    }, { passive: false });
}

function mindmapZoom(factor) {
    const svg = document.getElementById('mindmap-svg');
    if (!svg) return;
    const newW = mmViewBox.w / factor;
    const newH = mmViewBox.h / factor;
    mmViewBox.x += (mmViewBox.w - newW) / 2;
    mmViewBox.y += (mmViewBox.h - newH) / 2;
    mmViewBox.w = newW;
    mmViewBox.h = newH;
    svg.setAttribute('viewBox', `${mmViewBox.x} ${mmViewBox.y} ${mmViewBox.w} ${mmViewBox.h}`);
}

function mindmapReset() {
    const svg = document.getElementById('mindmap-svg');
    if (!svg) return;
    mmViewBox = { x: 0, y: 0, w: 1200, h: 800 };
    svg.setAttribute('viewBox', `0 0 1200 800`);
}

function buildMindMapTree(nodes, central) {
    const tree = { label: central, children: [] };
    const nodeMap = { root: tree };
    
    // Group by parent
    nodes.forEach(n => {
        const parent = n.parent === 'root' || !n.parent ? tree : nodeMap[n.parent];
        const node = { label: n.label, children: [] };
        nodeMap[n.id] = node;
        if (parent) parent.children.push(node);
        else tree.children.push(node);
    });
    
    return tree;
}

function drawMindMapNode(svg, node, x, y, startAngle, endAngle, depth, radius) {
    const colors = ['#3b82f6', '#10b981', '#f59e0b', '#ef4444', '#8b5cf6', '#06b6d4'];
    const color = colors[depth % colors.length];
    
    // Draw node
    const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
    
    if (depth === 0) {
        // Central node
        const rect = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
        const textLen = node.label.length * 8 + 24;
        rect.setAttribute('x', x - textLen / 2);
        rect.setAttribute('y', y - 18);
        rect.setAttribute('width', textLen);
        rect.setAttribute('height', 36);
        rect.setAttribute('rx', 18);
        rect.setAttribute('fill', color);
        g.appendChild(rect);
    } else {
        const rect = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
        const textLen = node.label.length * 6 + 16;
        rect.setAttribute('x', x - textLen / 2);
        rect.setAttribute('y', y - 12);
        rect.setAttribute('width', textLen);
        rect.setAttribute('height', 24);
        rect.setAttribute('rx', 12);
        rect.setAttribute('fill', color);
        rect.setAttribute('fill-opacity', '0.8');
        g.appendChild(rect);
    }
    
    const text = document.createElementNS('http://www.w3.org/2000/svg', 'text');
    text.setAttribute('x', x);
    text.setAttribute('y', y + 5);
    text.setAttribute('text-anchor', 'middle');
    text.setAttribute('fill', '#fff');
    text.setAttribute('font-size', depth === 0 ? '14' : '11');
    text.setAttribute('font-weight', depth === 0 ? 'bold' : 'normal');
    text.textContent = node.label.length > 25 ? node.label.slice(0, 22) + '...' : node.label;
    g.appendChild(text);
    
    svg.appendChild(g);
    
    // Draw children
    if (node.children.length > 0) {
        const angleStep = (endAngle - startAngle) / node.children.length;
        node.children.forEach((child, i) => {
            const angle = startAngle + angleStep * (i + 0.5);
            const childX = x + Math.cos(angle) * radius;
            const childY = y + Math.sin(angle) * radius;
            
            // Draw line
            const line = document.createElementNS('http://www.w3.org/2000/svg', 'path');
            const midX = (x + childX) / 2;
            const midY = (y + childY) / 2;
            line.setAttribute('d', `M ${x} ${y} Q ${midX} ${y} ${childX} ${childY}`);
            line.setAttribute('stroke', colors[(depth + 1) % colors.length]);
            line.setAttribute('stroke-width', Math.max(1, 3 - depth));
            line.setAttribute('fill', 'none');
            line.setAttribute('stroke-opacity', '0.6');
            svg.insertBefore(line, svg.firstChild);
            
            // Draw child recursively
            const childAngleSpread = angleStep * 0.8;
            drawMindMapNode(svg, child, childX, childY, angle - childAngleSpread / 2, angle + childAngleSpread / 2, depth + 1, radius * 0.7);
        });
    }
}

function generateMindMapText(node, depth) {
    const indent = '  '.repeat(depth);
    let text = indent + (depth === 0 ? '# ' : '- ') + node.label + '\n';
    node.children.forEach(c => text += generateMindMapText(c, depth + 1));
    return text;
}

function downloadMindMapSVG() {
    const svg = document.getElementById('mindmap-svg');
    if (!svg) return;
    const svgData = new XMLSerializer().serializeToString(svg);
    const blob = new Blob([svgData], { type: 'image/svg+xml' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'mindmap.svg';
    a.click();
    URL.revokeObjectURL(url);
    showToast('Mind map downloaded!', 'success');
}

function copyMindMapText() {
    const textEl = document.getElementById('mindmap-text-output');
    if (!textEl) return;
    navigator.clipboard.writeText(textEl.textContent);
    showToast('Mind map text copied!', 'success');
}

// ══════════════════════════════════════════════════════════════════════════
// YOUTUBE SUMMARY
// ══════════════════════════════════════════════════════════════════════════

async function processYouTubeSummary() {
    const url = document.getElementById('youtube-url-input').value.trim();
    const videoIdMatch = url.match(/(?:v=|\/|youtu\.be\/)([a-zA-Z0-9_-]{11})/);
    if (!videoIdMatch) {
        showToast('Please enter a valid YouTube URL.', 'warning');
        return;
    }
    const videoId = videoIdMatch[1];
    const statusEl = document.querySelector('.processing-status[data-tool="youtube-summary"]');
    const resultEl = document.querySelector('.youtube-result[data-tool="youtube-summary"]');
    statusEl.classList.remove('hidden');
    resultEl.classList.add('hidden');

    try {
        const resp = await fetch('/api/youtube-summary', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ videoId })
        });
        const data = await resp.json();
        if (!resp.ok) throw new Error(data.error || 'Failed to summarize video');
        renderYouTubeSummary(data, resultEl, videoId);
        if (data.model_used) showModelBadge('youtube-summary', data.model_used);
        resultEl.classList.remove('hidden');
    } catch (err) {
        showToast(err.message, 'error');
    } finally {
        statusEl.classList.add('hidden');
    }
}

function renderYouTubeSummary(data, container, videoId) {
    const summaryText = (data.title ? data.title + '\n\n' : '') +
        (data.summary || '') +
        (data.keyPoints?.length ? '\n\nKey Points:\n' + data.keyPoints.map(p => '- ' + p).join('\n') : '');
    container.innerHTML = `
        <div class="youtube-summary-container">
            <div class="youtube-thumbnail">
                <img src="https://img.youtube.com/vi/${videoId}/maxresdefault.jpg" alt="Video thumbnail" onerror="this.src='https://img.youtube.com/vi/${videoId}/hqdefault.jpg'">
            </div>
            <div class="youtube-summary-content">
                <h3>${escapeHtml(data.title || 'Video Summary')}</h3>
                <div class="youtube-summary-text">${data.summary.replace(/\n/g, '<br>')}</div>
                ${data.keyPoints ? `
                <div class="youtube-key-points">
                    <h4><i class="fas fa-list"></i> Key Points</h4>
                    <ul>${data.keyPoints.map(p => `<li>${escapeHtml(p)}</li>`).join('')}</ul>
                </div>` : ''}
            </div>
            <div class="youtube-summary-actions">
                <button class="btn-secondary" onclick="copyYouTubeSummary()"><i class="fas fa-copy"></i> Copy Summary</button>
                <div class="yt-ai-actions">
                    <span class="yt-ai-label"><i class="fas fa-magic"></i> Use summary for:</span>
                    <button class="yt-ai-btn" onclick="sendYouTubeToAI('ai-study-notes')"><i class="fas fa-brain"></i> Study Notes</button>
                    <button class="yt-ai-btn" onclick="sendYouTubeToAI('ai-flashcards')"><i class="fas fa-clone"></i> Flashcards</button>
                    <button class="yt-ai-btn" onclick="sendYouTubeToAI('ai-quiz')"><i class="fas fa-question-circle"></i> Practice Quiz</button>
                </div>
            </div>
        </div>
    `;
    // Store text on container for sendYouTubeToAI to read
    container.dataset.summaryText = summaryText;
}

function copyYouTubeSummary() {
    const text = document.querySelector('.youtube-summary-text')?.innerText;
    if (text) {
        navigator.clipboard.writeText(text);
        showToast('Summary copied!', 'success');
    }
}

function sendYouTubeToAI(toolId) {
    const container = document.querySelector('.youtube-result[data-tool="youtube-summary"]');
    const text = container?.dataset.summaryText || document.querySelector('.youtube-summary-text')?.innerText || '';
    if (!text.trim()) { showToast('No summary available to send.', 'error'); return; }

    switchTool(toolId);
    if (toolId === 'ai-study-notes') {
        // Activate paste mode
        const modeGrid = document.querySelector('.preset-grid[data-tool="study-notes-input-mode"]');
        modeGrid?.querySelectorAll('.preset-btn').forEach(b => b.classList.remove('active'));
        modeGrid?.querySelector('[data-val="paste"]')?.classList.add('active');
        toggleStudyNotesInput('paste');
        const ta = document.getElementById('study-notes-text-input');
        if (ta) { ta.value = text; ta.dispatchEvent(new Event('input')); }
    } else if (toolId === 'ai-flashcards') {
        const modeGrid = document.querySelector('.preset-grid[data-tool="flashcards-input-mode"]');
        modeGrid?.querySelectorAll('.preset-btn').forEach(b => b.classList.remove('active'));
        modeGrid?.querySelector('[data-val="paste"]')?.classList.add('active');
        toggleFlashcardsInput('paste');
        const ta = document.getElementById('flashcards-text-input');
        if (ta) { ta.value = text; ta.dispatchEvent(new Event('input')); }
    } else if (toolId === 'ai-quiz') {
        const modeGrid = document.querySelector('.preset-grid[data-tool="quiz-input-mode"]');
        modeGrid?.querySelectorAll('.preset-btn').forEach(b => b.classList.remove('active'));
        modeGrid?.querySelector('[data-val="paste"]')?.classList.add('active');
        toggleQuizInput('paste');
        const ta = document.getElementById('quiz-text-input');
        if (ta) { ta.value = text; ta.dispatchEvent(new Event('input')); }
    }
    showToast('Summary loaded — adjust settings and generate!', 'success');
}

// ══════════════════════════════════════════════════════════════════════════
// COLOR PALETTE EXTRACTOR
// ══════════════════════════════════════════════════════════════════════════

function initColorPaletteUpload() {
    const zone = document.getElementById('uz-color-palette');
    if (!zone || zone.dataset.initialized) return;
    zone.dataset.initialized = 'true';
    
    const input = zone.querySelector('.upload-input');
    zone.addEventListener('click', () => input.click());
    zone.addEventListener('dragover', e => { e.preventDefault(); zone.classList.add('dragover'); });
    zone.addEventListener('dragleave', () => zone.classList.remove('dragover'));
    zone.addEventListener('drop', e => {
        e.preventDefault();
        zone.classList.remove('dragover');
        if (e.dataTransfer.files.length) handleColorPaletteFile(e.dataTransfer.files[0]);
    });
    input.addEventListener('change', () => {
        if (input.files.length) handleColorPaletteFile(input.files[0]);
    });
}

function handleColorPaletteFile(file) {
    if (!file.type.startsWith('image/')) {
        showToast('Please upload an image file.', 'warning');
        return;
    }
    const reader = new FileReader();
    reader.onload = e => extractColorPalette(e.target.result);
    reader.readAsDataURL(file);
}

function extractColorPalette(dataUrl) {
    const img = new Image();
    img.onload = () => {
        const canvas = document.createElement('canvas');
        const ctx = canvas.getContext('2d');
        const size = 100; // Sample at lower res for speed
        canvas.width = size;
        canvas.height = size;
        ctx.drawImage(img, 0, 0, size, size);
        
        const imageData = ctx.getImageData(0, 0, size, size).data;
        const colors = {};
        
        // Sample colors
        for (let i = 0; i < imageData.length; i += 4) {
            // Quantize to reduce unique colors
            const r = Math.round(imageData[i] / 32) * 32;
            const g = Math.round(imageData[i + 1] / 32) * 32;
            const b = Math.round(imageData[i + 2] / 32) * 32;
            const key = `${r},${g},${b}`;
            colors[key] = (colors[key] || 0) + 1;
        }
        
        // Sort by frequency and get top colors
        const sorted = Object.entries(colors)
            .sort((a, b) => b[1] - a[1])
            .slice(0, 8)
            .map(([rgb]) => {
                const [r, g, b] = rgb.split(',').map(Number);
                return { r, g, b, hex: rgbToHex(r, g, b) };
            });
        
        // Filter out near-duplicates
        const palette = [];
        sorted.forEach(c => {
            const isDup = palette.some(p => colorDistance(c, p) < 50);
            if (!isDup && palette.length < 6) palette.push(c);
        });
        
        renderColorPalette(palette, dataUrl);
    };
    img.src = dataUrl;
}

function rgbToHex(r, g, b) {
    return '#' + [r, g, b].map(x => x.toString(16).padStart(2, '0')).join('');
}

function colorDistance(c1, c2) {
    return Math.sqrt((c1.r - c2.r) ** 2 + (c1.g - c2.g) ** 2 + (c1.b - c2.b) ** 2);
}

function renderColorPalette(palette, imgSrc) {
    const container = document.querySelector('.palette-result[data-tool="color-palette"]');
    container.innerHTML = `
        <div class="palette-preview">
            <img src="${imgSrc}" alt="Source image">
        </div>
        <div class="palette-colors">
            ${palette.map(c => `
                <div class="palette-color" onclick="copyColor('${c.hex}')" title="Click to copy">
                    <div class="palette-swatch" style="background:${c.hex}"></div>
                    <span class="palette-hex">${c.hex.toUpperCase()}</span>
                    <span class="palette-rgb">rgb(${c.r}, ${c.g}, ${c.b})</span>
                </div>
            `).join('')}
        </div>
    `;
    container.classList.remove('hidden');
}

function copyColor(hex) {
    navigator.clipboard.writeText(hex);
    showToast(`Copied ${hex}`, 'success');
}

// ══════════════════════════════════════════════════════════════════════════
// UNIX ↔ DATE CONVERTER
// ══════════════════════════════════════════════════════════════════════════

function initUnixDateConverter() {
    const unixInput = document.getElementById('unix-timestamp');
    const dateInput = document.getElementById('human-date');
    if (!unixInput || !dateInput) return;
    
    unixInput.addEventListener('input', () => {
        const ts = parseInt(unixInput.value);
        if (!isNaN(ts)) {
            const date = new Date(ts * 1000);
            dateInput.value = formatDatetimeLocal(date);
            showUnixOutput(ts);
        }
    });
    
    dateInput.addEventListener('input', () => {
        const date = new Date(dateInput.value);
        if (!isNaN(date.getTime())) {
            const ts = Math.floor(date.getTime() / 1000);
            unixInput.value = ts;
            showUnixOutput(ts);
        }
    });
}

function setCurrentTimestamp() {
    const unixInput = document.getElementById('unix-timestamp');
    const dateInput = document.getElementById('human-date');
    const now = Math.floor(Date.now() / 1000);
    unixInput.value = now;
    dateInput.value = formatDatetimeLocal(new Date());
    showUnixOutput(now);
}

function formatDatetimeLocal(date) {
    const pad = n => n.toString().padStart(2, '0');
    return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}T${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`;
}

function showUnixOutput(ts) {
    const outputEl = document.getElementById('unix-output');
    const date = new Date(ts * 1000);
    outputEl.innerHTML = `
        <div class="unix-info-grid">
            <div><strong>ISO 8601:</strong> ${date.toISOString()}</div>
            <div><strong>UTC:</strong> ${date.toUTCString()}</div>
            <div><strong>Local:</strong> ${date.toLocaleString()}</div>
            <div><strong>Relative:</strong> ${getRelativeTime(date)}</div>
        </div>
    `;
    outputEl.classList.remove('hidden');
}

function getRelativeTime(date) {
    const diff = Date.now() - date.getTime();
    const abs = Math.abs(diff);
    const past = diff > 0;
    
    if (abs < 60000) return past ? 'just now' : 'in a moment';
    if (abs < 3600000) return `${Math.floor(abs / 60000)} minutes ${past ? 'ago' : 'from now'}`;
    if (abs < 86400000) return `${Math.floor(abs / 3600000)} hours ${past ? 'ago' : 'from now'}`;
    if (abs < 2592000000) return `${Math.floor(abs / 86400000)} days ${past ? 'ago' : 'from now'}`;
    return `${Math.floor(abs / 2592000000)} months ${past ? 'ago' : 'from now'}`;
}

// ══════════════════════════════════════════════════════════════════════════
// REGEX TESTER
// ══════════════════════════════════════════════════════════════════════════

function initRegexTester() {
    const patternInput = document.getElementById('regex-pattern');
    const flagsInput = document.getElementById('regex-flags');
    const textInput = document.getElementById('regex-test-text');
    
    if (!patternInput || !textInput) return;
    
    const runTest = () => testRegex();
    patternInput.addEventListener('input', runTest);
    flagsInput?.addEventListener('input', runTest);
    textInput.addEventListener('input', runTest);
}

function testRegex() {
    const pattern = document.getElementById('regex-pattern').value;
    const flags = document.getElementById('regex-flags').value || 'g';
    const text = document.getElementById('regex-test-text').value;
    const resultEl = document.getElementById('regex-result');
    
    if (!pattern || !text) {
        resultEl.classList.add('hidden');
        return;
    }
    
    try {
        const regex = new RegExp(pattern, flags);
        const matches = [];
        let match;
        
        if (flags.includes('g')) {
            while ((match = regex.exec(text)) !== null) {
                matches.push({ value: match[0], index: match.index, groups: match.slice(1) });
                if (!match[0]) break; // Prevent infinite loop on empty match
            }
        } else {
            match = regex.exec(text);
            if (match) matches.push({ value: match[0], index: match.index, groups: match.slice(1) });
        }
        
        // Highlight matches
        let highlighted = escapeHtml(text);
        let offset = 0;
        matches.forEach(m => {
            const start = m.index + offset;
            const end = start + m.value.length;
            const before = highlighted.slice(0, start);
            const matched = highlighted.slice(start, end);
            const after = highlighted.slice(end);
            const mark = `<mark class="regex-match">${matched}</mark>`;
            highlighted = before + mark + after;
            offset += mark.length - m.value.length;
        });
        
        resultEl.innerHTML = `
            <div class="regex-matches-count">${matches.length} match${matches.length !== 1 ? 'es' : ''} found</div>
            <div class="regex-highlighted">${highlighted}</div>
            ${matches.length > 0 ? `
            <div class="regex-matches-list">
                ${matches.slice(0, 20).map((m, i) => `
                    <div class="regex-match-item">
                        <span class="match-num">${i + 1}</span>
                        <span class="match-value">"${escapeHtml(m.value)}"</span>
                        <span class="match-index">@ ${m.index}</span>
                        ${m.groups.length ? `<span class="match-groups">Groups: ${m.groups.map(g => `"${escapeHtml(g || '')}"`).join(', ')}</span>` : ''}
                    </div>
                `).join('')}
                ${matches.length > 20 ? `<div class="regex-more">... and ${matches.length - 20} more</div>` : ''}
            </div>` : ''}
        `;
        resultEl.classList.remove('hidden');
    } catch (e) {
        resultEl.innerHTML = `<div class="regex-error"><i class="fas fa-exclamation-triangle"></i> ${e.message}</div>`;
        resultEl.classList.remove('hidden');
    }
}

// ══════════════════════════════════════════════════════════════════════════
// CODE BEAUTIFIER
// ══════════════════════════════════════════════════════════════════════════

function beautifyCode() {
    const lang = document.querySelector('[data-tool="code-lang"] .preset-btn.active')?.dataset.val || 'javascript';
    const input = document.getElementById('code-input').value;
    const outputContainer = document.getElementById('code-output');
    const outputText = document.getElementById('code-output-text');
    
    if (!input.trim()) {
        showToast('Please enter some code to beautify.', 'warning');
        return;
    }
    
    try {
        let formatted;
        switch (lang) {
            case 'json':
                formatted = JSON.stringify(JSON.parse(input), null, 2);
                break;
            case 'javascript':
            case 'typescript':
                formatted = beautifyJS(input);
                break;
            case 'css':
                formatted = beautifyCSS(input);
                break;
            case 'html':
                formatted = beautifyHTML(input);
                break;
            case 'xml':
                formatted = beautifyXML(input);
                break;
            case 'sql':
                formatted = beautifySQL(input);
                break;
            case 'python':
                formatted = beautifyPython(input);
                break;
            case 'java':
            case 'csharp':
            case 'cpp':
            case 'go':
            case 'rust':
            case 'php':
                formatted = beautifyCLike(input);
                break;
            case 'ruby':
                formatted = beautifyRuby(input);
                break;
            case 'yaml':
                formatted = beautifyYAML(input);
                break;
            case 'markdown':
                formatted = beautifyMarkdown(input);
                break;
            case 'graphql':
                formatted = beautifyGraphQL(input);
                break;
            default:
                formatted = input;
        }
        outputText.textContent = formatted;
        outputContainer.classList.remove('hidden');
    } catch (e) {
        showToast(`Error: ${e.message}`, 'error');
    }
}

function beautifyJS(code) {
    // Simple JS beautifier (indent blocks)
    let result = '';
    let indent = 0;
    let inString = false;
    let stringChar = '';
    
    for (let i = 0; i < code.length; i++) {
        const c = code[i];
        const next = code[i + 1];
        
        if (inString) {
            result += c;
            if (c === stringChar && code[i - 1] !== '\\') inString = false;
            continue;
        }
        
        if (c === '"' || c === "'" || c === '`') {
            inString = true;
            stringChar = c;
            result += c;
            continue;
        }
        
        if (c === '{' || c === '[') {
            result += c + '\n' + '  '.repeat(++indent);
        } else if (c === '}' || c === ']') {
            result = result.trimEnd() + '\n' + '  '.repeat(--indent) + c;
            if (next && next !== ',' && next !== ')' && next !== ';') result += '\n' + '  '.repeat(indent);
        } else if (c === ';') {
            result += c + '\n' + '  '.repeat(indent);
        } else if (c === ',') {
            result += c;
            if (next !== '\n') result += '\n' + '  '.repeat(indent);
        } else if (c === '\n' || c === '\r') {
            // Skip original newlines
        } else {
            result += c;
        }
    }
    
    return result.replace(/\n\s*\n/g, '\n').trim();
}

function beautifyCSS(code) {
    return code
        .replace(/\s*{\s*/g, ' {\n  ')
        .replace(/\s*}\s*/g, '\n}\n')
        .replace(/;\s*/g, ';\n  ')
        .replace(/,\s*/g, ',\n')
        .replace(/\n\s*\n/g, '\n')
        .replace(/\{\s+\}/g, '{}')
        .trim();
}

function beautifyHTML(code) {
    const lines = [];
    let indent = 0;
    const selfClosing = ['br', 'hr', 'img', 'input', 'meta', 'link', 'area', 'base', 'col', 'embed', 'param', 'source', 'track', 'wbr'];
    
    code.replace(/>(\s*)</g, '>\n<').split('\n').forEach(line => {
        line = line.trim();
        if (!line) return;
        
        const isClosing = /^<\//.test(line);
        const isOpening = /^<[a-zA-Z]/.test(line) && !selfClosing.some(t => line.toLowerCase().startsWith(`<${t}`));
        const isSelfClose = /\/>$/.test(line) || selfClosing.some(t => line.toLowerCase().startsWith(`<${t}`));
        
        if (isClosing) indent = Math.max(0, indent - 1);
        lines.push('  '.repeat(indent) + line);
        if (isOpening && !isSelfClose && !line.includes('</')) indent++;
    });
    
    return lines.join('\n');
}

function beautifySQL(code) {
    const keywords = ['SELECT', 'FROM', 'WHERE', 'AND', 'OR', 'JOIN', 'LEFT', 'RIGHT', 'INNER', 'OUTER', 'ON', 'GROUP BY', 'ORDER BY', 'HAVING', 'LIMIT', 'OFFSET', 'INSERT', 'INTO', 'VALUES', 'UPDATE', 'SET', 'DELETE', 'CREATE', 'ALTER', 'DROP', 'TABLE', 'INDEX', 'UNION', 'CASE', 'WHEN', 'THEN', 'ELSE', 'END', 'AS'];
    
    let result = code.toUpperCase();
    keywords.forEach(kw => {
        result = result.replace(new RegExp(`\\b${kw}\\b`, 'gi'), `\n${kw}`);
    });
    
    return result.replace(/^\n+/, '').replace(/\n{2,}/g, '\n').trim();
}

function beautifyXML(code) {
    const lines = [];
    let indent = 0;
    
    code.replace(/>(\s*)</g, '>\n<').split('\n').forEach(line => {
        line = line.trim();
        if (!line) return;
        
        const isClosing = /^<\//.test(line);
        const isSelfClose = /\/>$/.test(line);
        const isOpening = /^<[a-zA-Z?!]/.test(line) && !isClosing;
        
        if (isClosing) indent = Math.max(0, indent - 1);
        lines.push('  '.repeat(indent) + line);
        if (isOpening && !isSelfClose && !line.includes('</')) indent++;
    });
    
    return lines.join('\n');
}

function beautifyPython(code) {
    let result = '';
    let indent = 0;
    const lines = code.split('\n');
    const increaseIndent = /:\s*$/;
    const decreaseKeywords = ['return', 'break', 'continue', 'pass', 'raise'];
    
    lines.forEach((line, i) => {
        const trimmed = line.trim();
        if (!trimmed) {
            result += '\n';
            return;
        }
        
        // Check for dedent keywords
        if (/^(elif|else|except|finally)\b/.test(trimmed)) {
            indent = Math.max(0, indent - 1);
        }
        
        result += '    '.repeat(indent) + trimmed + '\n';
        
        // Check if we should increase indent
        if (increaseIndent.test(trimmed)) {
            indent++;
        }
        
        // Single-line decrease (return, break, etc. at end of block)
        if (decreaseKeywords.some(kw => trimmed.startsWith(kw))) {
            // Don't decrease indent here, let the next line handle it
        }
    });
    
    return result.trim();
}

function beautifyCLike(code) {
    // Generic C-like beautifier for Java, C#, C++, Go, Rust, PHP
    let result = '';
    let indent = 0;
    let inString = false;
    let stringChar = '';
    let prevChar = '';
    
    for (let i = 0; i < code.length; i++) {
        const c = code[i];
        const next = code[i + 1] || '';
        
        if (inString) {
            result += c;
            if (c === stringChar && prevChar !== '\\') inString = false;
            prevChar = c;
            continue;
        }
        
        if (c === '"' || c === "'" || c === '`') {
            inString = true;
            stringChar = c;
            result += c;
            prevChar = c;
            continue;
        }
        
        if (c === '{') {
            result = result.trimEnd() + ' {\n' + '    '.repeat(++indent);
        } else if (c === '}') {
            result = result.trimEnd() + '\n' + '    '.repeat(--indent) + '}';
            if (next && next !== ';' && next !== ',' && next !== ')' && next !== '\n' && next !== ' ') {
                result += '\n' + '    '.repeat(indent);
            }
        } else if (c === ';') {
            result += ';\n' + '    '.repeat(indent);
        } else if (c === '\n' || c === '\r') {
            // Skip original newlines
        } else {
            result += c;
        }
        prevChar = c;
    }
    
    return result.replace(/\n\s*\n\s*\n/g, '\n\n').replace(/{\s+}/g, '{}').trim();
}

function beautifyRuby(code) {
    let result = '';
    let indent = 0;
    const lines = code.split('\n');
    const increaseKeywords = /^(def|class|module|if|unless|case|while|until|for|begin|do)\b/;
    const decreaseKeywords = /^(end|else|elsif|when|rescue|ensure)\b/;
    
    lines.forEach(line => {
        const trimmed = line.trim();
        if (!trimmed) {
            result += '\n';
            return;
        }
        
        if (decreaseKeywords.test(trimmed)) {
            indent = Math.max(0, indent - 1);
        }
        
        result += '  '.repeat(indent) + trimmed + '\n';
        
        if (increaseKeywords.test(trimmed) || /\bdo\s*(\|.*\|)?\s*$/.test(trimmed)) {
            indent++;
        }
        
        if (/^end\b/.test(trimmed)) {
            // Already handled above
        }
    });
    
    return result.trim();
}

function beautifyYAML(code) {
    const lines = code.split('\n');
    let result = '';
    
    lines.forEach(line => {
        const trimmed = line.trim();
        if (!trimmed) {
            result += '\n';
            return;
        }
        
        // Preserve existing indentation for YAML
        const leadingSpaces = line.match(/^(\s*)/)[1].length;
        const properIndent = Math.floor(leadingSpaces / 2) * 2;
        result += ' '.repeat(properIndent) + trimmed + '\n';
    });
    
    return result.trim();
}

function beautifyMarkdown(code) {
    let result = code
        // Ensure headers have space after #
        .replace(/^(#{1,6})([^\s#])/gm, '$1 $2')
        // Ensure blank lines before headers
        .replace(/([^\n])\n(#{1,6}\s)/g, '$1\n\n$2')
        // Ensure blank lines before code blocks
        .replace(/([^\n])\n```/g, '$1\n\n```')
        // Ensure blank lines after code blocks
        .replace(/```\n([^\n])/g, '```\n\n$1')
        // Clean up excessive blank lines
        .replace(/\n{3,}/g, '\n\n');
    
    return result.trim();
}

function beautifyGraphQL(code) {
    let result = '';
    let indent = 0;
    let inString = false;
    
    for (let i = 0; i < code.length; i++) {
        const c = code[i];
        const next = code[i + 1] || '';
        
        if (c === '"') {
            inString = !inString;
            result += c;
            continue;
        }
        
        if (inString) {
            result += c;
            continue;
        }
        
        if (c === '{') {
            result = result.trimEnd() + ' {\n' + '  '.repeat(++indent);
        } else if (c === '}') {
            result = result.trimEnd() + '\n' + '  '.repeat(--indent) + '}';
            if (next && next !== '\n') result += '\n' + '  '.repeat(indent);
        } else if (c === '(') {
            result += '(';
        } else if (c === ')') {
            result += ')';
        } else if (c === '\n' || c === '\r') {
            // Skip
        } else if (c === ' ' && result.endsWith(' ')) {
            // Skip extra spaces
        } else {
            result += c;
        }
    }
    
    return result.replace(/\n\s*\n/g, '\n').trim();
}

function copyCodeOutput() {
    const code = document.getElementById('code-output-text').textContent;
    navigator.clipboard.writeText(code);
    showToast('Code copied!', 'success');
}

// ══════════════════════════════════════════════════════════════════════════
// UUID GENERATOR
// ══════════════════════════════════════════════════════════════════════════

function generateUUIDs() {
    const count = parseInt(document.querySelector('[data-tool="uuid-count"] .preset-btn.active')?.dataset.val || '1');
    const list = document.getElementById('uuid-list');
    const output = document.getElementById('uuid-output');
    const label = document.getElementById('uuid-count-label');
    
    const uuids = [];
    for (let i = 0; i < count; i++) {
        uuids.push(generateUUIDv4());
    }
    
    label.textContent = `${count} UUID${count > 1 ? 's' : ''} generated`;
    list.innerHTML = uuids.map(u => `
        <div class="uuid-item">
            <code>${u}</code>
            <button class="btn-icon" onclick="copyUUID('${u}')" title="Copy"><i class="fas fa-copy"></i></button>
        </div>
    `).join('');
    output.classList.remove('hidden');
}

function generateUUIDv4() {
    return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, c => {
        const r = Math.random() * 16 | 0;
        const v = c === 'x' ? r : (r & 0x3 | 0x8);
        return v.toString(16);
    });
}

function copyUUID(uuid) {
    navigator.clipboard.writeText(uuid);
    showToast('UUID copied!', 'success');
}

function copyAllUUIDs() {
    const uuids = Array.from(document.querySelectorAll('.uuid-item code')).map(el => el.textContent).join('\n');
    navigator.clipboard.writeText(uuids);
    showToast('All UUIDs copied!', 'success');
}

// ══════════════════════════════════════════════════════════════════════════
// URL ENCODER / DECODER
// ══════════════════════════════════════════════════════════════════════════

function encodeURL() {
    const input = document.getElementById('url-input').value;
    const output = document.getElementById('url-output');
    const outputText = document.getElementById('url-output-text');
    const direction = document.getElementById('url-direction');
    
    if (!input) {
        showToast('Please enter text to encode.', 'warning');
        return;
    }
    
    outputText.value = encodeURIComponent(input);
    direction.textContent = 'Encoded';
    output.classList.remove('hidden');
}

function decodeURL() {
    const input = document.getElementById('url-input').value;
    const output = document.getElementById('url-output');
    const outputText = document.getElementById('url-output-text');
    const direction = document.getElementById('url-direction');
    
    if (!input) {
        showToast('Please enter text to decode.', 'warning');
        return;
    }
    
    try {
        outputText.value = decodeURIComponent(input);
        direction.textContent = 'Decoded';
        output.classList.remove('hidden');
    } catch (e) {
        showToast('Invalid URL encoding.', 'error');
    }
}

function copyURLOutput() {
    const output = document.getElementById('url-output-text').value;
    navigator.clipboard.writeText(output);
    showToast('Copied!', 'success');
}

// ══════════════════════════════════════════════════════════════════════════
// RESUME BUILDER
// ══════════════════════════════════════════════════════════════════════════

function addExperience() {
    const list = document.getElementById('resume-experience-list');
    const idx = list.children.length;
    const div = document.createElement('div');
    div.className = 'resume-entry';
    div.innerHTML = `
        <button class="entry-remove" onclick="this.parentElement.remove()"><i class="fas fa-times"></i></button>
        <div class="form-grid">
            <div class="input-group"><label>Job Title</label><input type="text" data-field="exp-title-${idx}" placeholder="Software Engineer"></div>
            <div class="input-group"><label>Company</label><input type="text" data-field="exp-company-${idx}" placeholder="Tech Corp"></div>
            <div class="input-group"><label>Start Date</label><input type="text" data-field="exp-start-${idx}" placeholder="Jan 2020"></div>
            <div class="input-group"><label>End Date</label><input type="text" data-field="exp-end-${idx}" placeholder="Present"></div>
        </div>
        <div class="input-group"><label>Description</label><textarea data-field="exp-desc-${idx}" rows="2" placeholder="Key responsibilities and achievements..."></textarea></div>
    `;
    list.appendChild(div);
}

function addEducation() {
    const list = document.getElementById('resume-education-list');
    const idx = list.children.length;
    const div = document.createElement('div');
    div.className = 'resume-entry';
    div.innerHTML = `
        <button class="entry-remove" onclick="this.parentElement.remove()"><i class="fas fa-times"></i></button>
        <div class="form-grid">
            <div class="input-group"><label>Degree</label><input type="text" data-field="edu-degree-${idx}" placeholder="B.S. Computer Science"></div>
            <div class="input-group"><label>Institution</label><input type="text" data-field="edu-school-${idx}" placeholder="University Name"></div>
            <div class="input-group"><label>Start Year</label><input type="text" data-field="edu-start-${idx}" placeholder="2016"></div>
            <div class="input-group"><label>End Year</label><input type="text" data-field="edu-end-${idx}" placeholder="2020"></div>
        </div>
    `;
    list.appendChild(div);
}

async function generateResume() {
    const name = document.getElementById('resume-name').value || 'Your Name';
    const title = document.getElementById('resume-title').value || '';
    const email = document.getElementById('resume-email').value || '';
    const phone = document.getElementById('resume-phone').value || '';
    const location = document.getElementById('resume-location').value || '';
    const linkedin = document.getElementById('resume-linkedin').value || '';
    const summary = document.getElementById('resume-summary').value || '';
    const skills = document.getElementById('resume-skills').value || '';
    
    // Collect experience entries
    const experiences = [];
    document.querySelectorAll('#resume-experience-list .resume-entry').forEach((entry, i) => {
        experiences.push({
            title: entry.querySelector(`[data-field="exp-title-${i}"]`)?.value || '',
            company: entry.querySelector(`[data-field="exp-company-${i}"]`)?.value || '',
            start: entry.querySelector(`[data-field="exp-start-${i}"]`)?.value || '',
            end: entry.querySelector(`[data-field="exp-end-${i}"]`)?.value || '',
            desc: entry.querySelector(`[data-field="exp-desc-${i}"]`)?.value || ''
        });
    });
    
    // Collect education entries
    const education = [];
    document.querySelectorAll('#resume-education-list .resume-entry').forEach((entry, i) => {
        education.push({
            degree: entry.querySelector(`[data-field="edu-degree-${i}"]`)?.value || '',
            school: entry.querySelector(`[data-field="edu-school-${i}"]`)?.value || '',
            start: entry.querySelector(`[data-field="edu-start-${i}"]`)?.value || '',
            end: entry.querySelector(`[data-field="edu-end-${i}"]`)?.value || ''
        });
    });
    
    // Generate PDF using jsPDF
    await loadJsPDF();
    const { jsPDF } = window.jspdf;
    const doc = new jsPDF();
    
    let y = 20;
    const margin = 20;
    const pageWidth = doc.internal.pageSize.width;
    
    // Header
    doc.setFontSize(24);
    doc.setFont('helvetica', 'bold');
    doc.text(name, pageWidth / 2, y, { align: 'center' });
    y += 8;
    
    if (title) {
        doc.setFontSize(12);
        doc.setFont('helvetica', 'normal');
        doc.setTextColor(100);
        doc.text(title, pageWidth / 2, y, { align: 'center' });
        y += 6;
    }
    
    // Contact info
    doc.setFontSize(10);
    doc.setTextColor(80);
    const contact = [email, phone, location, linkedin].filter(Boolean).join(' • ');
    if (contact) {
        doc.text(contact, pageWidth / 2, y, { align: 'center' });
        y += 10;
    }
    
    // Line
    doc.setDrawColor(200);
    doc.line(margin, y, pageWidth - margin, y);
    y += 8;
    
    doc.setTextColor(0);
    
    // Summary
    if (summary) {
        doc.setFontSize(12);
        doc.setFont('helvetica', 'bold');
        doc.text('PROFESSIONAL SUMMARY', margin, y);
        y += 6;
        doc.setFont('helvetica', 'normal');
        doc.setFontSize(10);
        const sumLines = doc.splitTextToSize(summary, pageWidth - margin * 2);
        doc.text(sumLines, margin, y);
        y += sumLines.length * 5 + 8;
    }
    
    // Experience
    if (experiences.length > 0) {
        doc.setFontSize(12);
        doc.setFont('helvetica', 'bold');
        doc.text('WORK EXPERIENCE', margin, y);
        y += 6;
        
        experiences.forEach(exp => {
            doc.setFontSize(11);
            doc.setFont('helvetica', 'bold');
            doc.text(exp.title, margin, y);
            y += 5;
            doc.setFontSize(10);
            doc.setFont('helvetica', 'normal');
            doc.setTextColor(80);
            doc.text(`${exp.company} | ${exp.start} - ${exp.end}`, margin, y);
            y += 5;
            doc.setTextColor(0);
            if (exp.desc) {
                const descLines = doc.splitTextToSize(exp.desc, pageWidth - margin * 2);
                doc.text(descLines, margin, y);
                y += descLines.length * 4 + 4;
            }
            y += 4;
        });
        y += 4;
    }
    
    // Education
    if (education.length > 0) {
        doc.setFontSize(12);
        doc.setFont('helvetica', 'bold');
        doc.text('EDUCATION', margin, y);
        y += 6;
        
        education.forEach(edu => {
            doc.setFontSize(11);
            doc.setFont('helvetica', 'bold');
            doc.text(edu.degree, margin, y);
            y += 5;
            doc.setFontSize(10);
            doc.setFont('helvetica', 'normal');
            doc.setTextColor(80);
            doc.text(`${edu.school} | ${edu.start} - ${edu.end}`, margin, y);
            doc.setTextColor(0);
            y += 8;
        });
        y += 4;
    }
    
    // Skills
    if (skills) {
        doc.setFontSize(12);
        doc.setFont('helvetica', 'bold');
        doc.text('SKILLS', margin, y);
        y += 6;
        doc.setFontSize(10);
        doc.setFont('helvetica', 'normal');
        const skillLines = doc.splitTextToSize(skills, pageWidth - margin * 2);
        doc.text(skillLines, margin, y);
    }
    
    doc.save('resume.pdf');
    showToast('Resume PDF generated!', 'success');
}

// ══════════════════════════════════════════════════════════════════════════
// INVOICE GENERATOR
// ══════════════════════════════════════════════════════════════════════════

let invoiceItemIndex = 0;

function initInvoiceGenerator() {
    // Set default dates
    const today = new Date().toISOString().split('T')[0];
    const due = new Date(Date.now() + 30 * 86400000).toISOString().split('T')[0];
    
    const dateEl = document.getElementById('inv-date');
    const dueEl = document.getElementById('inv-due');
    if (dateEl && !dateEl.value) dateEl.value = today;
    if (dueEl && !dueEl.value) dueEl.value = due;
    
    // Add initial item
    if (document.getElementById('invoice-items-list')?.children.length === 0) {
        addInvoiceItem();
    }
}

function addInvoiceItem() {
    const list = document.getElementById('invoice-items-list');
    const idx = invoiceItemIndex++;
    const div = document.createElement('div');
    div.className = 'invoice-item-row';
    div.innerHTML = `
        <input type="text" data-field="item-desc-${idx}" placeholder="Service/Product">
        <input type="number" data-field="item-qty-${idx}" value="1" min="1" onchange="calculateInvoiceTotal()">
        <input type="number" data-field="item-rate-${idx}" value="0" step="0.01" onchange="calculateInvoiceTotal()">
        <span class="item-amount" data-amount="${idx}">$0.00</span>
        <button class="btn-icon" onclick="removeInvoiceItem(this)"><i class="fas fa-times"></i></button>
    `;
    list.appendChild(div);
    
    // Add listeners
    div.querySelectorAll('input').forEach(inp => inp.addEventListener('input', calculateInvoiceTotal));
}

function removeInvoiceItem(btn) {
    btn.closest('.invoice-item-row').remove();
    calculateInvoiceTotal();
}

function calculateInvoiceTotal() {
    let subtotal = 0;
    document.querySelectorAll('.invoice-item-row').forEach(row => {
        const qty = parseFloat(row.querySelector('[data-field^="item-qty"]')?.value) || 0;
        const rate = parseFloat(row.querySelector('[data-field^="item-rate"]')?.value) || 0;
        const amount = qty * rate;
        subtotal += amount;
        const amountEl = row.querySelector('.item-amount');
        if (amountEl) amountEl.textContent = `$${amount.toFixed(2)}`;
    });
    
    const taxPercent = parseFloat(document.getElementById('inv-tax')?.value) || 0;
    const tax = subtotal * (taxPercent / 100);
    const total = subtotal + tax;
    
    document.getElementById('inv-subtotal').textContent = `$${subtotal.toFixed(2)}`;
    document.getElementById('inv-total').textContent = `$${total.toFixed(2)}`;
}

async function generateInvoice() {
    await loadJsPDF();
    const { jsPDF } = window.jspdf;
    const doc = new jsPDF();
    
    const margin = 20;
    const pageWidth = doc.internal.pageSize.width;
    let y = 20;
    
    // Header
    doc.setFontSize(24);
    doc.setFont('helvetica', 'bold');
    doc.text('INVOICE', margin, y);
    
    doc.setFontSize(10);
    doc.setFont('helvetica', 'normal');
    doc.text(`#${document.getElementById('inv-number')?.value || 'INV-001'}`, pageWidth - margin, y, { align: 'right' });
    y += 15;
    
    // From / To
    const fromName = document.getElementById('inv-from-name')?.value || 'Your Business';
    const fromAddr = document.getElementById('inv-from-address')?.value || '';
    const fromEmail = document.getElementById('inv-from-email')?.value || '';
    
    const toName = document.getElementById('inv-to-name')?.value || 'Client';
    const toAddr = document.getElementById('inv-to-address')?.value || '';
    const toEmail = document.getElementById('inv-to-email')?.value || '';
    
    // From
    doc.setFont('helvetica', 'bold');
    doc.text('From:', margin, y);
    doc.setFont('helvetica', 'normal');
    y += 5;
    doc.text(fromName, margin, y);
    y += 4;
    fromAddr.split('\n').forEach(line => {
        doc.text(line, margin, y);
        y += 4;
    });
    if (fromEmail) { doc.text(fromEmail, margin, y); y += 4; }
    
    // To (on right side)
    let yTo = 35;
    doc.setFont('helvetica', 'bold');
    doc.text('Bill To:', pageWidth / 2 + 10, yTo);
    doc.setFont('helvetica', 'normal');
    yTo += 5;
    doc.text(toName, pageWidth / 2 + 10, yTo);
    yTo += 4;
    toAddr.split('\n').forEach(line => {
        doc.text(line, pageWidth / 2 + 10, yTo);
        yTo += 4;
    });
    if (toEmail) doc.text(toEmail, pageWidth / 2 + 10, yTo);
    
    y = Math.max(y, yTo) + 10;
    
    // Dates
    doc.setFont('helvetica', 'bold');
    doc.text('Issue Date:', margin, y);
    doc.setFont('helvetica', 'normal');
    doc.text(document.getElementById('inv-date')?.value || '', margin + 25, y);
    doc.setFont('helvetica', 'bold');
    doc.text('Due Date:', margin + 60, y);
    doc.setFont('helvetica', 'normal');
    doc.text(document.getElementById('inv-due')?.value || '', margin + 82, y);
    y += 15;
    
    // Items table header
    doc.setFillColor(240, 240, 240);
    doc.rect(margin, y - 5, pageWidth - margin * 2, 8, 'F');
    doc.setFont('helvetica', 'bold');
    doc.text('Description', margin + 2, y);
    doc.text('Qty', margin + 95, y);
    doc.text('Rate', margin + 115, y);
    doc.text('Amount', pageWidth - margin - 25, y);
    y += 8;
    
    // Items
    doc.setFont('helvetica', 'normal');
    let subtotal = 0;
    document.querySelectorAll('.invoice-item-row').forEach(row => {
        const desc = row.querySelector('[data-field^="item-desc"]')?.value || 'Item';
        const qty = parseFloat(row.querySelector('[data-field^="item-qty"]')?.value) || 0;
        const rate = parseFloat(row.querySelector('[data-field^="item-rate"]')?.value) || 0;
        const amount = qty * rate;
        subtotal += amount;
        
        doc.text(desc.substring(0, 40), margin + 2, y);
        doc.text(qty.toString(), margin + 95, y);
        doc.text(`$${rate.toFixed(2)}`, margin + 115, y);
        doc.text(`$${amount.toFixed(2)}`, pageWidth - margin - 25, y);
        y += 6;
    });
    
    y += 10;
    
    // Totals
    const taxPercent = parseFloat(document.getElementById('inv-tax')?.value) || 0;
    const tax = subtotal * (taxPercent / 100);
    const total = subtotal + tax;
    
    doc.text('Subtotal:', margin + 100, y);
    doc.text(`$${subtotal.toFixed(2)}`, pageWidth - margin - 25, y);
    y += 6;
    doc.text(`Tax (${taxPercent}%):`, margin + 100, y);
    doc.text(`$${tax.toFixed(2)}`, pageWidth - margin - 25, y);
    y += 6;
    doc.setFont('helvetica', 'bold');
    doc.text('Total:', margin + 100, y);
    doc.text(`$${total.toFixed(2)}`, pageWidth - margin - 25, y);
    y += 15;
    
    // Notes
    const notes = document.getElementById('inv-notes')?.value;
    if (notes) {
        doc.setFont('helvetica', 'bold');
        doc.text('Notes:', margin, y);
        y += 5;
        doc.setFont('helvetica', 'normal');
        const noteLines = doc.splitTextToSize(notes, pageWidth - margin * 2);
        doc.text(noteLines, margin, y);
    }
    
    doc.save('invoice.pdf');
    showToast('Invoice PDF generated!', 'success');
}

// ══════════════════════════════════════════════════════════════════════════
// UTILITY FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════

function escapeHtml(str) {
    const div = document.createElement('div');
    div.textContent = str;
    return div.innerHTML;
}

async function loadJsPDF() {
    if (window.jspdf) return;
    return new Promise((resolve, reject) => {
        const script = document.createElement('script');
        script.src = 'https://cdnjs.cloudflare.com/ajax/libs/jspdf/2.5.1/jspdf.umd.min.js';
        script.onload = resolve;
        script.onerror = reject;
        document.head.appendChild(script);
    });
}

// ══════════════════════════════════════════════════════════════════════════
// INITIALIZATION
// ══════════════════════════════════════════════════════════════════════════

document.addEventListener('DOMContentLoaded', () => {
    initColorPaletteUpload();
    initUnixDateConverter();
    initRegexTester();
    initInvoiceGenerator();
});

// Initialize when tools are shown
const originalShowTool = window.showTool;
if (originalShowTool) {
    window.showTool = function(toolId) {
        originalShowTool(toolId);
        if (toolId === 'color-palette') initColorPaletteUpload();
        if (toolId === 'unix-date') initUnixDateConverter();
        if (toolId === 'regex-tester') initRegexTester();
        if (toolId === 'invoice-gen') initInvoiceGenerator();
    };
}
