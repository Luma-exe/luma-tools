// ═══════════════════════════════════════════════════════════════════════════
// IMAGE CROP TOOL
// ═══════════════════════════════════════════════════════════════════════════

let cropImg = null;
let cropRatio = 'free';

function initCropCanvas(file) {
    const wrap = $('cropCanvasWrap');
    const canvas = $('cropCanvas');

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
        const p = getPos(e); startX = p.x; startY = p.y;
    }

    function onMove(e) {
        if (!dragging) return;
        e.preventDefault();
        const p = getPos(e);
        let x = Math.min(startX, p.x), y = Math.min(startY, p.y);
        let w = Math.abs(p.x - startX), h = Math.abs(p.y - startY);

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
    const sel = $('cropSelection');

    if (!sel) return;
    sel.style.left = x + 'px'; sel.style.top = y + 'px';
    sel.style.width = w + 'px'; sel.style.height = h + 'px';
}
