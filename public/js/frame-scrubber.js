/**
 * FrameScrubber — visual frame picker for video & GIF tools
 *
 * Modes:
 *   'select' — single-frame pick  (video-frame extract)
 *   'multi'  — multi-frame toggle (gif-frame-remove)
 *
 * GIF frames are fully composited via ImageDecoder + disposal-method parsing
 * from raw GIF bytes (Graphic Control Extension blocks), so thumbnails show
 * exactly what each frame looks like in a GIF viewer.
 */
const FrameScrubber = (() => {
    const THUMB_W   = 112;  // thumbnail render width (px)
    const MAX_VIDEO = 60;
    const MAX_GIF   = 300;

    // Singleton hover popup
    let _popup = null;
    function _ensurePopup() {
        if (_popup) return;
        _popup = document.createElement('div');
        _popup.className = 'fscrub-popup';
        _popup.innerHTML = '<img class="fscrub-popup-img" draggable="false"><span class="fscrub-popup-lbl"></span>';
        document.body.appendChild(_popup);
    }

    // keyed by toolId
    const _state = {};

    /* ── Public API ──────────────────────────────────────────────────── */

    async function init(toolId, file) {
        destroy(toolId);
        _ensurePopup();

        const isGif = file.type === 'image/gif' || file.name.toLowerCase().endsWith('.gif');
        const mode  = toolId === 'video-frame' ? 'select' : 'multi';

        const panel = document.getElementById('tool-' + toolId);
        if (!panel) return;
        const anchor = panel.querySelector('.file-preview[data-tool="' + toolId + '"]');
        if (!anchor) return;

        const wrap = document.createElement('div');
        wrap.className  = 'frame-scrubber';
        wrap.id         = 'fscrub-' + toolId;
        wrap.dataset.mode = mode;
        wrap.innerHTML  = `
            <div class="fscrub-header">
                <span class="fscrub-title"><i class="fas fa-film"></i><span class="fscrub-count"> Loading…</span></span>
                <div class="fscrub-hdr-right">
                    <span class="fscrub-sel-tag" style="display:none"></span>
                    ${mode === 'multi' ? `
                        <button class="fscrub-btn" onclick="FrameScrubber.selectAll('${toolId}')">Select all</button>
                        <button class="fscrub-btn" onclick="FrameScrubber.clearSelection('${toolId}')">Clear</button>
                    ` : `<span class="fscrub-hint">Click a frame to select it</span>`}
                </div>
            </div>
            <div class="fscrub-body">
                <div class="fscrub-loading"><i class="fas fa-circle-notch fa-spin"></i> Generating previews…</div>
                <div class="fscrub-strip fscrub-hidden"></div>
            </div>`;
        anchor.after(wrap);

        let thumbs = null;
        try {
            thumbs = isGif ? await _genGif(file) : await _genVideo(file);
        } catch (_) { /* fallthrough */ }

        if (!thumbs || thumbs.length === 0) { wrap.remove(); return; }

        _state[toolId] = { thumbs, mode, isGif, selected: mode === 'select' ? null : new Set() };
        _renderStrip(toolId, wrap);
    }

    function destroy(toolId) {
        const el = document.getElementById('fscrub-' + toolId);
        if (el) el.remove();
        delete _state[toolId];
        if (_popup) _popup.classList.remove('fscrub-popup-visible');
    }

    function clearSelection(toolId) {
        const s = _state[toolId];
        if (!s || s.mode !== 'multi') return;
        s.selected.clear();
        const wrap = document.getElementById('fscrub-' + toolId);
        if (wrap) wrap.querySelectorAll('.fscrub-thumb.fscrub-on').forEach(el => el.classList.remove('fscrub-on'));
        _syncInput(toolId);
        _updateTag(toolId);
    }

    function selectAll(toolId) {
        const s = _state[toolId];
        if (!s || s.mode !== 'multi') return;
        s.thumbs.forEach((_, i) => s.selected.add(i));
        const wrap = document.getElementById('fscrub-' + toolId);
        if (wrap) wrap.querySelectorAll('.fscrub-thumb').forEach(el => el.classList.add('fscrub-on'));
        _syncInput(toolId);
        _updateTag(toolId);
    }

    /* ── Private: rendering ──────────────────────────────────────────── */

    function _renderStrip(toolId, wrap) {
        const s       = _state[toolId];
        const loading = wrap.querySelector('.fscrub-loading');
        const strip   = wrap.querySelector('.fscrub-strip');
        const countEl = wrap.querySelector('.fscrub-count');
        const total   = s.thumbs.length;

        countEl.textContent = ' ' + total + (total === 1 ? ' frame' : ' frames');

        s.thumbs.forEach((t, i) => {
            const div = document.createElement('div');
            div.className   = 'fscrub-thumb';
            div.dataset.idx = i;
            const lbl = s.isGif ? ('Frame ' + t.frameIndex) : _fmtLabel(t.timestamp);
            div.innerHTML = `
                <div class="fscrub-img-wrap">
                    <img src="${t.thumbnail}" draggable="false">
                    <div class="fscrub-overlay-x"><i class="fas fa-times"></i></div>
                    <div class="fscrub-overlay-check"><i class="fas fa-check"></i></div>
                </div>
                <span class="fscrub-lbl">${lbl}</span>`;
            div.addEventListener('click', () => _onClick(toolId, i));
            div.addEventListener('mouseenter', e => _onHover(t, lbl, e));
            div.addEventListener('mouseleave', _hidePopup);
            strip.appendChild(div);
        });

        loading.classList.add('fscrub-hidden');
        strip.classList.remove('fscrub-hidden');

        if (s.mode === 'select') _onClick(toolId, 0);
    }

    function _onClick(toolId, idx) {
        const s = _state[toolId];
        if (!s) return;
        const thumbEls = _getThumbEls(toolId);
        if (!thumbEls.length) return;

        if (s.mode === 'select') {
            thumbEls.forEach(el => el.classList.remove('fscrub-on'));
            thumbEls[idx].classList.add('fscrub-on');
            s.selected = idx;
            thumbEls[idx].scrollIntoView({ behavior: 'smooth', block: 'nearest', inline: 'center' });
        } else {
            if (s.selected.has(idx)) { s.selected.delete(idx); thumbEls[idx].classList.remove('fscrub-on'); }
            else                     { s.selected.add(idx);    thumbEls[idx].classList.add('fscrub-on'); }
        }
        _syncInput(toolId);
        _updateTag(toolId);
    }

    function _onHover(t, lbl, e) {
        if (!_popup) return;
        _popup.querySelector('.fscrub-popup-img').src = t.thumbnail;
        _popup.querySelector('.fscrub-popup-lbl').textContent = lbl;
        _popup.classList.add('fscrub-popup-visible');
        _positionPopup(e.currentTarget);
    }

    function _hidePopup() {
        if (_popup) _popup.classList.remove('fscrub-popup-visible');
    }

    function _positionPopup(thumbEl) {
        if (!_popup) return;
        const r    = thumbEl.getBoundingClientRect();
        const pw   = 200;
        let   left = r.left + r.width / 2 - pw / 2;
        left = Math.max(8, Math.min(left, window.innerWidth - pw - 8));
        const top  = r.top + window.scrollY - _popup.offsetHeight - 8;
        _popup.style.left = left + 'px';
        _popup.style.top  = Math.max(8, top) + 'px';
    }

    function _syncInput(toolId) {
        const s = _state[toolId];
        if (!s) return;
        if (toolId === 'video-frame') {
            if (s.selected === null) return;
            const t = s.thumbs[s.selected];
            if (s.isGif) {
                const el = document.getElementById('frameIndex');
                if (el) el.value = t.frameIndex;
            } else {
                const el = document.getElementById('frameTimestamp');
                if (el) el.value = _fmtTimestamp(t.timestamp);
            }
        } else if (toolId === 'gif-frame-remove') {
            const el = document.getElementById('removeFrames');
            if (!el) return;
            el.value = [...s.selected].map(i => s.thumbs[i].frameIndex).sort((a, b) => a - b).join(', ');
        }
    }

    function _updateTag(toolId) {
        const s    = _state[toolId];
        const wrap = document.getElementById('fscrub-' + toolId);
        if (!s || !wrap) return;
        const tag  = wrap.querySelector('.fscrub-sel-tag');
        if (!tag) return;
        if (s.mode === 'select') {
            if (s.selected !== null) {
                const t = s.thumbs[s.selected];
                tag.textContent = s.isGif ? ('Frame ' + t.frameIndex) : _fmtTimestamp(t.timestamp);
                tag.style.display = 'inline-block';
            }
        } else {
            const n = s.selected.size;
            if (n === 0) { tag.textContent = ''; tag.style.display = 'none'; }
            else         { tag.textContent = n + ' selected'; tag.style.display = 'inline-block'; }
        }
    }

    function _getThumbEls(toolId) {
        const wrap = document.getElementById('fscrub-' + toolId);
        return wrap ? [...wrap.querySelectorAll('.fscrub-thumb')] : [];
    }

    /* ── Thumbnail generators ─────────────────────────────────────────── */

    async function _genVideo(file) {
        return new Promise((resolve, reject) => {
            const url = URL.createObjectURL(file);
            const vid = document.createElement('video');
            vid.muted   = true;
            vid.preload = 'metadata';

            vid.addEventListener('error', () => { URL.revokeObjectURL(url); reject(new Error('video load error')); }, { once: true });

            vid.addEventListener('loadedmetadata', async () => {
                const dur = vid.duration;
                if (!isFinite(dur) || dur <= 0) {
                    URL.revokeObjectURL(url);
                    reject(new Error('bad duration'));
                    return;
                }

                const count    = Math.min(MAX_VIDEO, Math.max(8, Math.ceil(dur * 2)));
                const interval = dur / Math.max(count - 1, 1);
                const thumbs   = [];
                const canvas   = document.createElement('canvas');
                const ctx      = canvas.getContext('2d');
                canvas.width   = THUMB_W;

                for (let i = 0; i < count; i++) {
                    const t = i === count - 1 ? Math.max(0, dur - 0.05) : i * interval;
                    await _seekTo(vid, t);
                    canvas.height = vid.videoHeight > 0
                        ? Math.round(THUMB_W * vid.videoHeight / vid.videoWidth)
                        : Math.round(THUMB_W * 9 / 16);
                    ctx.drawImage(vid, 0, 0, canvas.width, canvas.height);
                    thumbs.push({ thumbnail: canvas.toDataURL('image/jpeg', 0.85), timestamp: t });
                }

                vid.src = '';
                URL.revokeObjectURL(url);
                resolve(thumbs);
            }, { once: true });

            vid.src = url;
        });
    }

    function _seekTo(vid, time) {
        return new Promise(resolve => {
            vid.addEventListener('seeked', resolve, { once: true });
            vid.currentTime = time;
        });
    }

    /* ── GIF with proper compositing + disposal handling ─────────────── */

    async function _genGif(file) {
        if (!('ImageDecoder' in window)) return null;

        const buf       = await file.arrayBuffer();
        const disposals = _readGifDisposals(buf);  // parse disposal methods from raw bytes

        const decoder = new ImageDecoder({ data: buf.slice(0), type: 'image/gif' });
        await decoder.tracks.ready;

        const track = decoder.tracks.selectedTrack;
        const total = track ? track.frameCount : 0;
        if (!total || total <= 0) { decoder.close(); return null; }

        const toProcess = Math.min(total, MAX_GIF);

        // Decode frame 0 to get logical-screen dimensions
        const { image: firstImg } = await decoder.decode({ frameIndex: 0 });
        const W = firstImg.displayWidth;
        const H = firstImg.displayHeight;
        firstImg.close();
        if (!W || !H) { decoder.close(); return null; }

        // Persistent compositor canvas — accumulates frames exactly as a GIF viewer would
        const comp  = new OffscreenCanvas(W, H);
        const cctx  = comp.getContext('2d');

        // Thumbnail output canvas
        const thumbH = Math.round(THUMB_W * H / W) || THUMB_W;
        const tcanv  = document.createElement('canvas');
        tcanv.width  = THUMB_W;
        tcanv.height = thumbH;
        const tctx   = tcanv.getContext('2d');

        const thumbs    = [];
        let   prevRect  = null;      // bounding rect of prev frame on compositor
        let   savedPx   = null;      // ImageData snapshot for disposal=3

        for (let i = 0; i < toProcess; i++) {
            const { image } = await decoder.decode({ frameIndex: i });
            const vr       = image.visibleRect;
            const fx = vr.x, fy = vr.y, fw = vr.width || W, fh = vr.height || H;
            const disposal = disposals[i] ?? 1;

            // Apply PREVIOUS frame's disposal before drawing this frame
            if (i > 0 && prevRect) {
                const pd = disposals[i - 1] ?? 1;
                if (pd === 2) {
                    // Restore to background (transparent)
                    cctx.clearRect(prevRect.x, prevRect.y, prevRect.w, prevRect.h);
                } else if (pd === 3 && savedPx) {
                    // Restore to pre-previous-frame content
                    cctx.putImageData(savedPx, prevRect.x, prevRect.y);
                    savedPx = null;
                }
            }

            // Save snapshot of region if THIS frame's disposal will restore it later
            if (disposal === 3) {
                savedPx = cctx.getImageData(fx, fy, fw, fh);
            }

            cctx.drawImage(image, fx, fy, fw, fh);
            prevRect = { x: fx, y: fy, w: fw, h: fh };
            image.close();

            // Capture thumbnail of composited state
            tctx.clearRect(0, 0, THUMB_W, thumbH);
            tctx.drawImage(comp, 0, 0, THUMB_W, thumbH);
            thumbs.push({ thumbnail: tcanv.toDataURL('image/jpeg', 0.85), frameIndex: i });
        }

        decoder.close();
        return thumbs;
    }

    /**
     * Parse Graphic Control Extension blocks from raw GIF bytes.
     * GCE: 0x21 0xF9 0x04 [packed] [dly_lo] [dly_hi] [tci] 0x00
     * disposal = (packed >> 3) & 0x07
     */
    function _readGifDisposals(buf) {
        const bytes     = new Uint8Array(buf);
        const disposals = [];
        for (let i = 0; i < bytes.length - 7; i++) {
            if (bytes[i] === 0x21 && bytes[i + 1] === 0xF9 && bytes[i + 2] === 0x04) {
                disposals.push((bytes[i + 3] >> 3) & 0x07);
                i += 7;
            }
        }
        return disposals;
    }

    /* ── Formatting helpers ──────────────────────────────────────────── */

    function _fmtLabel(secs) {
        if (!isFinite(secs)) return '–';
        if (secs >= 60) {
            const m = Math.floor(secs / 60);
            const s = secs % 60;
            return m + ':' + (s < 10 ? '0' : '') + s.toFixed(0);
        }
        return secs.toFixed(2) + 's';
    }

    function _fmtTimestamp(secs) {
        if (!isFinite(secs)) return '00:00:00';
        const h = Math.floor(secs / 3600);
        const m = Math.floor((secs % 3600) / 60);
        const s = secs % 60;
        return String(h).padStart(2, '0') + ':' +
               String(m).padStart(2, '0') + ':' +
               s.toFixed(3).padStart(6, '0');
    }

    return { init, destroy, clearSelection, selectAll };
})();
