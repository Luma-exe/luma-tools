// ════════════════════════════════════════════════════════════════════════
// LUMA TOOLS — v2 shell controller
//
// Drives the new pixel-perfect chrome (sidebar / home / category / search /
// tool page) while delegating all per-tool processing to the existing JS
// modules (file-tools.js, ai-tools.js, etc.) and the existing
// switchTool() in ui.js.
// ════════════════════════════════════════════════════════════════════════
(function () {
  const $  = (s, r) => (r || document).querySelector(s);
  const $$ = (s, r) => Array.from((r || document).querySelectorAll(s));

  const CATS = window.LUMA_CATEGORIES;
  const ALL  = window.LUMA_ALL_TOOLS;
  const FEAT = window.LUMA_FEATURED;

  // ── Icon set (line icons; mirrors the design's icons.jsx) ─────────────
  const ICONS = {
    home:      '<path d="M3 9l7-5 7 5v8a1 1 0 0 1-1 1h-3v-6h-6v6H4a1 1 0 0 1-1-1z"/>',
    grid:      '<rect x="3" y="3" width="6" height="6" rx="1"/><rect x="11" y="3" width="6" height="6" rx="1"/><rect x="3" y="11" width="6" height="6" rx="1"/><rect x="11" y="11" width="6" height="6" rx="1"/>',
    clock:     '<circle cx="10" cy="10" r="7"/><path d="M10 6v4l3 2"/>',
    download:  '<path d="M10 3v10"/><path d="M6 9l4 4 4-4"/><path d="M4 16h12"/>',
    image:     '<rect x="3" y="4" width="14" height="12" rx="1.5"/><circle cx="7.5" cy="8" r="1.2"/><path d="M3 14l4-4 4 4 3-3 3 3"/>',
    video:     '<rect x="3" y="5" width="11" height="10" rx="1.5"/><path d="M14 9l3-2v6l-3-2z"/>',
    audio:     '<path d="M5 8v4"/><path d="M8 6v8"/><path d="M11 4v12"/><path d="M14 7v6"/><path d="M17 9v2"/>',
    doc:       '<path d="M5 2h7l4 4v12H5z"/><path d="M12 2v4h4"/><path d="M8 10h6"/><path d="M8 13h6"/>',
    sparkles:  '<path d="M10 3 L11.5 7.5 L16 9 L11.5 10.5 L10 15 L8.5 10.5 L4 9 L8.5 7.5 Z"/><path d="M15 14 L15.7 15.3 L17 16 L15.7 16.7 L15 18 L14.3 16.7 L13 16 L14.3 15.3 Z"/>',
    wrench:    '<path d="M14.5 2.5a3.5 3.5 0 0 0-4.7 4.4L3 13.7 5.3 16l6.8-6.8a3.5 3.5 0 0 0 4.4-4.7l-2.3 2.3-1.8-1.8z"/>',
    search:    '<circle cx="9" cy="9" r="5"/><path d="M13 13l4 4"/>',
    arrow:     '<path d="M4 10h12"/><path d="M12 6l4 4-4 4"/>',
    'arrow-left': '<path d="M16 10H4"/><path d="M8 14l-4-4 4-4"/>',
    upload:    '<path d="M10 14V4"/><path d="M6 8l4-4 4 4"/><path d="M4 16h12"/>',
    link:      '<path d="M8 12l4-4"/><path d="M11 5l1-1a3 3 0 0 1 4 4l-2 2"/><path d="M9 15l-1 1a3 3 0 0 1-4-4l2-2"/>',
    close:     '<path d="M5 5l10 10"/><path d="M15 5L5 15"/>',
    check:     '<path d="M4 10l4 4 8-8"/>',
    shield:    '<path d="M10 2l6 2v6c0 4-3 7-6 8-3-1-6-4-6-8V4z"/><path d="M7.5 10l1.7 1.7L13 8"/>',
    calendar:  '<rect x="3" y="5" width="14" height="13" rx="1.5"/><path d="M3 9h14"/><path d="M7 3v4"/><path d="M13 3v4"/><path d="M7 13l2 2 4-4"/>',
  };
  const FILLED = {
    crown: '<path d="M3 6.5l3 3 4-5 4 5 3-3 -1 8.5H4z" fill="currentColor"/>',
  };
  function icon(name, size) {
    size = size || 14;
    if (FILLED[name]) {
      return `<svg width="${size}" height="${size}" viewBox="0 0 20 20" style="display:block">${FILLED[name]}</svg>`;
    }
    const body = ICONS[name] || '<circle cx="10" cy="10" r="6"/>';
    return `<svg width="${size}" height="${size}" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" style="display:block">${body}</svg>`;
  }
  function escape(s) {
    return String(s == null ? '' : s).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  }

  // ── Sidebar render ────────────────────────────────────────────────────
  function renderSidebar() {
    const nav = $('#sbNav');
    if (!nav) return;
    const totalTools = ALL.length;
    nav.innerHTML = `
      <div class="sb-group">
        <button class="sb-item" data-view="home">
          <span class="sb-item-ic">${icon('home')}</span>
          <span class="sb-item-name">Home</span>
        </button>
        <button class="sb-item" data-view="all">
          <span class="sb-item-ic">${icon('grid')}</span>
          <span class="sb-item-name">All tools</span>
          <span class="sb-item-count">${totalTools}</span>
        </button>
      </div>
      <div class="sb-group">
        <div class="sb-group-label">Categories</div>
        ${CATS.map(c => `
          <button class="sb-item" data-view="cat:${c.id}">
            <span class="sb-item-ic">${icon(c.icon)}</span>
            <span class="sb-item-name">${escape(c.label)}</span>
            <span class="sb-item-count">${c.tools.length}</span>
          </button>
        `).join('')}
      </div>
    `;
    nav.addEventListener('click', e => {
      const btn = e.target.closest('.sb-item[data-view]');
      if (!btn) return;
      const v = btn.dataset.view;
      if (v === 'home') showHome();
      else if (v === 'all') showAll();
      else if (v.startsWith('cat:')) showCategory(v.slice(4));
    });
  }

  // ── Top breadcrumb / sign-in render ───────────────────────────────────
  function setCrumb(html) { const c = $('#crumb'); if (c) c.innerHTML = html; }
  function setBackVisible(visible) {
    const b = $('#topBackBtn'); if (!b) return;
    b.style.display = visible ? 'inline-flex' : 'none';
  }

  // ── Sidebar active dot ────────────────────────────────────────────────
  function setSbActive(view) {
    $$('.sb-item[data-view]').forEach(b => {
      b.classList.toggle('active', b.dataset.view === view);
    });
  }

  // ── View visibility ───────────────────────────────────────────────────
  const VIEW_IDS = ['tool-landing', 'tool-categories', 'tool-all', 'tool-search'];
  function hideAllPanels() {
    $$('.tool-panel').forEach(p => p.classList.remove('active'));
  }
  function showPanel(id) {
    hideAllPanels();
    const p = document.getElementById(id);
    if (p) p.classList.add('active');
    window.scrollTo(0, 0);
  }

  // ── Home ──────────────────────────────────────────────────────────────
  function showHome() {
    setCrumb('<span class="crumb-now">Home</span>');
    setBackVisible(false);
    setSbActive('home');
    showPanel('tool-landing');
    try { history.replaceState({ view: 'home' }, '', '/'); } catch {}
  }
  function renderHome() {
    const panel = $('#tool-landing');
    if (!panel) return;
    const total = ALL.length;
    const feat = FEAT.map(id => window.LUMA_TOOL_BY_ID[id]).filter(Boolean);

    panel.innerHTML = `
      <div class="hero">
        <div class="hero-logo">${logoSvg(36)}</div>
        <h1 class="hero-title">Luma <span class="hero-title-accent">Tools</span></h1>
        <p class="hero-tagline">A free toolkit for students, creators and developers</p>
        <div class="hero-trust">
          <span class="hero-trust-badge">${icon('check',12)} No account needed</span>
          <span class="hero-trust-badge">${icon('check',12)} No email required</span>
          <span class="hero-trust-badge">${icon('shield',12)} Private by default</span>
          <span class="hero-trust-badge">${icon('check',12)} Free forever</span>
        </div>
        <div class="hero-stats">
          <span class="hero-stat"><b>${total}+</b> tools</span>
          <span class="hero-stat-sep"></span>
          <span class="hero-stat"><b>10+</b> download platforms</span>
          <span class="hero-stat-sep"></span>
          <span class="hero-stat"><b>3-pass</b> AI study pipeline</span>
          <span class="hero-stat-sep"></span>
          <span class="hero-stat"><b>Free</b> forever</span>
        </div>
      </div>

      <div class="quick" id="quickBar">
        <span class="quick-ic">${icon('link', 15)}</span>
        <input id="quickInput" placeholder="Paste a link, or drop a file anywhere"/>
        <span class="quick-detect" id="quickDetect" style="display:none"></span>
        <span class="quick-meta" id="quickMeta">Auto-routes to the right tool</span>
        <button class="btn-primary" id="quickGo" disabled>Continue ${icon('arrow', 12)}</button>
      </div>

      <div class="section">
        <div class="section-head">
          <span class="section-title">Most used</span>
          <span class="section-sub">Jump straight in</span>
        </div>
        <div class="featured">
          ${feat.map(t => `
            <button class="feat-card" data-tool="${t.id}">
              <div class="feat-card-head">
                <div class="feat-card-ic">${icon(t.catIcon, 15)}</div>
                <span class="feat-card-tag">${escape(t.catLabel)}</span>
              </div>
              <div class="feat-card-name">${escape(t.name)}</div>
              <div class="feat-card-blurb">${escape(t.desc)}</div>
              <div class="feat-card-go">Open ${icon('arrow', 11)}</div>
            </button>
          `).join('')}
        </div>
      </div>

      <div class="section">
        <div class="section-head">
          <span class="section-title">Browse by category</span>
          <span class="section-sub">${total} tools total</span>
        </div>
        <div class="cat-grid">
          ${CATS.map(c => `
            <button class="cat-card" data-cat="${c.id}">
              <div class="cat-card-head">
                <div class="cat-card-ic">${icon(c.icon, 15)}</div>
                <span class="cat-card-count">${c.tools.length}</span>
              </div>
              <div class="cat-card-name">${escape(c.label)}</div>
              <div class="cat-card-blurb">${escape(c.blurb)}</div>
            </button>
          `).join('')}
        </div>
      </div>

      <div class="section">
        <div class="section-head">
          <span class="section-title">Where things run</span>
          <span class="section-sub">Privacy by default</span>
        </div>
        <div class="behind">
          ${behindCard('browser', countWhere('browser'))}
          ${behindCard('server',  countWhere('server'))}
        </div>
      </div>

      <div class="section">
        <div class="planner">
          <div class="planner-body">
            <div class="planner-tag">${icon('calendar', 12)} From the same developer</div>
            <h3 class="planner-title">Meet <span class="planner-title-accent">Luma Planner</span></h3>
            <p class="planner-desc">You'll never miss an assignment again. Luma Planner pulls your deadlines from Canvas, Moodle, iCal, or Outlook into one clean dashboard — then keeps you on track with Pomodoro timers, email reminders, AI study tools, and custom themes.</p>
            <div class="planner-chips">
              ${['Canvas & Moodle sync','Pomodoro focus','Email reminders','AI study tools','Kanban board','Custom themes']
                .map(l => `<span class="planner-chip">${icon('check',12)} ${escape(l)}</span>`).join('')}
            </div>
            <a class="planner-cta" href="https://planner.lumaplayground.com" target="_blank" rel="noopener">${icon('arrow',13)} Try Luma Planner</a>
          </div>
          <div class="planner-art" aria-hidden="true">
            <div class="planner-art-card">${icon('calendar',36)}</div>
          </div>
        </div>
      </div>

      <div class="section">
        <div class="showcase">
          <h3 class="showcase-title">${total}+ Tools, Zero Ads</h3>
          <div class="showcase-grid">
            ${ALL.map(t => `
              <button class="showcase-pill" data-tool="${t.id}">
                ${icon(t.catIcon, 12)}
                <span>${escape(t.name)}</span>
              </button>
            `).join('')}
          </div>
          <div class="showcase-cta-row">
            <button class="btn-primary btn-lg" data-tool="ai-study-notes">${icon('sparkles',14)} Try AI Study Tools</button>
            <button class="btn-ghost btn-lg btn-outline" id="installCta">${icon('download',14)} Install App</button>
          </div>
        </div>
      </div>

      <div class="section">
        ${pricingTable()}
      </div>

      <div class="section">
        <div class="privacy">
          <div class="privacy-ic">${icon('shield', 16)}</div>
          <div>
            <div class="privacy-title">What we track &amp; why</div>
            <p class="privacy-body">We collect the bare minimum needed to understand how the site is used: which tools are used, whether a request succeeded or failed, and an anonymous daily visitor count. Your IP address is never stored — it is immediately hashed (one-way, irreversible) and discarded. No personal data, no cookies, no third-party analytics, no ads. Files you process are deleted from our server as soon as your download is ready.</p>
          </div>
        </div>
      </div>

      <div class="tech-foot">
        <p class="tech-stack">Powered by <b>C++</b>, <b>WebAssembly</b>, <b>FFmpeg</b>, <b>yt-dlp</b> &amp; <b>Ghostscript</b> — Built with <span class="tech-heart">♥</span> by <a href="https://github.com/Luma-exe" target="_blank" rel="noopener" class="tech-author"><b>Luma</b></a></p>
        <a class="tech-coffee" href="https://ko-fi.com/lumaexe" target="_blank" rel="noopener">☕ Buy me a coffee</a>
      </div>
    `;

    // Wire interactions
    panel.addEventListener('click', e => {
      const t = e.target.closest('[data-tool]');
      if (t) { openTool(t.dataset.tool); return; }
      const c = e.target.closest('[data-cat]');
      if (c) { showCategory(c.dataset.cat); return; }
      if (e.target.closest('#installCta')) { try { window.installPWA && window.installPWA(); } catch {} }
    });
    wireQuickBar();
  }

  function logoSvg(size) {
    return `<svg width="${size}" height="${size}" viewBox="0 0 32 32" fill="none"><path d="M18 4 L10 18 L15.5 18 L14 28 L22 14 L16.5 14 Z" fill="currentColor"/></svg>`;
  }
  function countWhere(w) { return ALL.filter(t => t.where === w).length; }
  function behindCard(kind, count) {
    const cls = kind === 'browser' ? 'browser' : 'server';
    const title = kind === 'browser' ? 'In your browser' : 'On our server';
    const body = kind === 'browser'
      ? `Most image, audio, and utility tools run locally using the Canvas API and ffmpeg.wasm. Files never leave your device — they're not even uploaded.`
      : `Video processing, PDFs, archives, and AI tools need server compute. Files are processed in a sandboxed job and auto-deleted on completion.`;
    return `
      <div class="behind-card behind-card-${cls}">
        <div class="behind-card-head">
          <span class="behind-pill behind-pill-${cls}">${escape(title)}</span>
          <span class="behind-card-num">~${count} tools</span>
        </div>
        <div class="behind-card-body">${body}</div>
      </div>
    `;
  }
  function pricingTable() {
    const rows = [
      ['All 50+ tools',         'check',     'check'],
      ['In-browser processing', 'check',     'check'],
      ['AI study tools',        'check',     'check'],
      ['Daily AI requests',     '20 / day',  'Unlimited'],
      ['Max file size',         '100 MB',    '2 GB'],
      ['Batch processing',      '—',         'Full'],
      ['Priority queue',        '—',         'check'],
      ['Support',               'Community', 'Priority'],
    ];
    const cell = v => {
      if (v === 'check') return `<span class="pricing-check">${icon('check', 14)}</span>`;
      if (v === '—')     return `<span class="pricing-dash">—</span>`;
      return `<span class="pricing-text">${escape(v)}</span>`;
    };
    return `
      <div class="pricing">
        <div class="pricing-head">
          <h3 class="pricing-title">Support the project, unlock faster workflow</h3>
          <p class="pricing-sub">Core tools are free forever. Pro removes the limits and keeps the servers running.</p>
        </div>
        <div class="pricing-table">
          <div class="pricing-row pricing-row-head">
            <div class="pricing-cell pricing-feat">Feature</div>
            <div class="pricing-cell">
              <div class="pricing-plan">Free</div>
              <div class="pricing-price">$0.00</div>
              <div class="pricing-meta">&nbsp;</div>
            </div>
            <div class="pricing-cell pricing-cell-pro">
              <div class="pricing-pro-strip"></div>
              <div class="pricing-plan">Pro</div>
              <div class="pricing-price">A$5 <span>/ mo</span></div>
              <div class="pricing-meta">billed monthly in AUD</div>
            </div>
          </div>
          ${rows.map(([feat, free, pro]) => `
            <div class="pricing-row">
              <div class="pricing-cell pricing-feat">${escape(feat)}</div>
              <div class="pricing-cell">${cell(free)}</div>
              <div class="pricing-cell pricing-cell-pro">${cell(pro)}</div>
            </div>
          `).join('')}
          <div class="pricing-row pricing-row-foot">
            <div class="pricing-cell pricing-feat"></div>
            <div class="pricing-cell"><button class="btn-ghost btn-outline" data-act="signup">Get started</button></div>
            <div class="pricing-cell pricing-cell-pro"><button class="btn-pro" data-act="pro">Join Pro</button></div>
          </div>
        </div>
      </div>
    `;
  }

  // ── QuickBar (URL detect + file drop everywhere) ──────────────────────
  const PLATFORM_HINTS = [
    [/youtube\.com|youtu\.be/, 'YouTube'],
    [/tiktok\.com/, 'TikTok'],
    [/instagram\.com/, 'Instagram'],
    [/spotify\.com/, 'Spotify'],
    [/twitter\.com|x\.com/, 'X'],
    [/soundcloud\.com/, 'SoundCloud'],
    [/reddit\.com/, 'Reddit'],
    [/twitch\.tv/, 'Twitch'],
    [/vimeo\.com/, 'Vimeo'],
    [/facebook\.com|fb\.com/, 'Facebook'],
  ];
  function wireQuickBar() {
    const bar    = $('#quickBar');
    const input  = $('#quickInput');
    const detect = $('#quickDetect');
    const meta   = $('#quickMeta');
    const go     = $('#quickGo');
    if (!input || !go) return;

    function refresh() {
      const v = input.value.trim();
      const isUrl = /^https?:\/\//i.test(v);
      const hit = isUrl && PLATFORM_HINTS.find(([re]) => re.test(v));
      if (hit) {
        detect.textContent = hit[1] + ' detected';
        detect.style.display = '';
      } else {
        detect.style.display = 'none';
      }
      go.disabled = !v;
    }
    input.addEventListener('input', refresh);
    function carryToDownloader(v) {
      openTool('downloader', () => {
        // Try multiple selectors — legacy panel uses #urlInput
        const urlInput = $('#urlInput')
                      || $('#downloaderUrl')
                      || document.querySelector('#tool-downloader input[type="url"], #tool-downloader input[type="text"]');
        if (urlInput) {
          urlInput.value = v;
          urlInput.dispatchEvent(new Event('input', { bubbles: true }));
          urlInput.dispatchEvent(new Event('change', { bubbles: true }));
        }
        // Auto-kick the analyze step so the user lands on a populated page
        // ready to go, not on an empty form they have to re-fill.
        if (typeof window.analyzeURL === 'function') {
          try { window.analyzeURL(); } catch {}
        }
      });
    }
    go.addEventListener('click', () => {
      const v = input.value.trim();
      if (!v) return;
      if (/^https?:\/\//i.test(v)) carryToDownloader(v);
    });
    input.addEventListener('keydown', e => {
      if (e.key === 'Enter') {
        const v = input.value.trim();
        if (v && /^https?:\/\//i.test(v)) carryToDownloader(v);
      }
    });
    ['dragover','dragenter'].forEach(ev => bar.addEventListener(ev, e => { e.preventDefault(); bar.classList.add('quick-drag'); meta.textContent = 'Release to upload'; }));
    ['dragleave'].forEach(ev => bar.addEventListener(ev, e => { e.preventDefault(); bar.classList.remove('quick-drag'); meta.textContent = 'Auto-routes to the right tool'; }));
    bar.addEventListener('drop', e => {
      e.preventDefault(); bar.classList.remove('quick-drag'); meta.textContent = 'Auto-routes to the right tool';
      const file = e.dataTransfer.files[0];
      if (!file) return;
      const tool = detectToolForFile(file);
      if (tool) {
        openTool(tool, () => {
          // Pre-fill the drop zone in the tool
          const panel = document.getElementById('tool-' + tool);
          if (!panel) return;
          const zone = panel.querySelector('.upload-zone input[type=file], .drop input[type=file], .tpv .drop input');
          if (zone) {
            const dt = new DataTransfer(); dt.items.add(file);
            zone.files = dt.files;
            zone.dispatchEvent(new Event('change', { bubbles: true }));
          }
        });
      }
    });
  }

  // ── Category / All / Search views ─────────────────────────────────────
  function toolRow(t) {
    return `
      <button class="tool-row" data-tool="${t.id}">
        <div class="tool-row-ic">${icon(t.catIcon, 14)}</div>
        <div class="tool-row-body">
          <div class="tool-row-head">
            <span class="tool-row-name">${escape(t.name)}</span>
            <span class="tag tag-${t.where}">${t.where === 'browser' ? 'In browser' : 'On server'}</span>
          </div>
          <div class="tool-row-desc">${escape(t.desc)}</div>
        </div>
        <span class="tool-arrow">${icon('arrow', 14)}</span>
      </button>
    `;
  }
  function showCategory(catId) {
    const cat = CATS.find(c => c.id === catId);
    if (!cat) return showHome();
    const tools = cat.tools.map(t => ({ ...t, cat: catId, catLabel: cat.label, catIcon: cat.icon }));
    const panel = $('#tool-categories');
    panel.innerHTML = `
      <div class="page-head">
        <h1 class="page-title">${escape(cat.label)}</h1>
        <p class="page-sub">${escape(cat.blurb)} · ${tools.length} tools available.</p>
      </div>
      <div class="tool-list">${tools.map(toolRow).join('')}</div>
    `;
    setCrumb(`<a data-nav="home">Home</a><span class="crumb-sep">/</span><span class="crumb-now">${escape(cat.label)}</span>`);
    setBackVisible(false);
    setSbActive('cat:' + catId);
    showPanel('tool-categories');
    try { history.pushState({ view: 'cat', cat: catId }, '', '#cat=' + catId); } catch {}
  }
  function showAll() {
    const panel = $('#tool-all');
    panel.innerHTML = `
      <div class="page-head">
        <h1 class="page-title">All tools</h1>
        <p class="page-sub">Every tool, in one searchable list. Use ⌘K to filter.</p>
      </div>
      ${CATS.map(cat => `
        <div class="section">
          <div class="section-head">
            <span class="section-title">${escape(cat.label)}</span>
            <span class="section-sub">${cat.tools.length} tools</span>
          </div>
          <div class="tool-list">
            ${cat.tools.map(t => toolRow({ ...t, cat: cat.id, catLabel: cat.label, catIcon: cat.icon })).join('')}
          </div>
        </div>
      `).join('')}
    `;
    setCrumb(`<a data-nav="home">Home</a><span class="crumb-sep">/</span><span class="crumb-now">All tools</span>`);
    setBackVisible(false);
    setSbActive('all');
    showPanel('tool-all');
    try { history.pushState({ view: 'all' }, '', '#all'); } catch {}
  }
  function showSearch(query) {
    const q = query.trim().toLowerCase();
    if (!q) return showHome();
    const hits = ALL.filter(t =>
      t.name.toLowerCase().includes(q) ||
      t.desc.toLowerCase().includes(q) ||
      t.catLabel.toLowerCase().includes(q)
    );
    const panel = $('#tool-search');
    panel.innerHTML = `
      <div class="page-head">
        <h1 class="page-title">Search</h1>
        <p class="page-sub">${hits.length} ${hits.length === 1 ? 'tool' : 'tools'} matching “${escape(query)}”</p>
      </div>
      ${hits.length
        ? `<div class="tool-list">${hits.map(toolRow).join('')}</div>`
        : `<div class="empty"><div class="empty-title">No tools match “${escape(query)}”</div><div class="empty-body">Try a different keyword, or browse by category from the sidebar.</div></div>`
      }
    `;
    setCrumb(`<span class="crumb-now">Search results</span>`);
    setBackVisible(false);
    setSbActive(null);
    showPanel('tool-search');
  }

  // ── Tool open: route to existing switchTool, then decorate breadcrumb ─
  function openTool(id, after) {
    if (!window.LUMA_TOOL_BY_ID[id]) {
      console.warn('Unknown tool id', id);
      return;
    }
    // Delegate to existing handler which toggles .tool-panel.active
    if (typeof window.switchTool === 'function') {
      try { window.switchTool(id); } catch (e) { console.error(e); }
    }
    // Hoist the activated panel into the new tool-page chrome
    afterToolSwitch(id);
    if (typeof after === 'function') setTimeout(after, 50);
  }

  function afterToolSwitch(id) {
    const t = window.LUMA_TOOL_BY_ID[id];
    if (!t) { showHome(); return; }

    // Show/hide AI model badge based on whether this tool uses the AI chain
    if (AI_TOOLS.has(id)) {
      setAIModelBadge(null, 'checking');
      probeAIStatus();
    } else {
      setAIModelBadge(null, 'hidden');
    }

    // Spec-driven path: if a v2 tool spec exists, render the pixel-perfect
    // form directly into #toolHost — bypass the legacy panel entirely.
    if (window.LumaToolPage && window.LumaToolPage.has(id)) {
      const host = $('#toolHost');
      if (host) {
        // Stash any legacy panel currently in the host back to storage
        const storage = $('#toolPanelStorage');
        if (storage) {
          [...host.children].forEach(c => {
            if (c.classList && c.classList.contains('tool-panel')) storage.appendChild(c);
          });
        }
        host.innerHTML = '';
        host.appendChild(window.LumaToolPage.render(id));
      }
      $('#tpageTitle').textContent = t.name;
      $('#tpageSub').textContent = t.desc;
      renderSidePanel(t);
      hideAllPanels();
      const wrap = $('#tool-page-wrap');
      if (wrap) wrap.classList.add('active');
      setCrumb(`<a data-nav="cat:${t.cat}">${escape(t.catLabel)}</a><span class="crumb-sep">/</span><span class="crumb-now">${escape(t.name)}</span>`);
      setBackVisible(true);
      setSbActive('cat:' + t.cat);
      try { history.pushState({ view: 'tool', tool: id }, '', '#' + id); } catch {}
      return;
    }

    // Fall-through: legacy tool-panel path
    const panel = document.getElementById('tool-' + id);
    if (!panel) { console.warn('No panel for tool', id); return; }

    // Move panel into the tool host so it renders inside .tpage-main.
    // Stash any panel already in the host back to storage so its state
    // (form inputs, in-flight jobs, etc.) is preserved.
    const host = $('#toolHost');
    if (host && panel.parentElement !== host) {
      const storage = $('#toolPanelStorage');
      if (storage) {
        while (host.firstChild) storage.appendChild(host.firstChild);
      }
      host.appendChild(panel);
    }

    // Render page-head with name + desc; render side cards
    $('#tpageTitle').textContent = t.name;
    $('#tpageSub').textContent = t.desc;
    renderSidePanel(t);

    // Force the panel to be active even if switchTool moved earlier
    hideAllPanels();
    panel.classList.add('active');
    // Show the tool-page wrapper itself
    const wrap = $('#tool-page-wrap');
    if (wrap) wrap.classList.add('active');

    setCrumb(`<a data-nav="cat:${t.cat}">${escape(t.catLabel)}</a><span class="crumb-sep">/</span><span class="crumb-now">${escape(t.name)}</span>`);
    setBackVisible(true);
    setSbActive('cat:' + t.cat);
    try { history.pushState({ view: 'tool', tool: id }, '', '#' + id); } catch {}
  }

  function renderSidePanel(t) {
    const side = $('#tpageSide');
    if (!side) return;
    const related = ALL.filter(x => x.cat === t.cat && x.id !== t.id).slice(0, 6);
    side.innerHTML = `
      <div class="side-card">
        <div class="side-head">Details</div>
        <dl class="side-dl">
          <dt>Runs</dt><dd>${t.where === 'browser' ? 'In your browser' : 'On our server'}</dd>
          <dt>Category</dt><dd>${escape(t.catLabel)}</dd>
          <dt>Retention</dt><dd>${t.where === 'browser' ? 'Never uploaded' : 'Deleted on completion'}</dd>
        </dl>
      </div>
      ${related.length ? `
      <div class="side-card">
        <div class="side-head">Related</div>
        <div class="side-rel">
          ${related.map(r => `<div class="side-rel-row" data-tool="${r.id}"><span>${escape(r.name)}</span>${icon('arrow', 12)}</div>`).join('')}
        </div>
      </div>` : ''}
    `;
    side.addEventListener('click', e => {
      const row = e.target.closest('[data-tool]');
      if (row) openTool(row.dataset.tool);
    }, { once: true });
  }

  // ── Search input ──────────────────────────────────────────────────────
  function wireSearch() {
    const input = $('#sbSearch');
    if (!input) return;
    let last = '';
    function tick() {
      const v = input.value.trim();
      if (v === last) return;
      last = v;
      if (v) showSearch(v); else showHome();
    }
    input.addEventListener('input', tick);
    document.addEventListener('keydown', e => {
      if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'k') {
        e.preventDefault();
        input.focus(); input.select();
      }
    });
  }

  // ── Account state ─────────────────────────────────────────────────────
  function setSignedIn(user) {
    const card = $('#sbUserCard');
    const pill = $('#topSignIn');
    if (user && user.email) {
      const name = user.display_name || user.username || user.email.split('@')[0];
      const initial = (name[0] || 'L').toUpperCase();
      const isPro = !!user.is_pro;
      if (card) {
        card.classList.add('sb-user');
        card.classList.remove('sb-pro-btn');
        card.innerHTML = `
          <div class="sb-user-avatar">${escape(initial)}</div>
          <div class="sb-user-meta">
            <div class="sb-user-name">${escape(name)}</div>
            <div class="sb-user-tier">${isPro ? '<span class="sb-user-tier-mark">✦</span> Pro member' : 'Free member'}</div>
          </div>
          <span class="sb-user-arrow">${icon('arrow', 12)}</span>
        `;
        card.href = '/account';
      }
      if (pill) {
        pill.classList.add('btn-account');
        pill.innerHTML = `
          <span class="btn-account-avatar">${escape(initial)}</span>
          <span class="btn-account-meta">
            <span class="btn-account-name">${escape(name)}</span>
            <span class="btn-account-tier ${isPro ? 'is-pro' : ''}">${isPro ? '<span class="btn-account-tier-mark">✦</span> Pro' : 'Free'}</span>
          </span>
        `;
        pill.href = '/account';
      }
    } else {
      if (card) {
        card.innerHTML = `
          <div class="sb-user-avatar">L</div>
          <div class="sb-user-meta">
            <div class="sb-user-name">Sign in</div>
            <div class="sb-user-tier">to save your usage</div>
          </div>
          <span class="sb-user-arrow">${icon('arrow', 12)}</span>
        `;
        card.href = '/account/login';
      }
      if (pill) {
        pill.textContent = 'Sign in';
        pill.href = '/account/login';
      }
    }
  }
  async function loadAccount() {
    try {
      const r = await fetch('/api/account/me', { credentials: 'same-origin' });
      if (!r.ok) { setSignedIn(null); return; }
      const j = await r.json();
      setSignedIn(j && (j.email || j.user) ? (j.user || j) : null);
    } catch { setSignedIn(null); }
  }

  // ── Status ticker (live from luma-status + local /api/health) ─────────
  const FALLBACK_TICKER = [
    { label: 'Server',  value: 'Luma Tools', kind: 'ver' },
    { label: 'Status',  value: 'online',     kind: 'ok' },
    { label: 'Build',   value: 'passing',    kind: 'ok' },
  ];
  function renderTicker(items) {
    const track = $('#sbTicker');
    if (!track) return;
    const set = items.concat(items); // duplicate so the loop is seamless
    track.innerHTML = set.map(it => `
      <span class="sb-ticker-item">
        <span class="sb-ticker-label">${escape(it.label)}:</span>
        <span class="sb-ticker-val sb-ticker-${it.kind}">${escape(it.value)}</span>
      </span>
      <span class="sb-ticker-sep">•</span>
    `).join('');
  }
  async function loadTicker() {
    const items = [];
    try {
      const r = await fetch('/api/health', { cache: 'no-store' });
      if (r.ok) {
        const j = await r.json();
        if (j.version) items.push({ label: 'Server', value: 'v' + j.version, kind: 'ver' });
        if (j.ffmpeg) items.push({ label: 'FFmpeg', value: j.ffmpeg, kind: 'ver' });
        if (j.ytdlp)  items.push({ label: 'yt-dlp', value: j.ytdlp,  kind: 'ver' });
      }
    } catch {}
    try {
      const r = await fetch('https://status.lumaplayground.com/api/status', { cache: 'no-store' });
      if (r.ok) {
        const j = await r.json();
        const tools = (j.sites || []).find(s => s.id === 'tools');
        if (tools) {
          items.push({ label: 'Uptime', value: (Math.round(tools.uptime_pct * 100) / 100).toFixed(2) + '%', kind: 'ok' });
          if (tools.latency_ms) items.push({ label: 'Latency', value: tools.latency_ms + 'ms', kind: 'ok' });
        }
      }
    } catch {}
    if (!items.length) renderTicker(FALLBACK_TICKER);
    else renderTicker(items.concat([{ label: 'Status', value: 'online', kind: 'ok' }]));
  }

  // ── History pass-through (delegates to existing toggleHistoryDrawer) ─
  function openHistory() {
    try { window.toggleHistoryDrawer && window.toggleHistoryDrawer(true); } catch {}
  }

  // ── Click delegation for crumbs / back / install / tool rows / etc ────
  function wireGlobalClicks() {
    document.addEventListener('click', e => {
      // Tool rows / featured cards / showcase pills / related rows
      // (anything with data-tool, anywhere in the document)
      const toolEl = e.target.closest('[data-tool]');
      if (toolEl) {
        e.preventDefault();
        openTool(toolEl.dataset.tool);
        return;
      }
      // Category cards on Home — scope to .cat-card so the attribute
      // can't collide with tool-internal data-cat usage (Bulk Installer's
      // filter chips, etc.)
      const catEl = e.target.closest('.cat-card[data-cat]');
      if (catEl) {
        e.preventDefault();
        showCategory(catEl.dataset.cat);
        return;
      }
      // Breadcrumb / nav links
      const nav = e.target.closest('[data-nav]');
      if (nav) {
        e.preventDefault();
        const v = nav.dataset.nav;
        if (v === 'home') showHome();
        else if (v === 'all') showAll();
        else if (v.startsWith('cat:')) showCategory(v.slice(4));
        return;
      }
      const back = e.target.closest('#topBackBtn');
      if (back) {
        e.preventDefault();
        const cur = window.LUMA_TOOL_BY_ID[window.state && window.state.currentTool];
        if (cur) showCategory(cur.cat); else showHome();
        return;
      }
      const inst = e.target.closest('#sbInstall, [data-act="install"]');
      if (inst) { e.preventDefault(); try { window.installPWA && window.installPWA(); } catch {} return; }
      const hist = e.target.closest('#sbHistory, [data-act="history"]');
      if (hist) { e.preventDefault(); openHistory(); return; }
      const proAct = e.target.closest('[data-act="pro"]');
      if (proAct) { e.preventDefault(); try { window.openUpgradeModal && window.openUpgradeModal(); } catch {} return; }
      const signup = e.target.closest('[data-act="signup"]');
      if (signup) { e.preventDefault(); location.href = '/account/login?signup=1'; return; }
    });
  }

  // ── File-type detector for QuickBar drop ──────────────────────────────
  function detectToolForFile(file) {
    const t = file.type || '';
    const n = file.name.toLowerCase();
    const ext = n.includes('.') ? n.split('.').pop() : '';
    if (t.startsWith('image/')) {
      if (['heic','heif'].includes(ext)) return 'image-convert';
      return 'image-compress';
    }
    if (t.startsWith('video/') || ['mp4','mkv','mov','avi','webm','flv','wmv','m4v'].includes(ext)) return 'video-compress';
    if (t.startsWith('audio/') || ['mp3','flac','wav','aac','ogg','opus','m4a'].includes(ext)) return 'audio-convert';
    if (t === 'application/pdf' || ext === 'pdf') return 'pdf-compress';
    if (['doc','docx'].includes(ext)) return 'word-to-pdf';
    if (['zip','7z','rar','tar','gz','iso','dmg'].includes(ext)) return 'archive-extract';
    return null;
  }

  // ── Dark / light mode toggle ──────────────────────────────────────────
  function applyTheme(mode) {
    document.documentElement.setAttribute('data-theme', mode);
    try { localStorage.setItem('lt_theme', mode); } catch {}
    const btn = $('#themeToggleBtn');
    if (btn) btn.innerHTML = mode === 'light'
      ? '<i class="fas fa-moon"></i>'
      : '<i class="fas fa-sun"></i>';
  }
  function toggleTheme() {
    const current = document.documentElement.getAttribute('data-theme') || 'dark';
    applyTheme(current === 'dark' ? 'light' : 'dark');
  }
  function initTheme() {
    let saved = 'dark';
    try { saved = localStorage.getItem('lt_theme') || 'dark'; } catch {}
    applyTheme(saved);
    const btn = $('#themeToggleBtn');
    if (btn) btn.addEventListener('click', toggleTheme);
  }

  // ── Logo click → home ─────────────────────────────────────────────────
  function wireLogoClick() {
    const head = document.querySelector('.sb-head');
    if (!head) return;
    head.style.cursor = 'pointer';
    head.setAttribute('role', 'link');
    head.setAttribute('tabindex', '0');
    head.setAttribute('title', 'Home');
    head.addEventListener('click', e => {
      // ignore the mobile-close button click
      if (e.target.closest('.sb-mobile-trigger')) return;
      e.preventDefault();
      showHome();
    });
    head.addEventListener('keydown', e => {
      if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); showHome(); }
    });
  }

  // ── AI model badge ────────────────────────────────────────────────────
  // List of tools that use the AI chain. Mirrors AI_BADGE_TOOLS in ui.js
  // but also includes ai-improve-notes and ai-mind-map alias.
  const AI_TOOLS = new Set([
    'ai-study-notes','ai-improve-notes','ai-flashcards','ai-quiz','ai-paraphrase',
    'citation-gen','mind-map','youtube-summary',
  ]);

  // Current chain — matches the live Groq /v1/models list (Nov 2025+)
  const AI_CHAIN = [
    { id: 'llama-3.3-70b-versatile',                    label: 'Llama 3.3 70B',  prov: 'Groq' },
    { id: 'openai/gpt-oss-120b',                        label: 'GPT-OSS 120B',   prov: 'Groq' },
    { id: 'meta-llama/llama-4-scout-17b-16e-instruct',  label: 'Llama 4 Scout',  prov: 'Groq' },
    { id: 'qwen/qwen3-32b',                             label: 'Qwen 3 32B',     prov: 'Groq' },
    { id: 'openai/gpt-oss-20b',                         label: 'GPT-OSS 20B',    prov: 'Groq' },
    { id: 'cerebras:gpt-oss-120b',                      label: 'GPT-OSS 120B',   prov: 'Cerebras' },
    { id: 'gemini:gemini-2.0-flash',                    label: 'Gemini 2.0 Flash', prov: 'Google' },
    { id: 'llama-3.1-8b-instant',                       label: 'Llama 3.1 8B',   prov: 'Groq' },
    { id: 'ollama:llama3.1:8b',                         label: 'Llama 3.1 8B',   prov: 'Local Ollama' },
  ];

  function setAIModelBadge(modelId, state) {
    const b = $('#tpvModelBadge');
    if (!b) return;
    if (state === 'hidden') { b.style.display = 'none'; b.innerHTML = ''; return; }
    b.style.display = '';
    if (state === 'checking') {
      b.className = 'tpv-model-badge is-checking';
      b.innerHTML = `<span class="tpv-mb-dot"></span><span>Checking AI…</span>`;
      return;
    }
    if (state === 'error') {
      b.className = 'tpv-model-badge is-error';
      b.innerHTML = `<span class="tpv-mb-dot"></span><span>AI unavailable</span>` + chainTooltip(null);
      return;
    }
    if (!modelId) {
      b.className = 'tpv-model-badge';
      b.innerHTML = `<span class="tpv-mb-dot"></span><span>Auto</span>` + chainTooltip(null);
      return;
    }
    const idx = AI_CHAIN.findIndex(m => m.id === modelId);
    const m = idx >= 0 ? AI_CHAIN[idx] : { label: shortenModelName(modelId), prov: 'Custom' };
    // Class hints for visual state
    let cls = 'tpv-model-badge';
    if (idx === 0) cls += '';                       // primary (purple)
    else if (idx > 0 && idx < 5) cls += ' is-fallback';   // amber for any fallback
    else if (idx >= 5) cls += ' is-fallback';            // amber for cross-provider
    b.className = cls;
    b.innerHTML = `<span class="tpv-mb-dot"></span><span>${escape(m.label)}</span>` + chainTooltip(modelId);
  }
  function shortenModelName(id) {
    return String(id).replace(/^.*\//, '').replace(/^cerebras:/, '').replace(/^gemini:/, '').replace(/^ollama:/, 'Local ');
  }
  function chainTooltip(activeId) {
    const rows = AI_CHAIN.map((m, i) => `
      <div class="tpv-mb-tip-row${m.id === activeId ? ' active' : ''}">
        <span class="step">${i + 1}.</span>
        <span class="name">${escape(m.label)}</span>
        <span class="prov">${escape(m.prov)}</span>
      </div>
    `).join('');
    return `
      <div class="tpv-mb-tip">
        <div class="tpv-mb-tip-head">AI fallback chain</div>
        <div class="tpv-mb-tip-sub">Each request tries these models in order. The highlighted one served your last response.</div>
        ${rows}
      </div>
    `;
  }
  async function probeAIStatus() {
    try {
      const r = await fetch('/api/ai-status', { cache: 'no-store' });
      if (!r.ok) { setAIModelBadge(null, 'error'); return; }
      const j = await r.json();
      setAIModelBadge(j.model || 'llama-3.3-70b-versatile');
    } catch { setAIModelBadge(null, 'error'); }
  }
  // Sniff any fetch JSON response globally for { model_used } / { model }
  function installAIResponseSniffer() {
    const orig = window.fetch;
    window.fetch = async function (...args) {
      const res = await orig.apply(this, args);
      try {
        const url = typeof args[0] === 'string' ? args[0] : (args[0] && args[0].url) || '';
        if (/\/api\/(tools\/ai-|mind-map|youtube-summary|ai-status)/.test(url) && (res.headers.get('content-type') || '').includes('json')) {
          // Clone so the original consumer still gets to read the body
          res.clone().json().then(j => {
            if (!j) return;
            const m = j.model_used || j.model;
            if (m) setAIModelBadge(m);
          }).catch(() => {});
        }
      } catch {}
      return res;
    };
  }
  // Expose for legacy code that might want to push a model update
  window.LumaShellSetAIModel = setAIModelBadge;

  // ── Platform-aware ⌘/Ctrl shortcut label ──────────────────────────────
  function isMac() {
    if (navigator.userAgentData && navigator.userAgentData.platform) {
      return /mac/i.test(navigator.userAgentData.platform);
    }
    return /Mac|iPhone|iPad|iPod/.test(navigator.platform || navigator.userAgent || '');
  }
  function applyShortcutLabels() {
    const sym = isMac() ? '⌘K' : 'Ctrl K';
    document.querySelectorAll('.sb-search .kbd').forEach(el => { el.textContent = sym; });
  }

  // ── Initial route from URL hash ───────────────────────────────────────
  function routeFromHash() {
    const h = location.hash.replace(/^#/, '');
    if (!h) { showHome(); return; }
    if (h === 'all') { showAll(); return; }
    if (h.startsWith('cat=')) { showCategory(h.slice(4)); return; }
    if (window.LUMA_TOOL_BY_ID[h]) { openTool(h); return; }
    showHome();
  }

  // ── Boot ──────────────────────────────────────────────────────────────
  function boot() {
    renderSidebar();
    renderHome();
    wireSearch();
    wireGlobalClicks();
    wireLogoClick();
    applyShortcutLabels();
    installAIResponseSniffer();
    initTheme();
    if (window.lumaNotifyRequestPermission) window.lumaNotifyRequestPermission();
    loadAccount();
    loadTicker();
    setInterval(loadTicker, 60000);
    routeFromHash();
    window.addEventListener('popstate', routeFromHash);

    // Wrap switchTool so calls from legacy code (or hashchange) update the
    // new chrome. Listing handlers below also call afterToolSwitch().
    const orig = window.switchTool;
    if (typeof orig === 'function') {
      window.switchTool = function (id) {
        try { orig.call(this, id); } catch (e) { console.error(e); }
        if (id === 'landing') showHome();
        else if (id === 'all') showAll();
        else if (id === 'categories') { /* no-op */ }
        else if (id === 'search') { /* no-op */ }
        else if (window.LUMA_TOOL_BY_ID[id]) afterToolSwitch(id);
      };
    }
  }

  // ── Tool rating ─────────────────────────────────────────────────────────
  window.lumaRate = async function(toolId, rating, btn) {
    try {
      await fetch('/api/feedback-rate', {
        method: 'POST', headers: {'Content-Type':'application/json'},
        body: JSON.stringify({ tool: toolId, rating }),
      });
    } catch {}
    const container = btn.closest('.luma-rating');
    if (container) {
      container.innerHTML = `<span class="luma-rating-done">${rating === 'up' ? '👍 Thanks!' : '👎 Noted — we\'ll improve it.'}</span>`;
    }
  };

  // Expose for debugging / external scripts
  window.LumaShell = {
    showHome, showAll, showCategory, showSearch, openTool, loadTicker, loadAccount,
  };

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', boot);
  } else {
    boot();
  }
})();
