// ═══════════════════════════════════════════════════════════════════════════
// AUDIO/VIDEO WAVEFORM VISUALIZATION — WaveSurfer v7
// ═══════════════════════════════════════════════════════════════════════════

let waveSurfers = {};

function initWaveform(toolId, file) {
    if (waveSurfers[toolId]) { try { waveSurfers[toolId].destroy(); } catch(e) {} waveSurfers[toolId] = null; }

    let wrap, el;

    if (toolId === 'audio-convert') {
        wrap = $('waveform-audio-convert'); el = $('waveformAudioConvert');
    } else if (toolId === 'video-extract-audio') {
        wrap = $('waveform-video-extract-audio'); el = $('waveformVideoExtract');
    } else if (toolId === 'video-trim') {
        wrap = $('waveform-video-trim'); el = $('waveformVideoTrim');
    }

    if (!wrap || !el) return;

    wrap.style.display = '';
    el.innerHTML = '';

    let ws;

    try {
        ws = WaveSurfer.create({
            container: el,
            waveColor: '#7c5cff',
            progressColor: '#6366f1',
            height: 80,
            barWidth: 2,
            barGap: 1,
            cursorColor: '#a78bfa',
        });
    } catch (e) {
        console.error('WaveSurfer create failed:', e);
        wrap.style.display = 'none';
        return;
    }

    waveSurfers[toolId] = ws;

    const url = URL.createObjectURL(file);
    ws.load(url).catch(err => {
        console.error('WaveSurfer load error:', err);
        wrap.style.display = 'none';
    });

    if (toolId === 'video-trim') {
        ws.on('ready', () => {
            const dur = ws.getDuration();
            const startEl = $('waveformTrimStart');
            const endEl = $('waveformTrimEnd');

            if (startEl) startEl.textContent = '00:00:00.00';
            if (endEl) endEl.textContent = formatTime(dur);
            ws.on('seek', progress => {
                const t = formatTime(progress * dur);

                if ($('trimStart') && !$('trimStart').value) $('trimStart').value = t;
            });
        });
    }

    ws.on('error', err => {
        console.error('WaveSurfer error:', err);
        wrap.style.display = 'none';
    });
}

function formatTime(sec) {
    sec = Math.max(0, sec);
    const h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60), s = sec % 60;
    return (h > 0 ? String(h).padStart(2, '0') + ':' : '') + String(m).padStart(2, '0') + ':' + String(s.toFixed(2)).padStart(5, '0');
}
