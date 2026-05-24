// ═══════════════════════════════════════════════════════════════════════════
// PLAN GUARD — global fetch wrapper for billing-gated endpoints
//
// Responsibilities:
//   1. When batchQueue is running, tag outgoing /api/tools/* requests with
//      X-Lt-Batch: 1 so the backend can gate batch processing to Pro users.
//   2. Intercept 402/413/429 responses that include {"plan_required":"pro"}
//      and open the upgrade modal instead of showing a generic toast.
//   3. Fetch /api/account/quota on load + after every successful AI call so
//      the sidebar can show "X of 20 AI requests left today".
// ═══════════════════════════════════════════════════════════════════════════

(function () {
    const origFetch = window.fetch.bind(window);
    const TOOLS_PREFIX = '/api/tools/';
    const AI_EXTRA = new Set(['/api/mind-map', '/api/youtube-summary']);

    function isToolEndpoint(url) {
        if (typeof url !== 'string') return false;
        if (url.indexOf(TOOLS_PREFIX) === 0) return true;
        return AI_EXTRA.has(url.split('?')[0]);
    }

    function isAIEndpoint(url) {
        if (typeof url !== 'string') return false;
        const path = url.split('?')[0];
        if (AI_EXTRA.has(path)) return true;
        if (path.indexOf(TOOLS_PREFIX) !== 0) return false;
        const tail = path.slice(TOOLS_PREFIX.length).split('/')[0];
        return (
            tail === 'paraphrase' || tail === 'study-notes' ||
            tail === 'flashcards' || tail === 'quiz' ||
            tail === 'citation-generate' ||
            // The AI* aliases used by some tools — backend treats these as AI too.
            tail.indexOf('ai-') === 0
        );
    }

    function batchActive() {
        try { return !!(window.batchQueue && window.batchQueue.running); }
        catch { return false; }
    }

    window.fetch = async function (input, init) {
        init = init || {};
        const url = (typeof input === 'string') ? input : (input && input.url) || '';
        const method = (init.method || (input && input.method) || 'GET').toUpperCase();

        // Tag batch jobs so the backend can reject them on the Free plan.
        if (method === 'POST' && isToolEndpoint(url) && batchActive()) {
            const headers = new Headers(init.headers || (input && input.headers) || {});
            headers.set('X-Lt-Batch', '1');
            init.headers = headers;
        }

        let res;
        try {
            res = await origFetch(input, init);
        } catch (err) {
            throw err;
        }

        // Refresh quota display after any successful AI call.
        if (method === 'POST' && res.ok && isAIEndpoint(url)) {
            setTimeout(() => window.PlanGuard.refreshQuota(), 200);
        }

        // Plan-gated responses: 402 (batch), 413 (file too big), 429 (AI quota).
        if (res.status === 402 || res.status === 413 || res.status === 429) {
            try {
                const clone = res.clone();
                const data = await clone.json();
                if (data && data.plan_required === 'pro') {
                    handlePlanGate(res.status, data);
                }
            } catch { /* not JSON, let the original caller handle it */ }
        }
        return res;
    };

    function handlePlanGate(status, data) {
        const msg = (data && data.error) || 'Upgrade to Pro to continue.';
        if (typeof showToast === 'function') showToast(msg, 'warning');

        // Pop the upgrade modal. Reuse the existing modal & set tailored body copy.
        if (typeof openUpgradeModal === 'function') {
            openUpgradeModal();
            const body = document.getElementById('upgradeBody');
            if (body && msg) body.textContent = msg;
        }
        try {
            if (typeof lvTrack === 'function') {
                lvTrack('paywall_blocked', {
                    source_page: status === 413 ? 'file_size_limit'
                              : status === 429 ? 'ai_quota_limit'
                              : 'batch_limit',
                    selected_plan: 'pro'
                }, { dedupeKey: 'paywall_blocked:' + status, debounceMs: 2000 });
            }
        } catch {}
    }

    // ── Quota display ────────────────────────────────────────────────────────
    const PlanGuard = {
        lastQuota: null,
        async refreshQuota() {
            try {
                const r = await origFetch('/api/account/quota', { credentials: 'same-origin' });
                if (!r.ok) return null;
                const data = await r.json();
                this.lastQuota = data;
                this.render(data);
                return data;
            } catch { return null; }
        },
        render(data) {
            const el = document.getElementById('planQuotaChip');
            if (!el || !data) return;
            const ai = data.ai || {};
            if (ai.unlimited) {
                el.innerHTML = '<i class="fas fa-crown"></i> Pro — unlimited AI';
                el.classList.add('plan-quota-pro');
                el.classList.remove('plan-quota-low');
            } else {
                const remaining = Math.max(0, ai.remaining || 0);
                el.innerHTML = '<i class="fas fa-bolt"></i> ' + remaining + ' / ' +
                               (ai.quota || 20) + ' AI today';
                el.classList.remove('plan-quota-pro');
                el.classList.toggle('plan-quota-low', remaining <= 3);
            }
            el.style.display = '';
        }
    };
    window.PlanGuard = PlanGuard;

    document.addEventListener('DOMContentLoaded', () => {
        PlanGuard.refreshQuota();
    });
})();
