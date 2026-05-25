const k = document.getElementById('k');
const s = document.getElementById('status');
chrome.storage.sync.get(['apiKey'], d => {
    if (d.apiKey) k.value = d.apiKey;
});
document.getElementById('save').addEventListener('click', async () => {
    const v = (k.value || '').trim();
    if (!v.startsWith('lt_')) {
        s.textContent = 'Key should start with lt_'; s.className = 'status err';
        return;
    }
    chrome.storage.sync.set({ apiKey: v }, async () => {
        s.textContent = 'Saved. Verifying…'; s.className = 'status';
        try {
            const r = await fetch('https://tools.lumaplayground.com/api/account/quota', {
                headers: { 'Authorization': 'Bearer ' + v }
            });
            if (!r.ok) {
                s.textContent = 'Key rejected (HTTP ' + r.status + ').'; s.className = 'status err';
                return;
            }
            const d = await r.json();
            s.textContent = '✓ ' + d.plan + (d.ai && d.ai.unlimited ? ' (unlimited AI)' : '');
            s.className = 'status ok';
        } catch (e) {
            s.textContent = 'Verify failed: ' + e.message; s.className = 'status err';
        }
    });
});
