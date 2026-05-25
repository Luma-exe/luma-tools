// ═══════════════════════════════════════════════════════════════════════════
// FEEDBACK PILL — floating bottom-right button styled like the existing
// portfolio + ko-fi pills (pill shape, glass background, expands on hover
// to show its label). Click → opens a textarea modal → POSTs to /api/feedback
// (server forwards to Discord webhook).
// Skipped in embed mode.
// ═══════════════════════════════════════════════════════════════════════════
(function () {
    if (document.body.classList.contains('lt-embed')) return;

    function init() {
        if (document.getElementById('lt-feedback-btn')) return;
        const btn = document.createElement('a');
        btn.id = 'lt-feedback-btn';
        btn.href = '#';
        btn.className = 'feedback-float';
        btn.title = 'Send feedback';
        btn.setAttribute('aria-label', 'Send feedback');
        btn.innerHTML =
            '<span class="feedback-icon"><i class="fas fa-comment-dots"></i></span>' +
            '<span class="feedback-label">Feedback</span>';
        btn.addEventListener('click', e => { e.preventDefault(); open(); });
        document.body.appendChild(btn);
    }

    function open() {
        if (document.getElementById('lt-feedback-modal')) return;
        const back = document.createElement('div');
        back.id = 'lt-feedback-modal';
        back.style.cssText = 'position:fixed;inset:0;z-index:9100;background:rgba(0,0,0,.6);backdrop-filter:blur(4px);display:flex;align-items:center;justify-content:center;padding:20px';
        back.addEventListener('click', e => { if (e.target === back) close(); });
        back.innerHTML = `
            <div style="background:#12121e;border:1px solid rgba(255,255,255,.1);border-radius:14px;padding:22px;max-width:440px;width:100%;color:#e7e7ee;box-shadow:0 16px 48px rgba(0,0,0,.6)">
                <h3 style="margin:0 0 6px;font-size:1.05rem;display:flex;align-items:center;gap:8px">
                    <i class="fas fa-comment-dots" style="color:#7c5cff"></i> Send feedback
                </h3>
                <p style="margin:0 0 14px;font-size:.85rem;color:#9c9caf">Bug, idea, tool request — anything. Goes straight to Luma.</p>
                <textarea id="lt-fb-text" placeholder="What's on your mind?" rows="5" style="width:100%;box-sizing:border-box;padding:10px 12px;background:rgba(255,255,255,.05);border:1px solid rgba(255,255,255,.12);border-radius:8px;color:#fff;font:inherit;font-size:.92rem;resize:vertical"></textarea>
                <div style="display:flex;gap:10px;margin-top:14px;justify-content:flex-end">
                    <button id="lt-fb-cancel" type="button" style="padding:9px 16px;background:rgba(255,255,255,.05);border:1px solid rgba(255,255,255,.12);border-radius:8px;color:#a7a7b5;cursor:pointer">Cancel</button>
                    <button id="lt-fb-send" type="button" style="padding:9px 18px;background:linear-gradient(135deg,#7c5cff,#9b80ff);border:0;border-radius:8px;color:#fff;font-weight:600;cursor:pointer">Send</button>
                </div>
                <div id="lt-fb-status" style="margin-top:10px;font-size:.82rem;min-height:1em"></div>
            </div>`;
        document.body.appendChild(back);
        setTimeout(() => document.getElementById('lt-fb-text')?.focus(), 30);
        document.getElementById('lt-fb-cancel').onclick = close;
        document.getElementById('lt-fb-send').onclick   = send;
        document.addEventListener('keydown', escClose);
    }

    function close() {
        document.getElementById('lt-feedback-modal')?.remove();
        document.removeEventListener('keydown', escClose);
    }
    function escClose(e) { if (e.key === 'Escape') close(); }

    async function send() {
        const ta = document.getElementById('lt-fb-text');
        const status = document.getElementById('lt-fb-status');
        const btn = document.getElementById('lt-fb-send');
        const msg = (ta?.value || '').trim();
        if (!msg) { status.textContent = 'Please enter a message.'; status.style.color = '#fca5a5'; return; }
        btn.disabled = true; btn.textContent = 'Sending…';
        try {
            const r = await fetch('/api/feedback', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                credentials: 'same-origin',
                body: JSON.stringify({ message: msg, page: location.pathname + location.hash })
            });
            if (r.ok) {
                status.textContent = '✓ Sent. Thank you!';
                status.style.color = '#86efac';
                setTimeout(close, 1200);
            } else {
                const d = await r.json().catch(()=>({}));
                status.textContent = d.error || ('Could not send (HTTP ' + r.status + ').');
                status.style.color = '#fca5a5';
                btn.disabled = false; btn.textContent = 'Send';
            }
        } catch {
            status.textContent = 'Network error — try again.';
            status.style.color = '#fca5a5';
            btn.disabled = false; btn.textContent = 'Send';
        }
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }
})();
