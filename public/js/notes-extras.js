// ════════════════════════════════════════════════════════════════════════
// LUMA TOOLS — Study Notes extras
//  · Auto-save drafts every 30s to localStorage, show last 5 versions
//  · "Explain this" floating button on text selection inside notes preview
//  · OCR result: inline Copy + "Open in Study Notes" buttons
// ════════════════════════════════════════════════════════════════════════
(function () {
  const DRAFT_KEY = 'lt_notes_drafts';
  const MAX_DRAFTS = 5;

  // ── Auto-save ─────────────────────────────────────────────────────────
  let _saveTimer = null;

  function scheduleSave(toolId, content) {
    clearTimeout(_saveTimer);
    _saveTimer = setTimeout(() => saveDraft(toolId, content), 30000);
  }

  function saveDraft(toolId, content) {
    if (!content || content.length < 100) return;
    try {
      let drafts = JSON.parse(localStorage.getItem(DRAFT_KEY) || '[]');
      drafts.unshift({ toolId, content, ts: Date.now(), len: content.length });
      drafts = drafts.slice(0, MAX_DRAFTS);
      localStorage.setItem(DRAFT_KEY, JSON.stringify(drafts));
      updateRestoreDropdown(toolId);
    } catch {}
  }

  function getDrafts(toolId) {
    try {
      const all = JSON.parse(localStorage.getItem(DRAFT_KEY) || '[]');
      return all.filter(d => d.toolId === toolId);
    } catch { return []; }
  }

  function formatAgo(ts) {
    const s = Math.floor((Date.now() - ts) / 1000);
    if (s < 60) return 'just now';
    const m = Math.floor(s / 60);
    if (m < 60) return `${m}m ago`;
    const h = Math.floor(m / 60);
    return h < 24 ? `${h}h ago` : `${Math.floor(h/24)}d ago`;
  }

  function updateRestoreDropdown(toolId) {
    const dd = document.getElementById('lt-restore-dd');
    if (!dd) return;
    const drafts = getDrafts(toolId);
    if (!drafts.length) { dd.style.display = 'none'; return; }
    dd.style.display = '';
    dd.innerHTML = `<option value="">⏱ Restore a draft…</option>` +
      drafts.map((d, i) => `<option value="${i}">${formatAgo(d.ts)} · ${(d.len/1000).toFixed(1)}k chars</option>`).join('');
    dd.onchange = () => {
      const i = parseInt(dd.value);
      if (isNaN(i)) return;
      const draft = drafts[i];
      if (!draft) return;
      dd.value = '';
      // Inject into the active notes pane
      const pane = document.querySelector('.notes-preview-pane .notes-pre-content');
      if (pane) { pane.textContent = draft.content; scheduleSave(toolId, draft.content); }
    };
  }

  // ── Wire auto-save into the notes pane when it's created ─────────────
  // MutationObserver watches for .notes-preview-pane appearing in the DOM.
  const obs = new MutationObserver(mutations => {
    for (const m of mutations) {
      for (const node of m.addedNodes) {
        if (!(node instanceof HTMLElement)) continue;
        const pane = node.classList?.contains('notes-preview-pane') ? node
                   : node.querySelector?.('.notes-preview-pane');
        if (!pane) continue;
        wirePane(pane);
      }
    }
  });
  obs.observe(document.body, { subtree: true, childList: true });

  function wirePane(pane) {
    // Find the pre element (raw notes text)
    const pre = pane.querySelector('pre, .notes-pre-content');
    if (!pre) return;
    const toolId = 'ai-study-notes';

    // Inject restore dropdown into the pane header actions (if present)
    const actionBar = pane.querySelector('.notes-preview-actions, .notes-action-bar, .notes-btn-group');
    if (actionBar && !actionBar.querySelector('#lt-restore-dd')) {
      const dd = document.createElement('select');
      dd.id = 'lt-restore-dd';
      dd.style.cssText = 'font:inherit;font-size:12px;background:var(--surface-2);border:1px solid var(--border);border-radius:var(--r-sm);color:var(--text-2);padding:4px 8px;cursor:pointer;display:none';
      actionBar.appendChild(dd);
    }

    // Save draft now and start the timer
    saveDraft(toolId, pre.textContent || '');
    updateRestoreDropdown(toolId);

    // Wire the 30s timer on content changes (MutationObserver on the pre)
    new MutationObserver(() => scheduleSave(toolId, pre.textContent || ''))
      .observe(pre, { characterData: true, subtree: true, childList: true });
  }

  // ── "Explain this section" floating button ────────────────────────────
  let _explainBtn = null;
  let _explainPanel = null;

  function createExplainBtn() {
    if (_explainBtn) return;
    _explainBtn = document.createElement('button');
    _explainBtn.id = 'lt-explain-btn';
    _explainBtn.innerHTML = '✨ Explain';
    _explainBtn.style.cssText = `
      position:fixed;z-index:9999;display:none;
      background:linear-gradient(135deg,var(--accent),var(--accent-light));
      color:#fff;border:0;border-radius:var(--r-sm);
      padding:5px 12px;font:600 12.5px/1 var(--font);
      box-shadow:0 4px 14px var(--accent-glow);cursor:pointer;
    `;
    document.body.appendChild(_explainBtn);

    _explainPanel = document.createElement('div');
    _explainPanel.id = 'lt-explain-panel';
    _explainPanel.style.cssText = `
      position:fixed;z-index:9998;display:none;
      max-width:380px;width:90vw;
      background:rgba(12,12,20,0.97);
      backdrop-filter:blur(20px) saturate(160%);
      border:1px solid var(--border-2);border-radius:var(--r-md);
      padding:14px 16px;font-size:13px;line-height:1.55;color:var(--text-2);
      box-shadow:0 16px 40px rgba(0,0,0,.55);
    `;
    document.body.appendChild(_explainPanel);

    _explainBtn.addEventListener('click', async () => {
      const sel = window.getSelection();
      if (!sel || !sel.toString().trim()) return;
      const text = sel.toString().trim().slice(0, 1200);
      _explainBtn.style.display = 'none';
      _explainPanel.innerHTML = '<i class="fas fa-spinner fa-spin" style="color:var(--accent)"></i> Explaining…';
      _explainPanel.style.display = '';

      try {
        const r = await fetch('/api/tools/ai-paraphrase', {
          method: 'POST',
          body: (() => { const f = new FormData(); f.append('text', text); f.append('tone', 'simplified'); return f; })(),
        });
        const j = await r.json();
        const exp = j.result || j.error || 'Could not explain.';
        _explainPanel.innerHTML = `
          <div style="font-size:11px;font-weight:600;text-transform:uppercase;letter-spacing:.06em;color:var(--text-3);margin-bottom:8px">Explanation</div>
          <div style="color:var(--text)">${escapeHtml(exp)}</div>
          <button id="lt-explain-close" style="margin-top:10px;background:none;border:0;color:var(--text-3);font:inherit;font-size:12px;cursor:pointer">✕ Close</button>
        `;
        document.getElementById('lt-explain-close').onclick = closeExplain;
      } catch (e) {
        _explainPanel.innerHTML = `<span style="color:#f87171">Failed to explain: ${escapeHtml(String(e))}</span>`;
      }
    });

    document.addEventListener('click', e => {
      if (!_explainBtn.contains(e.target) && !_explainPanel.contains(e.target)) closeExplain();
    });
    document.addEventListener('selectionchange', onSelectionChange);
  }

  function closeExplain() {
    if (_explainBtn) _explainBtn.style.display = 'none';
    if (_explainPanel) _explainPanel.style.display = 'none';
  }

  function onSelectionChange() {
    const sel = window.getSelection();
    if (!sel || !sel.toString().trim()) { closeExplain(); return; }
    // Only show if inside a notes pane
    const range = sel.getRangeAt(0);
    if (!range.commonAncestorContainer.closest?.('.notes-preview-pane,.notes-content,.notes-pre-content')) return;
    if (!_explainBtn) createExplainBtn();
    const rect = range.getBoundingClientRect();
    _explainBtn.style.left = Math.max(10, Math.min(window.innerWidth - 120, rect.left + rect.width / 2 - 50)) + 'px';
    _explainBtn.style.top  = Math.max(10, rect.top - 42) + 'px';
    _explainBtn.style.display = 'block';
    if (_explainPanel) _explainPanel.style.display = 'none';
  }

  // ── OCR result: inject Copy + Open in Study Notes ─────────────────────
  // Watches for the OCR result section to become visible
  const ocrObs = new MutationObserver(() => {
    const ocrResult = document.querySelector('.result-section[data-tool="ocr"]');
    if (!ocrResult || ocrResult.classList.contains('hidden')) return;
    if (ocrResult.dataset.ltEnhanced) return;
    ocrResult.dataset.ltEnhanced = '1';
    addOcrActions(ocrResult);
  });
  ocrObs.observe(document.body, { subtree: true, attributes: true, attributeFilter: ['class'] });

  function addOcrActions(result) {
    // Find the download link to get the text
    const dl = result.querySelector('.result-download, a[download]');
    if (!dl) return;
    const bar = document.createElement('div');
    bar.style.cssText = 'display:flex;gap:8px;margin-top:8px;flex-wrap:wrap';
    const copyBtn = document.createElement('button');
    copyBtn.className = 'btn-ghost';
    copyBtn.style.cssText = 'font-size:12.5px;padding:7px 12px';
    copyBtn.innerHTML = '<i class="fas fa-copy"></i> Copy text';
    copyBtn.addEventListener('click', async () => {
      if (dl._objectUrl) {
        try {
          const text = await fetch(dl._objectUrl).then(r => r.text());
          await navigator.clipboard.writeText(text);
          copyBtn.innerHTML = '<i class="fas fa-check"></i> Copied!';
          setTimeout(() => { copyBtn.innerHTML = '<i class="fas fa-copy"></i> Copy text'; }, 2000);
        } catch {}
      }
    });
    const studyBtn = document.createElement('button');
    studyBtn.className = 'btn-ghost';
    studyBtn.style.cssText = 'font-size:12.5px;padding:7px 12px';
    studyBtn.innerHTML = '<i class="fas fa-brain"></i> Open in Study Notes';
    studyBtn.addEventListener('click', async () => {
      if (!dl._objectUrl) return;
      try {
        const text = await fetch(dl._objectUrl).then(r => r.text());
        if (window.LumaShell) window.LumaShell.openTool('ai-study-notes', () => {
          const ta = document.querySelector('#tool-ai-study-notes textarea, #study-notes-text-input');
          if (ta) { ta.value = text; ta.dispatchEvent(new Event('input', { bubbles: true })); }
          // Switch input mode to paste
          const pastePill = document.querySelector('.preset-grid[data-tool="study-notes-input-mode"] [data-val="paste"]');
          if (pastePill) pastePill.click();
        });
      } catch {}
    });
    bar.appendChild(copyBtn);
    bar.appendChild(studyBtn);
    result.appendChild(bar);
  }

  function escapeHtml(s) {
    return String(s || '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  }
})();
