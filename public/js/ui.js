// ═══════════════════════════════════════════════════════════════════════════
// SIDEBAR & NAVIGATION
// ═══════════════════════════════════════════════════════════════════════════

function switchTool(toolId) {
    // Guard: if the nav item is marked disabled, show the modal and bail out
    const navItemCheck = document.querySelector(`.nav-item[data-tool="${toolId}"]`);
    if (navItemCheck?.dataset.disabled === 'true') {
        showDisabledModal();
        return;
    }

    state.currentTool = toolId;
    $$('.nav-item').forEach(el => el.classList.toggle('active', el.dataset.tool === toolId));
    $$('.tool-panel').forEach(el => el.classList.toggle('active', el.id === 'tool-' + toolId));

    // Show browser/server badge in the tool header
    const navItem = document.querySelector(`.nav-item[data-tool="${toolId}"]`);
    const panel = document.getElementById('tool-' + toolId);

    if (navItem && panel) {
        const loc = navItem.dataset.location; // 'browser' | 'server'
        panel.querySelectorAll('.tool-location-badge, .tool-fav-btn, .tool-model-badge').forEach(b => b.remove());

        if (loc) {
            const badge = document.createElement('span');
            badge.className = 'tool-location-badge loc-' + loc;
            badge.title = loc === 'browser'
                ? 'Runs entirely in your browser — files never leave your device'
                : 'Processed on our server — file is uploaded, then deleted';
            badge.innerHTML = loc === 'browser'
                ? '<i class="fas fa-lock"></i> In your browser'
                : '<i class="fas fa-server"></i> On our server';
            const h2 = panel.querySelector('.tool-header h2');

            if (h2) {
                h2.appendChild(badge);

                // AI model badge — show cached model or spinner while probing
                if (AI_BADGE_TOOLS.includes(toolId)) {
                    if (_activeAIModel) {
                        showModelBadge(toolId, _activeAIModel);
                    } else {
                        // Show checking spinner, then update when probe resolves
                        const existingFav = h2.querySelector('.tool-fav-btn');
                        const chk = document.createElement('span');
                        chk.className = 'tool-model-badge tmb-checking';
                        chk.innerHTML = '<i class="fas fa-circle-notch fa-spin"></i> Checking';
                        if (existingFav) h2.insertBefore(chk, existingFav);
                        else h2.appendChild(chk);
                        fetchAIStatus();
                    }
                }

                // Fav button always last
                const favs = getFavs();
                const favBtn = document.createElement('button');
                const isStarredInit = favs.includes(toolId);
                favBtn.className = 'tool-fav-btn' + (isStarredInit ? ' starred' : '');
                favBtn.dataset.tool = toolId;
                favBtn.title = isStarredInit ? 'Remove from favourites' : 'Add to favourites';
                favBtn.innerHTML = isStarredInit ? '<i class="fas fa-star"></i>' : '<i class="far fa-star"></i>';
                favBtn.addEventListener('click', (e) => toggleFav(toolId, e));
                h2.appendChild(favBtn);
            }
        }
    }

    if (window.innerWidth <= 768) toggleSidebar(false);
    window.scrollTo(0, 0);
    document.documentElement.scrollTop = 0;
    document.body.scrollTop = 0;
    const mc = document.querySelector('.main-content');

    if (mc) mc.scrollTop = 0;
    const si = $('sidebarSearch');

    if (si && si.value) { si.value = ''; filterSidebarTools(); }

    // Deep linking
    try { history.pushState({ tool: toolId }, '', toolId === 'landing' ? '/' : '#' + toolId); } catch {}
    // Restore saved per-tool settings
    restoreToolSettings(toolId);
    // Server-offline warning for server-side tools
    const banner = $('serverOfflineBanner');
    if (banner) {
        const _navItem = document.querySelector(`.nav-item[data-tool="${toolId}"]`);
        const isServer = _navItem?.dataset.location === 'server';
        banner.classList.toggle('hidden', !(isServer && window._serverOnline === false));
    }
}

function showDisabledModal() {
    const bd = $('disabledToolBackdrop');
    if (bd) bd.classList.add('open');
}

function closeDisabledModal(e) {
    if (e && e.target !== $('disabledToolBackdrop') && !e.target.closest('.dtm-close')) return;
    const bd = $('disabledToolBackdrop');
    if (bd) bd.classList.remove('open');
}

function toggleSidebar(forceState) {
    const sidebar = $('sidebar');
    const overlay = $('sidebarOverlay');
    const isOpen = typeof forceState === 'boolean' ? forceState : !sidebar.classList.contains('open');
    sidebar.classList.toggle('open', isOpen);
    overlay.classList.toggle('open', isOpen);

    if (window.innerWidth <= 768) {
        document.body.style.overflow = isOpen ? 'hidden' : '';
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// SIDEBAR SEARCH
// ═══════════════════════════════════════════════════════════════════════════

function filterSidebarTools() {
    const q = ($('sidebarSearch')?.value || '').toLowerCase().trim();
    const cats = $$('#sidebarNav .nav-category');
    cats.forEach(cat => {
        const items = cat.querySelectorAll('.nav-item');
        let anyVisible = false;
        items.forEach(item => {
            const text = item.textContent.toLowerCase();
            const match = !q || text.includes(q);
            item.classList.toggle('search-hidden', !match);

            if (match) anyVisible = true;
        });
        cat.classList.toggle('search-hidden', !anyVisible);
    });
    // Show empty state when actively searching and nothing found
    const empty = $('searchEmpty');
    if (empty) {
        const anyVisible = [...cats].some(c => !c.classList.contains('search-hidden'));
        empty.classList.toggle('hidden', !q || anyVisible);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// TOOL OPTIONS (format/preset selectors, aspect lock)
// ═══════════════════════════════════════════════════════════════════════════

function selectOutputFmt(btn) {
    const grid = btn.closest('.format-select-grid');
    grid.querySelectorAll('.fmt-btn').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    const toolId = grid.dataset.tool;

    if (toolId) { state.outputFormats[toolId] = btn.dataset.fmt; try { localStorage.setItem('lt_f_' + toolId, btn.dataset.fmt); } catch {} }
}

function selectPreset(btn) {
    const grid = btn.closest('.preset-grid');
    grid.querySelectorAll('.preset-btn').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    const toolId = grid.dataset.tool;

    if (toolId) { state.presets[toolId] = btn.dataset.val; try { localStorage.setItem('lt_p_' + toolId, btn.dataset.val); } catch {} }
}

function selectWmPos(btn) {
    const grid = document.getElementById('wmPositionGrid');
    if (!grid) return;
    grid.querySelectorAll('.wm-pos-btn').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
}

// Generic pill selector used by csv-json, study-notes-format, etc.
function selectPill(groupId, btn) {
    const group = document.getElementById(groupId);
    if (!group) return;
    group.querySelectorAll('.fmt-pill').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
}

function toggleAspectLock() {
    state.aspectLock = !state.aspectLock;
    const lock = $('dimLock');
    lock.classList.toggle('active', state.aspectLock);
    lock.innerHTML = state.aspectLock ? '<i class="fas fa-link"></i>' : '<i class="fas fa-unlink"></i>';
}

function getSelectedFmt(toolId) {
    const grid = document.querySelector(`.format-select-grid[data-tool="${toolId}"]`);
    const active = grid?.querySelector('.fmt-btn.active');
    return active?.dataset.fmt || '';
}

function getSelectedPreset(toolId) {
    const grid = document.querySelector(`.preset-grid[data-tool="${toolId}"]`);
    const active = grid?.querySelector('.preset-btn.active');
    return active?.dataset.val || '';
}

// ──────────────────────────────────────────────────────────────
// PER-TOOL SETTINGS MEMORY
// ──────────────────────────────────────────────────────────────
function restoreToolSettings(toolId) {
    try {
        // Restore saved output format
        const savedFmt = localStorage.getItem('lt_f_' + toolId);
        if (savedFmt) {
            const fmtGrid = document.querySelector(`.format-select-grid[data-tool="${toolId}"]`);
            if (fmtGrid) {
                const fmtBtn = fmtGrid.querySelector(`.fmt-btn[data-fmt="${savedFmt}"]`);
                if (fmtBtn) { fmtGrid.querySelectorAll('.fmt-btn').forEach(b => b.classList.remove('active')); fmtBtn.classList.add('active'); state.outputFormats[toolId] = savedFmt; }
            }
        }
        // Restore saved preset
        const savedPreset = localStorage.getItem('lt_p_' + toolId);
        if (savedPreset) {
            const presetGrid = document.querySelector(`.preset-grid[data-tool="${toolId}"]`);
            if (presetGrid) {
                const presetBtn = presetGrid.querySelector(`.preset-btn[data-val="${savedPreset}"]`);
                if (presetBtn) { presetGrid.querySelectorAll('.preset-btn').forEach(b => b.classList.remove('active')); presetBtn.classList.add('active'); state.presets[toolId] = savedPreset; }
            }
        }
    } catch {}
}

// ──────────────────────────────────────────────────────────────
// FAVOURITES
// ──────────────────────────────────────────────────────────────
function getFavs() {
    try { return JSON.parse(localStorage.getItem('lt_favs') || '[]'); } catch { return []; }
}
function setFavs(arr) {
    try { localStorage.setItem('lt_favs', JSON.stringify(arr)); } catch {}
}
function toggleFav(toolId, e) {
    if (e) e.stopPropagation();
    const favs = getFavs();
    const idx  = favs.indexOf(toolId);
    if (idx >= 0) favs.splice(idx, 1); else favs.push(toolId);
    setFavs(favs);
    renderFavs();
    // Update the header fav button if visible
    const isStarred = favs.includes(toolId);
    document.querySelectorAll(`.tool-fav-btn[data-tool="${toolId}"]`).forEach(btn => {
        btn.classList.toggle('starred', isStarred);
        btn.title = isStarred ? 'Remove from favourites' : 'Add to favourites';
        btn.innerHTML = isStarred ? '<i class="fas fa-star"></i>' : '<i class="far fa-star"></i>';
    });
}
function renderFavs() {
    const favs = getFavs();
    const catEl = $('navCatFavs');
    if (!catEl) return;
    // Remove old fav nav-items
    catEl.querySelectorAll('.nav-item').forEach(el => el.remove());
    if (favs.length === 0) { catEl.style.display = 'none'; return; }
    catEl.style.display = '';
    favs.forEach(toolId => {
        const src = document.querySelector(`.nav-item[data-tool="${toolId}"]`);
        if (!src) return;
        const clone = src.cloneNode(true);
        catEl.appendChild(clone);
    });
}

// ──────────────────────────────────────────────────────────────
// COLLAPSIBLE CATEGORIES
// ──────────────────────────────────────────────────────────────
function toggleNavCat(titleEl) {
    const cat = titleEl.closest('.nav-category');
    if (!cat) return;
    cat.classList.toggle('collapsed');
    // Persist collapsed state
    const label = titleEl.childNodes[0]?.textContent?.trim() || '';
    try {
        const collapsed = JSON.parse(localStorage.getItem('lt_cats_collapsed') || '[]');
        const idx = collapsed.indexOf(label);
        if (cat.classList.contains('collapsed')) { if (idx < 0) collapsed.push(label); }
        else { if (idx >= 0) collapsed.splice(idx, 1); }
        localStorage.setItem('lt_cats_collapsed', JSON.stringify(collapsed));
    } catch {}
}

// ──────────────────────────────────────────────────────────────
// HISTORY DRAWER
// ──────────────────────────────────────────────────────────────
function toggleHistoryDrawer(open) {
    const drawer = $('historyDrawer');
    if (!drawer) return;
    const isOpen = typeof open === 'boolean' ? open : !drawer.classList.contains('open');
    drawer.classList.toggle('open', isOpen);
    drawer.setAttribute('aria-hidden', String(!isOpen));
}

// ──────────────────────────────────────────────────────────────
// INITIALISATION (DOMContentLoaded)
// ──────────────────────────────────────────────────────────────
document.addEventListener('DOMContentLoaded', () => {
    // ─ Add chevrons + onclick to all nav-category-titles ─
    document.querySelectorAll('#sidebarNav .nav-category-title').forEach(titleEl => {
        if (!titleEl.querySelector('.nav-cat-chevron')) {
            const ch = document.createElement('i');
            ch.className = 'fas fa-chevron-down nav-cat-chevron';
            titleEl.appendChild(ch);
        }
        titleEl.style.cursor = 'pointer';
        titleEl.onclick = () => toggleNavCat(titleEl);
    });

    // ─ Restore collapsed categories ─
    try {
        const collapsed = JSON.parse(localStorage.getItem('lt_cats_collapsed') || '[]');
        document.querySelectorAll('#sidebarNav .nav-category-title').forEach(titleEl => {
            const label = titleEl.childNodes[0]?.textContent?.trim() || '';
            if (collapsed.includes(label)) titleEl.closest('.nav-category')?.classList.add('collapsed');
        });
    } catch {}

    // ─ Render favourites category ─
    renderFavs();

    // ─ Deep-link: restore tool from URL hash ─
    const hash = location.hash.replace('#', '').trim();
    if (hash && document.getElementById('tool-' + hash)) {
        switchTool(hash);
    }

    // Probe AI model status quietly in the background so the badge is ready
    setTimeout(fetchAIStatus, 400);

    // Handle browser back/forward
    window.addEventListener('popstate', (e) => {
        const toolId = e.state?.tool || 'landing';
        if (document.getElementById('tool-' + toolId)) switchTool(toolId);
    });

    // Escape closes any open modal
    document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape') {
            closeDisabledModal({ target: $('disabledToolBackdrop') });
            closeQuizConfirmModal();
        }
    });
});

// ═════════════════════════════════════════════════════════════════════════
// AI MODEL BADGE
// ═════════════════════════════════════════════════════════════════════════

// Tools that use the AI model chain (shown in badge)
const AI_BADGE_TOOLS = ['ai-study-notes','ai-flashcards','ai-quiz','ai-paraphrase','ai-key-terms','mind-map','youtube-summary'];

// Cached active model — null until first probe or first tool response
let _activeAIModel = null;
let _aiStatusFetching = false;

// Probe /api/ai-status to get the current model without running a real job
function fetchAIStatus() {
    if (_aiStatusFetching) return;
    _aiStatusFetching = true;
    fetch('/api/ai-status')
        .then(r => r.json())
        .then(data => {
            _aiStatusFetching = false;
            // Use returned model, or fall back to primary so badge is never stuck
            updateActiveAIModel(data.model || 'llama-3.3-70b-versatile');
        })
        .catch(() => {
            _aiStatusFetching = false;
            // Server unreachable — resolve with primary model so badge doesn't stay on "Checking"
            updateActiveAIModel('llama-3.3-70b-versatile');
        });
}

// Call after any AI response or after /api/ai-status resolves
function updateActiveAIModel(modelId) {
    _activeAIModel = modelId;
    // Refresh badge on whichever AI tool panel is currently active
    if (AI_BADGE_TOOLS.includes(state.currentTool)) {
        showModelBadge(state.currentTool, modelId);
    }
}

const AI_MODELS = {
    'llama-3.3-70b-versatile': {
        badge: 'L3.3 70B', short: 'Llama 3.3 70B', tier: 'Primary', tpd: '100k/day',
        desc: "Meta's most capable open model. Used first for best quality results."
    },
    'deepseek-r1-distill-llama-70b': {
        badge: 'R1 70B', short: 'DeepSeek R1 70B', tier: 'Fallback 1', tpd: 'Separate quota',
        desc: 'DeepSeek R1 reasoning distilled on Llama 70B. Used when primary hits daily limits.'
    },
    'llama-3.1-8b-instant': {
        badge: 'L3.1 8B', short: 'Llama 3.1 8B', tier: 'Fallback 2', tpd: '500k/day',
        desc: 'Fast & lightweight. Highest daily allowance. Used when cloud quotas are low.'
    },
    'ollama:llama3.1:8b': {
        badge: 'Local', short: 'Llama 3.1 8B (Local)', tier: 'Local Device', tpd: 'Unlimited',
        desc: 'Runs locally on the server via Ollama. Same model as Groq Fallback 2 but with no API quota. Used only when all cloud models are exhausted.'
    }
};

function showModelBadge(toolId, modelId) {
    const panel = document.getElementById('tool-' + toolId);
    if (!panel) return;
    const h2 = panel.querySelector('.tool-header h2');
    if (!h2) return;
    let badge = h2.querySelector('.tool-model-badge');
    if (!badge) {
        badge = document.createElement('span');
        badge.className = 'tool-model-badge';
        // Always insert before the fav button so order is: location → model → fav
        const favBtn = h2.querySelector('.tool-fav-btn');
        if (favBtn) h2.insertBefore(badge, favBtn);
        else h2.appendChild(badge);
    }
    // Persist model across tool switches
    if (modelId && modelId !== 'none') _activeAIModel = modelId;
    // Error state — all models unavailable
    if (!modelId || modelId === 'none') {
        badge.className = 'tool-model-badge tmb-error';
        badge.innerHTML = `<i class="fas fa-exclamation-triangle"></i> No AI<div class="tmb-tooltip"><div class="tmb-header">AI Unavailable</div><div class="tmb-no-model">All AI models are currently rate-limited or unreachable. This includes the 3 Groq cloud models and the local Ollama fallback. Please try again later.</div></div>`;
        return;
    }
    const model = AI_MODELS[modelId];
    if (!model) return;
    badge.className = 'tool-model-badge';
    const allModels = Object.entries(AI_MODELS).map(([id, m]) => `
        <div class="tmb-model ${id === modelId ? 'active' : ''} ${id.startsWith('ollama:') ? 'tmb-local' : ''}">
            <div class="tmb-model-row">
                <span class="tmb-tier">${m.tier}</span>
                <span class="tmb-name">${m.short}</span>
            </div>
            <div class="tmb-meta"><span class="tmb-tpd">${m.tpd}</span></div>
            <div class="tmb-desc">${m.desc}</div>
        </div>`).join('');
    const isLocal = modelId.startsWith('ollama:');
    const whyColor = isLocal ? '#4ade80' : '#fbbf24';
    badge.innerHTML = `<i class="fas fa-robot"></i> ${model.badge}<div class="tmb-tooltip"><div class="tmb-header">AI Model Chain</div>${allModels}<div class="tmb-why" style="color:${whyColor}">Currently using <strong>${model.short}</strong> &mdash; ${model.tier}</div></div>`;
}
