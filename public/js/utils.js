// ═══════════════════════════════════════════════════════════════════════════
// UTILITIES
// ═══════════════════════════════════════════════════════════════════════════

function formatBytes(bytes) {
    if (!bytes || bytes === 0) return '0 B';
    const k = 1024, sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];
}

// Alias used in downloader for file sizes (same logic)
function formatSize(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1048576) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / 1048576).toFixed(1) + ' MB';
}

function lumaTag(filename) {
    if (!filename) return 'file_LumaTools';
    const dot = filename.lastIndexOf('.');
    if (dot <= 0) return filename + '_LumaTools';
    const name = filename.slice(0, dot), ext = filename.slice(dot);
    if (name.endsWith('_LumaTools')) return filename;
    return name + '_LumaTools' + ext;
}

function escapeHTML(str) {
    const div = document.createElement('div'); div.textContent = str; return div.innerHTML;
}

function showToast(message, type = 'info') {
    const existing = document.querySelector('.toast'); if (existing) existing.remove();
    const toast = document.createElement('div'); toast.className = `toast ${type}`;
    const icon = type === 'error' ? 'fas fa-exclamation-circle' : type === 'success' ? 'fas fa-check-circle' : 'fas fa-info-circle';
    toast.innerHTML = `<i class="${icon}"></i> ${escapeHTML(message)}`;
    document.body.appendChild(toast);
    setTimeout(() => toast.remove(), 3500);
}
