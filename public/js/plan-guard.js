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

        // 401 with require_login: user tried an AI endpoint without being signed in.
        if (res.status === 401 && method === 'POST' && isAIEndpoint(url)) {
            try {
                const clone = res.clone();
                const data = await clone.json();
                if (data && data.require_login) {
                    handleRequireLogin();
                }
            } catch {}
        }

        return res;
    };

    function handleRequireLogin() {
        // Show a toast + redirect prompt when an unauthenticated user hits an AI endpoint.
        if (typeof showToast === 'function') {
            showToast('Sign in to use AI tools — free account, no payment needed.', 'warning', 5000);
        }
        // Show a sign-in nudge modal if one exists, otherwise fall through to toast only.
        const modal = document.getElementById('signInNudgeModal');
        if (modal) {
            modal.classList.add('open');
        } else if (typeof openUpgradeModal === 'function') {
            // Re-use upgrade modal with custom copy as fallback.
            openUpgradeModal();
            const body = document.getElementById('upgradeBody');
            if (body) body.innerHTML =
                'AI tools require a free account. <a href="/account/login" style="color:var(--accent-light);text-decoration:underline">Sign in or register free</a> — no payment needed.';
        }
    }

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
                // Also refresh the sidebar user-profile card.
                this.refreshUserCard(data);
                // Wire AI guard buttons now that we know the sign-in state.
                if (!data.signed_in) wireAIGuard();
                return data;
            } catch { return null; }
        },

        async refreshUserCard(quota) {
            const card = document.getElementById('userProfileCard');
            if (!card) return;
            if (!quota || !quota.signed_in) {
                card.classList.add('signed-out');
                card.href = '/account/login';
                card.title = 'Sign in';
                const av = card.querySelector('.upc-avatar');
                const p  = card.querySelector('.upc-primary');
                const s  = card.querySelector('.upc-secondary');
                if (av) av.innerHTML = '<i class="fas fa-user"></i>';
                if (p)  p.textContent = 'Sign in';
                if (s)  s.textContent = 'to save your usage';
                return;
            }
            // Signed in — pull full profile (display_name, email) once and cache.
            let me = this._me;
            if (!me) {
                try {
                    const r = await origFetch('/api/account/me', { credentials: 'same-origin' });
                    if (r.ok) me = await r.json();
                } catch {}
                if (me && me.user) this._me = me;
            }
            const u = (me && me.user) || {};
            const name = u.display_name || (u.email ? u.email.split('@')[0] : 'You');
            const slug = encodeURIComponent(name);
            card.classList.remove('signed-out');
            card.href = '/u/' + slug;
            card.title = 'Open your public profile';
            const av = card.querySelector('.upc-avatar');
            const p  = card.querySelector('.upc-primary');
            const s  = card.querySelector('.upc-secondary');
            if (av) {
                // TODO: when we store OAuth avatar URLs (Discord/Google), drop in <img>.
                const initial = (name[0] || '?').toUpperCase();
                av.innerHTML = '<span>' + initial + '</span>';
            }
            if (p) p.textContent = name;
            if (s) {
                const planLabel = (quota.plan === 'pro' || quota.plan === 'starter')
                    ? '✦ Pro member'
                    : (u.email || 'View profile');
                s.textContent = planLabel;
            }
        },
        render(data) {
            if (!data) return;
            const ai = data.ai || {};
            const signedIn = !!data.signed_in;

            // ── Old sidebar chip (#planQuotaChip) ─────────────────────────────
            const chip = document.getElementById('planQuotaChip');
            if (chip) {
                if (ai.unlimited) {
                    chip.innerHTML = '<i class="fas fa-crown"></i> Pro — unlimited AI';
                    chip.classList.add('plan-quota-pro');
                    chip.classList.remove('plan-quota-low');
                } else {
                    const remaining = Math.max(0, ai.remaining || 0);
                    chip.innerHTML = '<i class="fas fa-bolt"></i> ' + remaining + ' / ' +
                                   (ai.quota || 20) + ' AI today';
                    chip.classList.remove('plan-quota-pro');
                    chip.classList.toggle('plan-quota-low', remaining <= 3);
                }
                chip.style.display = '';
            }

            // ── v2 sidebar .sb-pro button ─────────────────────────────────────
            const proBtn  = document.querySelector('.sb-pro');
            const proText = document.querySelector('.sb-pro-text');
            if (proText) {
                if (!signedIn) {
                    // Not logged in — show sign-in nudge
                    proText.textContent = 'Sign in for AI tools';
                    if (proBtn) {
                        proBtn.title = 'AI tools require a free account';
                        proBtn.onclick = () => { window.location.href = '/account/login'; };
                        proBtn.classList.remove('plan-quota-pro');
                    }
                } else if (ai.unlimited) {
                    proText.textContent = 'Pro — unlimited AI';
                    if (proBtn) proBtn.classList.add('plan-quota-pro');
                } else {
                    const remaining = Math.max(0, ai.remaining || 0);
                    const total = ai.quota || 20;
                    proText.textContent = remaining + ' / ' + total + ' AI requests today';
                    if (proBtn) {
                        proBtn.title = remaining <= 0
                            ? 'Daily AI limit reached — upgrade to Pro for unlimited'
                            : 'Click to upgrade to Pro for unlimited AI';
                        proBtn.classList.toggle('plan-quota-low', remaining <= 3);
                        proBtn.classList.remove('plan-quota-pro');
                    }
                }
            }

            // Store sign-in state for client-side pre-flight guard
            window._ltSignedIn = signedIn;
        }
    };
    window.PlanGuard = PlanGuard;

    // ── Client-side AI pre-flight guard ──────────────────────────────────────
    // After quota is loaded, wrap every AI submit button so that unauthenticated
    // users see the sign-in prompt instantly — before the network request fires.
    function wireAIGuard() {
        const AI_TOOL_IDS = [
            'ai-study-notes','ai-coverage','ai-flashcards','ai-quiz','ai-paraphrase',
            'citation-gen','mind-map','youtube-summary'
        ];
        AI_TOOL_IDS.forEach(id => {
            const panel = document.getElementById('tool-' + id);
            if (!panel) return;
            panel.querySelectorAll('.process-btn, .submit-btn, button[onclick]').forEach(btn => {
                if (btn.dataset.aiGuarded) return;
                btn.dataset.aiGuarded = '1';
                btn.addEventListener('click', function (e) {
                    if (window._ltSignedIn === false) {
                        e.stopImmediatePropagation();
                        e.preventDefault();
                        handleRequireLogin();
                    }
                }, true); // capture phase — fires before onclick
            });
        });
    }

    // Re-wire whenever a tool panel opens (new panels may not exist at DOMContentLoaded)
    document.addEventListener('lt:panelOpen', function (e) {
        if (window._ltSignedIn === false) wireAIGuard();
    });

    document.addEventListener('DOMContentLoaded', () => {
        PlanGuard.refreshQuota();
        // Embed mode: ?embed=1 or window in iframe → strip the chrome.
        try {
            const inFrame = window.self !== window.top;
            const params  = new URLSearchParams(location.search);
            if (params.get('embed') === '1' || (inFrame && params.has('embed'))) {
                document.body.classList.add('lt-embed');
            }
        } catch { /* sandboxed cross-origin iframe — fall through */ }
    });
})();
