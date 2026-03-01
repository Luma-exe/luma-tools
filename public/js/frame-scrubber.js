/**
 * FrameScrubber — visual frame picker for video & GIF tools
 *
 * Modes:
 *   'select' — single-frame pick  (video-frame extract)
 *   'multi'  — multi-frame toggle (gif-frame-remove)
 *
 * Usage:
 *   FrameScrubber.init(toolId, file)   — called after a file is selected
 *   FrameScrubber.destroy(toolId)      — called when a file is removed
 *   FrameScrubber.clearSelection(toolId)
 */
const FrameScrubber = (() => {
    const THUMB_W       = 88;
    const MAX_VIDEO     = 60;   // max thumbnails for video
    const MAX_GIF       = 200;  // max frames for GIF

    // keyed by toolId → { thumbs, mode, selected, isGif }
    const _state = {};

    /* ── Public API ─────────────────────────────────────────────────── */

    async function init(toolId, file) {
        destroy(toolId);

        const isGif = file.type === 'image/gif' || file.name.toLowerCase().endsWith('.gif');
        const mode  = toolId === 'video-frame' ? 'select' : 'multi';

        const panel = document.getElementById('tool-' + toolId);
        if (!panel) return;
        const insertAfter = panel.querySelector('.file-preview[data-tool="' + toolId + '"]');
        if (!insertAfter) return;

        // Hide manual input sections while scrubber is active
        _setManualVisibility(toolId, false);

        // Build shell UI
        const wrap = document.createElement('div');
        wrap.className = 'frame-scrubber';
        wrap.id = 'fscrub-' + toolId;
        wrap.dataset.mode = mode;
        wrap.innerHTML = `
            <div class="fscrub-header">
                <span class="fscrub-title"><i class="fas fa-film"></i> <span class="fscrub-count">Loading…</span></span>
                <div class="fscrub-header-right">
                    <span class="fscrub-sel-tag"></span>
                    ${mode === 'multi' ? `
                        <button class="fscrub-action-btn" onclick="FrameScrubber.selectAll('${toolId}')">All</button>
                        <button class="fscrub-action-btn" onclick="FrameScrubber.clearSelection('${toolId}')">Clear</button>
                    ` : ''}
                </div>
            </div>
            <div class="fscrub-body">
                <div class="fscrub-loading"><i class="fas fa-circle-notch fa-spin"></i> Generating previews…</div>
                <div class="fscrub-strip hidden"></div>
            </div>
        `;
        insertAfter.after(wrap);

        // Generate thumbnails
        let thumbs = null;
        try {
            thumbs = isGif ? await _genGif(file) : await _genVideo(file);
        } catch (_) { /* fall through to fallback */ }

        if (!thumbs || thumbs.length === 0) {
            _setManualVisibility(toolId, true);
            wrap.remove();
            return;
        }

        _state[toolId] = {
            thumbs,
            mode,
            isGif,
            selected: mode === 'select' ? null : new Set()
        };

        _renderStrip(toolId, wrap);
    }

    function destroy(toolId) {
        const el = document.getElementById('fscrub-' + toolId);
        if (el) el.remove();
        delete _state[toolId];
        _setManualVisibility(toolId, true);
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

    /* ── Private ─────────────────────────────────────────────────────── */

    function _renderStrip(toolId, wrap) {
        const s = _state[toolId];
        const loading = wrap.querySelector('.fscrub-loading');
        const strip   = wrap.querySelector('.fscrub-strip');
        const countEl = wrap.querySelector('.fscrub-count');

        const total = s.thumbs.length;
        countEl.textContent = total + (total === 1 ? ' frame' : ' frames');

        s.thumbs.forEach((t, i) => {
            const div  = document.createElement('div');
            div.className = 'fscrub-thumb';
            div.dataset.idx = i;
            const label = s.isGif ? t.frameIndex : _fmtLabel(t.timestamp);
            div.innerHTML = `<img src="${t.thumbnail}" loading="lazy" draggable="false"><span class="fscrub-label">${label}</span><div class="fscrub-x"><i class="fas fa-times"></i></div>`;
            div.addEventListener('click', () => _onClickThumb(toolId, i));
            strip.appendChild(div);
        });

        loading.classList.add('hidden');
        strip.classList.remove('hidden');

        // Auto-select first frame in select mode
        if (s.mode === 'select') {
            _onClickThumb(toolId, 0);
        }
    }

    function _onClickThumb(toolId, idx) {
        const s = _state[toolId];
        if (!s) return;
        const wrap = document.getElementById('fscrub-' + toolId);
        if (!wrap) return;
        const thumbEls = wrap.querySelectorAll('.fscrub-thumb');

        if (s.mode === 'select') {
            thumbEls.forEach(el => el.classList.remove('fscrub-on'));
            thumbEls[idx].classList.add('fscrub-on');
            s.selected = idx;
            // Scroll selected thumb into view
            thumbEls[idx].scrollIntoView({ behavior: 'smooth', block: 'nearest', inline: 'nearest' });
            _syncInput(toolId);
            _updateTag(toolId);
        } else {
            if (s.selected.has(idx)) {
                s.selected.delete(idx);
                thumbEls[idx].classList.remove('fscrub-on');
            } else {
                s.selected.add(idx);
                thumbEls[idx].classList.add('fscrub-on');
            }
            _syncInput(toolId);
            _updateTag(toolId);
        }
    }

    function _syncInput(toolId) {
        const s = _state[toolId];
        if (!s) return;

        if (toolId === 'video-frame') {
            if (s.selected === null) return;
            const t = s.thumbs[s.selected];
            if (s.isGif) {
                const fi = document.getElementById('frameIndex');
                if (fi) fi.value = t.frameIndex;
            } else {
                const fts = document.getElementById('frameTimestamp');
                if (fts) fts.value = _fmtTimestamp(t.timestamp);
            }
        } else if (toolId === 'gif-frame-remove') {
            const input = document.getElementById('removeFrames');
            if (!input) return;
            const frames = [...s.selected]
                .map(i => s.thumbs[i].frameIndex)
                .sort((a, b) => a - b);
            input.value = frames.join(', ');
        }
    }

    function _updateTag(toolId) {
        const s    = _state[toolId];
        const wrap = document.getElementById('fscrub-' + toolId);
        if (!s || !wrap) return;
        const tag = wrap.querySelector('.fscrub-sel-tag');
        if (!tag) return;

        if (s.mode === 'select') {
            if (s.selected !== null) {
                const t = s.thumbs[s.selected];
                tag.textContent = s.isGif ? `Frame ${t.frameIndex}` : _fmtTimestamp(t.timestamp);
                tag.style.display = 'inline-block';
            }
        } else {
            const n = s.selected.size;
            if (n === 0) {
                tag.textContent = '';
                tag.style.display = 'none';
            } else {
                tag.textContent = n + ' selected';
                tag.style.display = 'inline-block';
            }
        }
    }

    function _setManualVisibility(toolId, visible) {
        if (toolId === 'video-frame') {
            // Both hid/shown depending on file type — scrubber overrides both to hidden
            const tsOpt  = document.getElementById('frameTimestampOption');
            const idxOpt = document.getElementById('frameIndexOption');
            if (!visible) {
                // Only mark as scrub-hidden if currently visible (preserve file-type toggle state)
                if (tsOpt  && !tsOpt.classList.contains('hidden'))  { tsOpt.dataset.scrubHidden  = '1'; tsOpt.classList.add('hidden'); }
                if (idxOpt && !idxOpt.classList.contains('hidden')) { idxOpt.dataset.scrubHidden = '1'; idxOpt.classList.add('hidden'); }
            } else {
                // restore to whatever the file-type logic set (leave them hidden unless explicitly restored)
                if (tsOpt  && tsOpt.dataset.scrubHidden)  { tsOpt.classList.remove('hidden');  delete tsOpt.dataset.scrubHidden; }
                if (idxOpt && idxOpt.dataset.scrubHidden) { idxOpt.classList.remove('hidden'); delete idxOpt.dataset.scrubHidden; }
            }
        } else if (toolId === 'gif-frame-remove') {
            const opts = document.querySelector('#tool-gif-frame-remove .tool-options');
            if (opts) opts.classList.toggle('hidden', !visible);
        }
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
                    thumbs.push({
                        thumbnail: canvas.toDataURL('image/jpeg', 0.72),
                        timestamp: t,
                        frameIndex: Math.round(t * 30)  // approx at 30 fps
                    });
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
            const onSeeked = () => resolve();
            vid.addEventListener('seeked', onSeeked, { once: true });
            vid.currentTime = time;
        });
    }

    async function _genGif(file) {
        if (!('ImageDecoder' in window)) return null;

        const buf     = await file.arrayBuffer();
        const decoder = new ImageDecoder({ data: buf, type: 'image/gif' });
        await decoder.tracks.ready;

        const track = decoder.tracks.selectedTrack;
        const total = track.frameCount;
        if (!total || total <= 0) { decoder.close(); return null; }

        const toProcess = Math.min(total, MAX_GIF);
        const thumbs    = [];
        const canvas    = document.createElement('canvas');
        const ctx       = canvas.getContext('2d');
        canvas.width    = THUMB_W;

        for (let i = 0; i < toProcess; i++) {
            const { image } = await decoder.decode({ frameIndex: i });
            canvas.height   = image.height > 0
                ? Math.round(THUMB_W * image.height / image.width)
                : THUMB_W;
            ctx.clearRect(0, 0, canvas.width, canvas.height);
            ctx.drawImage(image, 0, 0, canvas.width, canvas.height);
            image.close();
            thumbs.push({ thumbnail: canvas.toDataURL('image/jpeg', 0.72), frameIndex: i });
        }

        decoder.close();
        return thumbs;
    }

    /* ── Formatting helpers ──────────────────────────────────────────── */

    function _fmtLabel(secs) {
        if (!isFinite(secs)) return '–';
        if (secs >= 60) {
            const m = Math.floor(secs / 60);
            const s = (secs % 60).toFixed(1);
            return m + ':' + (parseFloat(s) < 10 ? '0' : '') + s;
        }
        return secs.toFixed(1) + 's';
    }

    function _fmtTimestamp(secs) {
        if (!isFinite(secs)) return '00:00:00';
        const h   = Math.floor(secs / 3600);
        const m   = Math.floor((secs % 3600) / 60);
        const s   = secs % 60;
        return String(h).padStart(2, '0') + ':' +
               String(m).padStart(2, '0') + ':' +
               s.toFixed(3).padStart(6, '0');
    }

    return { init, destroy, clearSelection, selectAll };
})();
