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
    if (_deferredInstallPrompt) {
        _deferredInstallPrompt.prompt();
        _deferredInstallPrompt.userChoice.then(() => {
            _deferredInstallPrompt = null;
        });
    } else {
        // Fallback for browsers that suppress beforeinstallprompt (e.g. Brave, Firefox)
        showToast('To install: click the install icon in your address bar, or use your browser menu and select "Install app"', 'info', 6000);
    }
}

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
                    'ai-study-notes': { toggle: 'toggleStudyNotesInput', inputId: 'study-notes-text-input' },
                    'ai-flashcards':  { toggle: 'toggleFlashcardsInput',  inputId: 'flashcards-text-input'  },
                    'ai-quiz':        { toggle: 'toggleQuizInput',         inputId: 'quiz-text-input'        },
                };
                var cfg = pm[tool];
                if (!cfg) return;
                if (mode === 'paste' || mode === 'ai') {
                    var fn = window[cfg.toggle];
                    if (typeof fn === 'function') fn('paste');
                    // Activate the Paste Text preset button visually
                    var modeGridMap = {
                        'ai-study-notes': 'study-notes-input-mode',
                        'ai-flashcards':  'flashcards-input-mode',
                        'ai-quiz':        'quiz-input-mode',
                    };
                    var gridKey = modeGridMap[tool];
                    if (gridKey) {
                        var grid = document.querySelector('.preset-grid[data-tool="' + gridKey + '"]');
                        if (grid) {
                            var pasteBtn = grid.querySelector('[data-val="paste"]');
                            if (pasteBtn && typeof window.selectPreset === 'function') window.selectPreset(pasteBtn);
                        }
                    }
                }
                if (text) {
                    var el = document.getElementById(cfg.inputId);
                    if (el) { el.value = text; el.dispatchEvent(new Event('input')); }
                }
            }, 300);
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
