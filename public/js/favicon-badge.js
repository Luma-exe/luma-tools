// ═══════════════════════════════════════════════════════════════════════════
// FAVICON BADGE — draws a small counter pip on the tab icon when work is
// in progress. Watches batchQueue and active download state.
// ═══════════════════════════════════════════════════════════════════════════
(function () {
    const SIZE = 64;
    const ORIGINAL = '/favicon.svg';
    let lastN = -1;
    let lastTitle = '';

    function loadOriginalImage() {
        return new Promise((resolve) => {
            const img = new Image();
            img.crossOrigin = 'anonymous';
            img.onload  = () => resolve(img);
            img.onerror = () => resolve(null);
            img.src = ORIGINAL;
        });
    }

    let baseImg = null;
    let baseReady = loadOriginalImage().then(i => { baseImg = i; });

    function setLink(href) {
        let link = document.querySelector("link[rel~='icon']");
        if (!link) {
            link = document.createElement('link');
            link.rel = 'icon';
            document.head.appendChild(link);
        }
        link.type = href.endsWith('.svg') ? 'image/svg+xml' : 'image/png';
        link.href = href;
    }

    async function render(n) {
        if (n === lastN) return;
        lastN = n;
        await baseReady;
        if (n <= 0 || !baseImg) {
            setLink(ORIGINAL);
            if (lastTitle) { document.title = lastTitle; lastTitle = ''; }
            return;
        }
        const canvas = document.createElement('canvas');
        canvas.width = SIZE; canvas.height = SIZE;
        const ctx = canvas.getContext('2d');
        ctx.drawImage(baseImg, 0, 0, SIZE, SIZE);
        // pip
        const r = 22;
        const cx = SIZE - r + 2, cy = SIZE - r + 2;
        ctx.fillStyle = '#ff3b5c';
        ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2); ctx.fill();
        ctx.strokeStyle = 'rgba(0,0,0,.45)';
        ctx.lineWidth = 2;
        ctx.stroke();
        // text
        const label = n > 9 ? '9+' : String(n);
        ctx.fillStyle = '#fff';
        ctx.font = 'bold ' + (label.length === 1 ? 32 : 24) + 'px -apple-system, Segoe UI, Roboto, sans-serif';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(label, cx, cy + 1);
        setLink(canvas.toDataURL('image/png'));
        // tab title
        if (!lastTitle) lastTitle = document.title.replace(/^\(\d+\+?\)\s*/, '');
        const baseTitle = (lastTitle || document.title).replace(/^\(\d+\+?\)\s*/, '');
        document.title = '(' + label + ') ' + baseTitle;
    }

    function countActive() {
        let n = 0;
        try {
            // Batch queue
            if (window.batchQueue && window.batchQueue.running) {
                const total = window.batchQueue.total || 0;
                const done  = window.batchQueue.current || 0;
                n += Math.max(0, total - done);
            }
            // Active media download (tracked by ui state if exposed)
            if (window.state && window.state.activeDownloads instanceof Set) {
                n += window.state.activeDownloads.size;
            }
            // Generic: any visible .progress-bar or batch-overlay
            const ov = document.getElementById('batchOverlay');
            if (ov && !ov.classList.contains('hidden') && n === 0) n = 1;
        } catch {}
        return n;
    }

    setInterval(() => render(countActive()), 1200);
    document.addEventListener('DOMContentLoaded', () => render(countActive()));
})();
