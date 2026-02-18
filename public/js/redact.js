// ═══════════════════════════════════════════════════════════════════════════
// PRIVACY REDACTION TOOL
// ═══════════════════════════════════════════════════════════════════════════

let redactImg = null;
let redactVideo = null;
let redactRegions = [];
let redactMode = 'box'; // 'box' | 'blur'

function initRedactCanvas(file) {
    const wrap = $('redactCanvasWrap');
    const canvas = $('redactCanvas');

    if (!wrap || !canvas) return;

    redactImg = null;
    redactVideo = null;
    redactRegions = [];

    $('redactBoxBtn')?.classList.add('active');
    $('redactBlurBtn')?.classList.remove('active');
    redactMode = 'box';

    if (file.type.startsWith('image/')) {
        const img = new Image();
        img.onload = () => {
            redactImg = img;
            canvas.width = img.naturalWidth;
            canvas.height = img.naturalHeight;
            wrap.classList.remove('hidden');
            drawRedactCanvas();
            initRedactDraw(canvas);
        };
        img.onerror = () => showToast('Could not load image', 'error');
        img.src = URL.createObjectURL(file);
    } else if (file.type.startsWith('video/')) {
        const video = document.createElement('video');
        video.preload = 'auto';
        video.muted = true;
        video.src = URL.createObjectURL(file);
        video.addEventListener('seeked', () => {
            canvas.width = video.videoWidth;
            canvas.height = video.videoHeight;
            wrap.classList.remove('hidden');
            redactVideo = video;
            drawRedactCanvas();
            initRedactDraw(canvas);
        }, { once: true });
        video.addEventListener('loadedmetadata', () => { video.currentTime = 0.1; });
    }
}

function drawRedactCanvas() {
    const canvas = $('redactCanvas');

    if (!canvas) return;
    const ctx = canvas.getContext('2d');

    if (redactImg) ctx.drawImage(redactImg, 0, 0, canvas.width, canvas.height);
    else if (redactVideo) ctx.drawImage(redactVideo, 0, 0, canvas.width, canvas.height);

    for (const reg of redactRegions) {
        if (reg.type === 'box') {
            ctx.save(); ctx.globalAlpha = 1.0; ctx.fillStyle = '#111';
            ctx.fillRect(reg.x, reg.y, reg.w, reg.h); ctx.restore();
        } else if (reg.type === 'blur') {
            try {
                const imgData = ctx.getImageData(reg.x, reg.y, reg.w, reg.h);
                ctx.putImageData(blurImageData(imgData, 10), reg.x, reg.y);
            } catch {}
        }
    }
}

function initRedactDraw(canvas) {
    let drawing = false, startX, startY;
    const scaleX = () => canvas.width / canvas.getBoundingClientRect().width;
    const scaleY = () => canvas.height / canvas.getBoundingClientRect().height;
    function getPos(e) {
        const rect = canvas.getBoundingClientRect();
        const cx = (e.clientX ?? e.touches?.[0]?.clientX ?? 0) - rect.left;
        const cy = (e.clientY ?? e.touches?.[0]?.clientY ?? 0) - rect.top;
        return { x: cx * scaleX(), y: cy * scaleY() };
    }

    canvas.onmousedown = canvas.ontouchstart = e => { e.preventDefault(); drawing = true; const p = getPos(e); startX = p.x; startY = p.y; };
    canvas.onmousemove = canvas.ontouchmove = e => {
        if (!drawing) return; e.preventDefault();
        const p = getPos(e);
        drawRedactCanvas();
        const ctx = canvas.getContext('2d');
        ctx.save(); ctx.strokeStyle = redactMode === 'box' ? '#fff' : '#7c5cff';
        ctx.lineWidth = 2; ctx.setLineDash([6, 4]);
        ctx.strokeRect(startX, startY, p.x - startX, p.y - startY); ctx.restore();
    };
    window.onmouseup = canvas.ontouchend = e => {
        if (!drawing) return; drawing = false;
        const p = getPos(e);
        const x = Math.round(Math.min(startX, p.x)), y = Math.round(Math.min(startY, p.y));
        const w = Math.round(Math.abs(p.x - startX)), h = Math.round(Math.abs(p.y - startY));

        if (w < 4 || h < 4) return;
        redactRegions.push({ type: redactMode, x, y, w, h });
        drawRedactCanvas();
    };
}

function blurImageData(imgData, radius) {
    const { data, width, height } = imgData;
    const out = new Uint8ClampedArray(data);

    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            let r=0,g=0,b=0,a=0,n=0;

            for (let dy = -radius; dy <= radius; dy++) {
                for (let dx = -radius; dx <= radius; dx++) {
                    const nx = x+dx, ny = y+dy;

                    if (nx>=0 && nx<width && ny>=0 && ny<height) {
                        const i=(ny*width+nx)*4; r+=data[i];g+=data[i+1];b+=data[i+2];a+=data[i+3];n++;
                    }
                }
            }

            const i=(y*width+x)*4; out[i]=r/n;out[i+1]=g/n;out[i+2]=b/n;out[i+3]=a/n;
        }
    }

    return new ImageData(out, width, height);
}

document.addEventListener('DOMContentLoaded', () => {
    $('redactBoxBtn')?.addEventListener('click', () => {
        redactMode = 'box';
        $('redactBoxBtn').classList.add('active');
        $('redactBlurBtn').classList.remove('active');
    });
    $('redactBlurBtn')?.addEventListener('click', () => {
        redactMode = 'blur';
        $('redactBlurBtn').classList.add('active');
        $('redactBoxBtn').classList.remove('active');
    });
    $('redactUndoBtn')?.addEventListener('click', () => { redactRegions.pop(); drawRedactCanvas(); });
    $('redactClearBtn')?.addEventListener('click', () => { redactRegions = []; drawRedactCanvas(); });
});
