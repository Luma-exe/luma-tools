// ═══════════════════════════════════════════════════════════════════════════
// DEVELOPER API — key management UI for the #api-access panel
// ═══════════════════════════════════════════════════════════════════════════

(function () {
    let _keys = [];

    function fmtDate(ts) {
        return new Date(ts * 1000).toLocaleDateString(undefined, { year: 'numeric', month: 'short', day: 'numeric' });
    }

    function renderKeyList() {
        const el = document.getElementById('apiKeyList');
        if (!el) return;
        if (!_keys.length) {
            el.innerHTML = '<span class="api-empty">No API keys yet. Create one below.</span>';
            return;
        }
        el.innerHTML = _keys.map(k => `
          <div class="api-key-row">
            <div class="api-key-info">
              <span class="api-key-label-text">${escapeHtml(k.label || 'API key')}</span>
              <span class="api-key-meta">Created ${fmtDate(k.created)} &middot; ${k.calls} calls total</span>
            </div>
            <code class="api-key-val api-key-val-sm">${escapeHtml(k.key_masked)}</code>
            <button class="api-key-revoke-btn" onclick="apiRevokeKey('${escapeHtml(k.key)}')" title="Revoke key">
              <i class="fas fa-trash-alt"></i>
            </button>
          </div>
        `).join('');
    }

    function escapeHtml(s) {
        return String(s || '').replace(/[&<>"']/g, c =>
            ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
    }

    window.apiLoadKeys = async function () {
        const el = document.getElementById('apiKeyList');
        if (!el) return;
        try {
            const res = await fetch('/api/keys/list');
            const data = await res.json();
            _keys = data.keys || [];
            renderKeyList();
        } catch (e) {
            if (el) el.innerHTML = '<span class="api-empty" style="color:var(--error)">Failed to load keys. Check your connection.</span>';
        }
    };

    window.apiCreateKey = async function () {
        const labelEl = document.getElementById('apiKeyLabel');
        const label = (labelEl && labelEl.value.trim()) || 'My API key';
        const btn = document.querySelector('#tool-api-access .process-btn');
        if (btn) { btn.disabled = true; btn.innerHTML = '<i class="fas fa-circle-notch fa-spin"></i> Creating…'; }
        try {
            const res = await fetch('/api/keys/create', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ label })
            });
            const data = await res.json();
            if (!res.ok) { showToast(data.error || 'Failed to create key', 'error'); return; }

            // Show the full key once (it won't be shown in full again)
            const newKeyDiv = document.getElementById('apiNewKey');
            const newKeyVal = document.getElementById('apiNewKeyVal');
            if (newKeyDiv && newKeyVal) {
                newKeyVal.textContent = data.key;
                newKeyDiv.classList.remove('hidden');
            }
            if (labelEl) labelEl.value = '';
            await apiLoadKeys();
            showToast('API key created!', 'success');
        } catch (e) {
            showToast('Failed to create key', 'error');
        } finally {
            if (btn) { btn.disabled = false; btn.innerHTML = '<i class="fas fa-plus"></i> Create key'; }
        }
    };

    window.apiRevokeKey = async function (key) {
        if (!confirm('Revoke this API key? Any apps using it will stop working.')) return;
        try {
            const res = await fetch('/api/keys/revoke', {
                method: 'DELETE',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ key })
            });
            const data = await res.json();
            if (!res.ok) { showToast(data.error || 'Failed to revoke key', 'error'); return; }
            _keys = _keys.filter(k => k.key !== key);
            renderKeyList();
            showToast('Key revoked', 'success');
        } catch (e) {
            showToast('Failed to revoke key', 'error');
        }
    };

    window.apiCopyNewKey = function () {
        const val = document.getElementById('apiNewKeyVal');
        if (!val) return;
        navigator.clipboard.writeText(val.textContent).then(
            () => showToast('Key copied to clipboard!', 'success'),
            () => showToast('Copy the key manually from the box above', 'info')
        );
    };

    async function loadEndpointDocs() {
        const el = document.getElementById('apiEndpointTable');
        if (!el) return;
        try {
            const res = await fetch('/api/keys/docs');
            const data = await res.json();
            const rows = (data.endpoints || []).map(ep => `
              <div class="api-ep-row">
                <span class="api-ep-method api-ep-${ep.method.toLowerCase()}">${ep.method}</span>
                <code class="api-ep-path">${escapeHtml(ep.path)}</code>
                <span class="api-ep-desc">${escapeHtml(ep.desc)}</span>
              </div>
            `).join('');
            el.innerHTML = rows || '<span class="api-empty">No endpoints listed.</span>';
        } catch (e) {
            if (el) el.innerHTML = '<span class="api-empty">Could not load endpoint list.</span>';
        }
    }

    // Init when the api-access panel becomes active
    document.addEventListener('lt:panelOpen', function (e) {
        if (e.detail && e.detail.toolId === 'api-access') {
            apiLoadKeys();
            loadEndpointDocs();
        }
    });
})();
