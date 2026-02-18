// ═══════════════════════════════════════════════════════════════════════════
// SERVER HEALTH CHECK & STATUS TICKER
// ═══════════════════════════════════════════════════════════════════════════

async function checkServerHealth() {
    const tickerTrack = $('tickerTrack');

    if (!tickerTrack) return;
    try {
        const res = await fetch('/api/health');
        const data = await res.json();

        if (data.status === 'ok') {
            const items = [];
            items.push({ label: 'Server',      value: data.server || 'Online',                                   ok: true });
            items.push({ label: 'FFmpeg',       value: data.ffmpeg_available ? 'Available' : 'Missing',           ok: data.ffmpeg_available });
            items.push({ label: 'yt-dlp',       value: data.yt_dlp_available ? ('v' + data.yt_dlp_version) : 'Missing', ok: data.yt_dlp_available });
            items.push({ label: 'Ghostscript',  value: data.ghostscript_available ? 'Available' : 'Not Found',   ok: data.ghostscript_available });

            if (data.git_commit && data.git_commit !== 'unknown') {
                const branch = data.git_branch && data.git_branch !== 'unknown' ? data.git_branch : '';
                items.push({ label: 'Version', value: branch ? `${branch}@${data.git_commit}` : data.git_commit, ver: true });
            }

            const renderItems = (arr) => arr.map(i =>
                `<span class="ticker-item"><span class="ticker-label">${i.label}:</span> ` +
                `<span class="${i.ver ? 'ticker-ver' : (i.ok ? 'ticker-ok' : 'ticker-err')}">${i.value}</span></span>`
            ).join('<span class="ticker-sep">•</span>');

            const once = renderItems(items);
            tickerTrack.innerHTML = once + '<span class="ticker-sep">│</span>' + once + '<span class="ticker-sep">│</span>';
            initTickerDrag(tickerTrack);
        } else {
            tickerTrack.innerHTML = '<span class="status-text">Server error</span>';
        }
    } catch {
        tickerTrack.innerHTML = '<span class="status-text">Server offline</span>';
    }
}

// ── Ticker drag-to-scroll ─────────────────────────────────────────────────

function initTickerDrag(track) {
    if (track._dragInit) return;
    track._dragInit = true;

    let isDragging = false, startX = 0, scrollOffset = 0;
    const DURATION = 40; // must match CSS animation-duration

    function getCurrentTranslateX() {
        const matrix = new DOMMatrix(getComputedStyle(track).transform);
        return matrix.m41;
    }

    function onPointerDown(e) {
        isDragging = true; startX = e.clientX;
        scrollOffset = getCurrentTranslateX();
        track.style.animation = 'none';
        track.style.transform = `translateX(${scrollOffset}px)`;
        track.classList.add('dragging');
        track.setPointerCapture(e.pointerId);
        e.preventDefault();
    }

    function onPointerMove(e) {
        if (!isDragging) return;
        track.style.transform = `translateX(${scrollOffset + (e.clientX - startX)}px)`;
    }

    function onPointerUp() {
        if (!isDragging) return;
        isDragging = false;
        track.classList.remove('dragging');
        const currentX = getCurrentTranslateX();
        const totalWidth = track.scrollWidth / 2;
        let progress = ((-currentX) % totalWidth) / totalWidth;

        if (progress < 0) progress += 1;
        if (isNaN(progress)) progress = 0;
        track.style.transform = '';
        track.offsetHeight; // force reflow
        track.style.animation = `ticker-scroll ${DURATION}s linear infinite`;
        track.style.animationDelay = `-${progress * DURATION}s`;
    }

    track.addEventListener('pointerdown', onPointerDown);
    track.addEventListener('pointermove', onPointerMove);
    track.addEventListener('pointerup', onPointerUp);
    track.addEventListener('pointercancel', onPointerUp);
}
