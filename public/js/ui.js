// ═══════════════════════════════════════════════════════════════════════════
// SIDEBAR & NAVIGATION
// ═══════════════════════════════════════════════════════════════════════════

function switchTool(toolId) {
    state.currentTool = toolId;
    $$('.nav-item').forEach(el => el.classList.toggle('active', el.dataset.tool === toolId));
    $$('.tool-panel').forEach(el => el.classList.toggle('active', el.id === 'tool-' + toolId));

    // Show browser/server badge in the tool header
    const navItem = document.querySelector(`.nav-item[data-tool="${toolId}"]`);
    const panel = document.getElementById('tool-' + toolId);

    if (navItem && panel) {
        const loc = navItem.dataset.location; // 'browser' | 'server'
        panel.querySelectorAll('.tool-location-badge').forEach(b => b.remove());

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

            if (h2) h2.appendChild(badge);
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
}

// ═══════════════════════════════════════════════════════════════════════════
// TOOL OPTIONS (format/preset selectors, aspect lock)
// ═══════════════════════════════════════════════════════════════════════════

function selectOutputFmt(btn) {
    const grid = btn.closest('.format-select-grid');
    grid.querySelectorAll('.fmt-btn').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    const toolId = grid.dataset.tool;

    if (toolId) state.outputFormats[toolId] = btn.dataset.fmt;
}

function selectPreset(btn) {
    const grid = btn.closest('.preset-grid');
    grid.querySelectorAll('.preset-btn').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    const toolId = grid.dataset.tool;

    if (toolId) state.presets[toolId] = btn.dataset.val;
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
