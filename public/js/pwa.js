// ═══════════════════════════════════════════════════════════════════════════
// PWA INSTALL PROMPT
// ═══════════════════════════════════════════════════════════════════════════

let _deferredInstallPrompt = null;

function _setInstallBtnsVisible(show) {
    [$('installAppBtn'), $('landingInstallBtn')].forEach(btn => {
        if (btn) btn.classList.toggle('hidden', !show);
    });
}

function initInstallPrompt() {
    // Show buttons immediately — hide only once confirmed installed
    _setInstallBtnsVisible(true);

    window.addEventListener('beforeinstallprompt', (e) => {
        e.preventDefault();
        _deferredInstallPrompt = e;
    });

    window.addEventListener('appinstalled', () => {
        _deferredInstallPrompt = null;
        _setInstallBtnsVisible(false);
        showToast('App installed successfully!', 'success');
    });

    // Hide if already running as installed PWA
    if (window.matchMedia('(display-mode: standalone)').matches || window.navigator.standalone) {
        _setInstallBtnsVisible(false);
    }
}

function installPWA() {
    lvTrack('signup_started', { source_page: 'pwa_install', signup_type: 'pwa_install' }, { dedupeKey: 'signup_started:pwa_install', debounceMs: 4000 });
    if (_deferredInstallPrompt) {
        _deferredInstallPrompt.prompt();
        _deferredInstallPrompt.userChoice.then(() => {
            lvTrack('signup_completed', { source_page: 'pwa_install', signup_type: 'pwa_install' }, { dedupeKey: 'signup_completed:pwa_install', debounceMs: 4000 });
            _deferredInstallPrompt = null;
        });
    } else {
        // Fallback for browsers that suppress beforeinstallprompt (e.g. Brave, Firefox)
        showToast('To install: click the install icon in your address bar, or use your browser menu and select "Install app"', 'info', 6000);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// PWA INSTALL BANNER — appears after first tool use or 45s, dismissible
// ═══════════════════════════════════════════════════════════════════════════

(function () {
    const DISMISSED_KEY = 'lt_pwa_banner_dismissed';
    const INSTALL_KEY   = 'lt_pwa_installed';

    function shouldShow() {
        if (localStorage.getItem(DISMISSED_KEY)) return false;
        if (localStorage.getItem(INSTALL_KEY))   return false;
        if (window.matchMedia('(display-mode: standalone)').matches) return false;
        if (window.navigator.standalone) return false;
        return true;
    }

    function createBanner() {
        if (document.getElementById('pwaBanner')) return;
        const el = document.createElement('div');
        el.id = 'pwaBanner';
        el.className = 'pwa-banner';
        el.innerHTML = `
          <div class="pwa-banner-icon"><svg width="22" height="22" viewBox="0 0 32 32" fill="none"><path d="M18 4 L10 18 L15.5 18 L14 28 L22 14 L16.5 14 Z" fill="currentColor"/></svg></div>
          <div class="pwa-banner-text">
            <strong>Install Luma Tools</strong>
            <span>One tap away — works offline, no browser chrome</span>
          </div>
          <button class="pwa-banner-install" onclick="window._pwaBannerInstall()">Install free</button>
          <button class="pwa-banner-dismiss" onclick="window._pwaBannerDismiss()" aria-label="Dismiss">&#x2715;</button>
        `;
        document.body.appendChild(el);
        requestAnimationFrame(() => el.classList.add('pwa-banner-visible'));
    }

    window._pwaBannerInstall = function () {
        installPWA();
        localStorage.setItem(INSTALL_KEY, '1');
        const b = document.getElementById('pwaBanner');
        if (b) b.remove();
    };

    window._pwaBannerDismiss = function () {
        localStorage.setItem(DISMISSED_KEY, '1');
        const b = document.getElementById('pwaBanner');
        if (b) { b.classList.remove('pwa-banner-visible'); setTimeout(() => b.remove(), 350); }
    };

    function initBanner() {
        if (!shouldShow()) return;

        // Show after first tool completion OR 45 seconds, whichever comes first
        let shown = false;
        function show() {
            if (shown || !shouldShow()) return;
            shown = true;
            createBanner();
        }

        setTimeout(show, 45000);

        // Also show when a tool result appears (tool completes)
        document.addEventListener('lt:toolComplete', show, { once: true });
    }

    window.addEventListener('appinstalled', () => {
        localStorage.setItem(INSTALL_KEY, '1');
        const b = document.getElementById('pwaBanner');
        if (b) b.remove();
    });

    document.addEventListener('DOMContentLoaded', initBanner);
})();

// ═══════════════════════════════════════════════════════════════════════════
// BACKGROUND PARTICLES
// ═══════════════════════════════════════════════════════════════════════════

function initParticles() {
    const canvas = document.getElementById('particles');

    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    let width, height;
    const particles = [];
    const isMobile = window.innerWidth <= 768;
    const COUNT = isMobile ? 20 : 50;
    const CONNECT_DIST = isMobile ? 100 : 150;

    function resize() { width = canvas.width = window.innerWidth; height = canvas.height = window.innerHeight; }
    function create() {
        return { x: Math.random() * width, y: Math.random() * height, vx: (Math.random() - 0.5) * 0.3, vy: (Math.random() - 0.5) * 0.3, size: Math.random() * 2 + 0.5, alpha: Math.random() * 0.3 + 0.05 };
    }

    function animate() {
        ctx.clearRect(0, 0, width, height);

        for (const p of particles) {
            p.x += p.vx; p.y += p.vy;

            if (p.x < 0) p.x = width; if (p.x > width) p.x = 0;
            if (p.y < 0) p.y = height; if (p.y > height) p.y = 0;
            ctx.beginPath(); ctx.arc(p.x, p.y, p.size, 0, Math.PI * 2);
            ctx.fillStyle = `rgba(124, 92, 255, ${p.alpha})`; ctx.fill();
        }

        for (let i = 0; i < particles.length; i++) {
            for (let j = i + 1; j < particles.length; j++) {
                const dx = particles[i].x - particles[j].x, dy = particles[i].y - particles[j].y;
                const dist = Math.sqrt(dx * dx + dy * dy);

                if (dist < CONNECT_DIST) {
                    ctx.beginPath(); ctx.moveTo(particles[i].x, particles[i].y); ctx.lineTo(particles[j].x, particles[j].y);
                    ctx.strokeStyle = `rgba(124, 92, 255, ${0.05 * (1 - dist / CONNECT_DIST)})`; ctx.lineWidth = 0.5; ctx.stroke();
                }
            }
        }

        requestAnimationFrame(animate);
    }

    window.addEventListener('resize', resize);
    resize();

    for (let i = 0; i < COUNT; i++) particles.push(create());
    animate();
}

// ═══════════════════════════════════════════════════════════════════════════
// MOBILE SWIPE — open/close sidebar
// ═══════════════════════════════════════════════════════════════════════════

function initMobileSwipe() {
    if (window.innerWidth > 768) return;

    let touchStartX = 0, touchStartY = 0, swiping = false;
    const SWIPE_THRESHOLD = 50;
    const EDGE_ZONE = 30;

    document.addEventListener('touchstart', (e) => {
        touchStartX = e.touches[0].clientX;
        touchStartY = e.touches[0].clientY;
        swiping = touchStartX < EDGE_ZONE || $('sidebar').classList.contains('open');
    }, { passive: true });

    document.addEventListener('touchend', (e) => {
        if (!swiping) return;
        const dx = e.changedTouches[0].clientX - touchStartX;
        const dy = Math.abs(e.changedTouches[0].clientY - touchStartY);

        if (dy > Math.abs(dx)) return; // ignore vertical scrolling
        const sidebar = $('sidebar');

        if (dx > SWIPE_THRESHOLD && !sidebar.classList.contains('open') && touchStartX < EDGE_ZONE) {
            toggleSidebar(true);
        } else if (dx < -SWIPE_THRESHOLD && sidebar.classList.contains('open')) {
            toggleSidebar(false);
        }

        swiping = false;
    }, { passive: true });
}

// ═══════════════════════════════════════════════════════════════════════════
// APP INIT
// ═══════════════════════════════════════════════════════════════════════════

document.addEventListener('DOMContentLoaded', () => {
    initParticles();
    initUploadZones();
    initDownloader();
    checkServerHealth();
    initMobileSwipe();
    initGlobalDrop();
    initInstallPrompt();

    // Always start on landing, then immediately override if luma-planner sent a deep-link.
    // This 'last write wins' approach is immune to script load order and SW caching.
    switchTool('landing');

    // ── QR share-link deep link: /#qr-generate?q=<encoded-text> ──────────
    // When someone shares a QR code link, auto-open the tool and generate it.
    setTimeout(function () {
        try {
            var hash = window.location.hash; // e.g. "#qr-generate?q=hello"
            if (hash.startsWith('#qr-generate?')) {
                var qs = hash.slice('#qr-generate?'.length);
                var params = new URLSearchParams(qs);
                var q = params.get('q');
                if (q) {
                    switchTool('qr-generate');
                    setTimeout(function () {
                        var inp = document.getElementById('qrInput');
                        if (inp) { inp.value = q; }
                        if (typeof generateQR === 'function') generateQR();
                    }, 400);
                }
            }
        } catch (_) {}
    }, 300);
    setTimeout(function () {
        try {
            var hasDeepLink = !!sessionStorage.getItem('lt_deeplink');
            var lastTool = localStorage.getItem('lt_last_tool');
            if (!hasDeepLink && lastTool && lastTool !== 'landing' && window._lvWasReturningUser) {
                switchTool(lastTool);
                showToast('Welcome back - restored your last tool.', 'info', 2600);
                lvTrack('activation_action', {
                    tool_id: lastTool,
                    source_page: 'session_restore',
                    action: 'resume_last_tool',
                }, { dedupeKey: `resume_last_tool:${lastTool}`, debounceMs: 60000 });
            }
        } catch (_) {}
    }, 220);

    setTimeout(function () {
        try {
            var dl = sessionStorage.getItem('lt_deeplink');
            if (!dl) return;
            var d = JSON.parse(dl);
            sessionStorage.removeItem('lt_deeplink');
            var tool = d.tool || '';
            var mode = d.mode || '';
            var text  = d.text  || '';
            if (!tool) return;
            switchTool(tool);
            // Prefill the paste textarea after the panel is active
            setTimeout(function () {
                var pm = {
                    'ai-study-notes': { toggle: 'toggleStudyNotesInput', inputId: 'study-notes-text-input', modeKey: 'study-notes-input-mode', submit: 'processStudyNotes' },
                    'ai-flashcards':  { toggle: 'toggleFlashcardsInput',  inputId: 'flashcards-text-input',  modeKey: 'flashcards-input-mode',  submit: 'processFlashcards' },
                    'ai-quiz':        { toggle: 'toggleQuizInput',         inputId: 'quiz-text-input',        modeKey: 'quiz-input-mode',        submit: 'processQuiz' },
                };
                var cfg = pm[tool];
                if (!cfg) return;
                if (mode === 'paste' || mode === 'ai') {
                    var fn = window[cfg.toggle];
                    if (typeof fn === 'function') fn('paste');
                    // Activate the Paste Text preset button visually
                    var grid = document.querySelector('.preset-grid[data-tool="' + cfg.modeKey + '"]');
                    if (grid) {
                        var pasteBtn = grid.querySelector('[data-val="paste"]');
                        if (pasteBtn && typeof window.selectPreset === 'function') window.selectPreset(pasteBtn);
                    }
                }
                if (text) {
                    var el = document.getElementById(cfg.inputId);
                    if (el) { el.value = text; el.dispatchEvent(new Event('input')); }

                    // Auto-generate when coming from Luma Planner with 'ai' mode
                    if (mode === 'ai' && cfg.submit) {
                        setTimeout(function () {
                            var submitFn = window[cfg.submit];
                            if (typeof submitFn === 'function') submitFn();
                        }, 250);
                    }
                }
            }, 600);
        } catch (e) {}
    }, 50);

    document.addEventListener('keydown', (e) => {
        if ((e.ctrlKey || e.metaKey) && e.key === 'k') {
            e.preventDefault();
            const si = $('sidebarSearch');

            if (si) { si.focus(); si.select(); }
            if (window.innerWidth <= 768 && !$('sidebar').classList.contains('open')) toggleSidebar(true);
        }

        if (e.key === 'Escape') closeQuickAction();
    });

    // Cache default format/preset selections
    document.querySelectorAll('.format-select-grid').forEach(grid => {
        const active = grid.querySelector('.fmt-btn.active');

        if (active && grid.dataset.tool) state.outputFormats[grid.dataset.tool] = active.dataset.fmt;
    });
    document.querySelectorAll('.preset-grid').forEach(grid => {
        const active = grid.querySelector('.preset-btn.active');

        if (active && grid.dataset.tool) state.presets[grid.dataset.tool] = active.dataset.val;
    });
});
