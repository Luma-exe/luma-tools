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

function showToast(message, type = 'info', duration = 3500) {
    const existing = document.querySelector('.toast'); if (existing) existing.remove();
    const toast = document.createElement('div'); toast.className = `toast ${type}`;
    const icon = type === 'error' ? 'fas fa-exclamation-circle' : type === 'success' ? 'fas fa-check-circle' : 'fas fa-info-circle';
    toast.innerHTML = `<i class="${icon}"></i> ${escapeHTML(message)}`;
    document.body.appendChild(toast);
    setTimeout(() => toast.remove(), duration);
}

// ═══════════════════════════════════════════════════════════════════════════
// LUMA VANTAGE ANALYTICS
// ═══════════════════════════════════════════════════════════════════════════
const LV_KEYS = {
    sessionId: 'lv_session_id',
    sessionStartedAt: 'lv_session_started_at',
    firstTouch: 'lv_first_touch',
    lastSeenAt: 'lv_last_seen_at',
    firstValueCount: 'lv_first_value_count',
};

const LV_TRACK_DEBOUNCE = Object.create(null);

function randomId(prefix) {
    const rand = Math.random().toString(36).slice(2, 10);
    return `${prefix}_${Date.now().toString(36)}_${rand}`;
}

function getSessionId() {
    try {
        let sid = sessionStorage.getItem(LV_KEYS.sessionId);
        if (!sid) {
            sid = randomId('sess');
            sessionStorage.setItem(LV_KEYS.sessionId, sid);
            sessionStorage.setItem(LV_KEYS.sessionStartedAt, String(Date.now()));
        }
        return sid;
    } catch {
        return randomId('sess');
    }
}

function getFirstTouch() {
    const nowParams = new URLSearchParams(window.location.search || '');
    const next = {
        utm_source: nowParams.get('utm_source') || '(direct)',
        utm_medium: nowParams.get('utm_medium') || '(none)',
        utm_campaign: nowParams.get('utm_campaign') || '(none)',
    };
    try {
        const savedRaw = localStorage.getItem(LV_KEYS.firstTouch);
        if (savedRaw) return JSON.parse(savedRaw);
        localStorage.setItem(LV_KEYS.firstTouch, JSON.stringify(next));
    } catch {}
    return next;
}

function getReferrerDomain() {
    try {
        if (!document.referrer) return '(direct)';
        return new URL(document.referrer).hostname || '(direct)';
    } catch {
        return '(direct)';
    }
}

function getDeviceType() {
    const w = window.innerWidth || 1280;
    if (w <= 768) return 'mobile';
    if (w <= 1024) return 'tablet';
    return 'desktop';
}

function getBehaviorSegment() {
    const count = Number(localStorage.getItem(LV_KEYS.firstValueCount) || '0');
    if (count >= 8) return 'power_user';
    if (count >= 2) return 'casual_user';
    return 'new_user';
}

function isReturningUser() {
    const lastSeen = Number(localStorage.getItem(LV_KEYS.lastSeenAt) || '0');
    return !!lastSeen && Date.now() - lastSeen > 1000 * 60 * 60 * 12;
}

function buildAnalyticsProps(extra) {
    const firstTouch = getFirstTouch();
    const defaults = {
        session_id: getSessionId(),
        tool_id: state?.currentTool || 'landing',
        tool_category: document.querySelector(`.nav-item[data-tool="${state?.currentTool || 'landing'}"]`)?.closest('.nav-category')?.querySelector('.nav-category-title')?.textContent?.trim() || 'general',
        source_page: window.location.pathname || '/',
        utm_source: firstTouch.utm_source,
        utm_medium: firstTouch.utm_medium,
        utm_campaign: firstTouch.utm_campaign,
        is_returning_user: isReturningUser(),
        experiment_variant: localStorage.getItem('lt_paywall_variant') || 'control',
        referrer_domain: getReferrerDomain(),
        device_type: getDeviceType(),
        behavior_segment: getBehaviorSegment(),
    };
    return Object.assign(defaults, extra || {});
}

function lvTrack(eventName, props = {}, opts = {}) {
    if (!eventName) return;
    const debounceMs = typeof opts.debounceMs === 'number' ? opts.debounceMs : 900;
    const dedupeKey = opts.dedupeKey || `${eventName}:${props.tool_id || state?.currentTool || 'landing'}`;
    const now = Date.now();
    if (LV_TRACK_DEBOUNCE[dedupeKey] && now - LV_TRACK_DEBOUNCE[dedupeKey] < debounceMs) return;
    LV_TRACK_DEBOUNCE[dedupeKey] = now;

    const finalProps = buildAnalyticsProps(props);
    const required = ['session_id', 'tool_id', 'tool_category', 'source_page', 'utm_source', 'utm_medium', 'utm_campaign', 'is_returning_user'];
    for (const key of required) {
        if (finalProps[key] == null || finalProps[key] === '') return;
    }

    try {
        if (window.LumaVantage?.track) {
            window.LumaVantage.track(eventName, finalProps);
            return;
        }
        if (window._lvProxy?.track) {
            window._lvProxy.track(eventName, finalProps);
            return;
        }
    } catch {}
}

function initLumaVantageTracking() {
    const wasReturningUser = isReturningUser();
    window._lvWasReturningUser = wasReturningUser;
    try { localStorage.setItem(LV_KEYS.lastSeenAt, String(Date.now())); } catch {}
    lvTrack('landing_view', { tool_id: 'landing', tool_category: 'landing', source_page: 'landing' }, { dedupeKey: 'landing_view', debounceMs: 60000 });
    if (wasReturningUser) {
        lvTrack('return_visit', { tool_id: 'landing', tool_category: 'landing', source_page: 'landing' }, { dedupeKey: 'return_visit', debounceMs: 60000 });
    }
    lvTrack('user_segment_evaluated', {
        tool_id: 'landing',
        tool_category: 'segmentation',
        source_page: 'session_start',
        behavior_segment: getBehaviorSegment(),
        first_value_actions: Number(localStorage.getItem(LV_KEYS.firstValueCount) || '0'),
    }, { dedupeKey: 'user_segment_evaluated', debounceMs: 60000 });
}

function trackFirstValueAction(toolId, meta = {}) {
    try {
        const current = Number(localStorage.getItem(LV_KEYS.firstValueCount) || '0') + 1;
        localStorage.setItem(LV_KEYS.firstValueCount, String(current));
    } catch {}
    lvTrack('first_value_action', Object.assign({ tool_id: toolId || state?.currentTool || 'landing' }, meta), { dedupeKey: `first_value_action:${toolId || state?.currentTool || 'landing'}`, debounceMs: 1200 });
}

document.addEventListener('DOMContentLoaded', initLumaVantageTracking);

// ═══════════════════════════════════════════════════════════════════════════
// LIVE LOGS (CLIENT + SERVER MERGE)
// ═══════════════════════════════════════════════════════════════════════════
const LiveLogs = (() => {
    const stateByTool = Object.create(null);
    const MAX_LINES = 220;

    function getState(toolId) {
        if (!stateByTool[toolId]) {
            stateByTool[toolId] = { lines: [], seenSeq: 0, unread: 0, autoScroll: true };
        }
        return stateByTool[toolId];
    }

    function lineHtml(line) {
        const lvl = line.level || 'info';
        const cls = lvl === 'error' ? 'll-line-error' : (lvl === 'success' ? 'll-line-success' : 'll-line-info');
        const ts = new Date(line.ts || Date.now()).toLocaleTimeString();
        return `<div class="ll-line ${cls}"><span class="ll-time">${ts}</span><span class="ll-msg">${escapeHTML(line.msg || '')}</span></div>`;
    }

    function render(toolId) {
        const panel = document.querySelector(`#tool-${toolId} .live-logs-panel`);
        if (!panel) return;
        const list = panel.querySelector('.live-logs-list');
        const badge = panel.querySelector('.live-logs-unread');
        const st = getState(toolId);
        if (list) list.innerHTML = st.lines.map(lineHtml).join('');
        if (badge) {
            badge.textContent = st.unread > 0 ? String(st.unread) : '';
            badge.classList.toggle('hidden', st.unread === 0);
        }
        if (list && st.autoScroll) list.scrollTop = list.scrollHeight;
    }

    function add(toolId, msg, level = 'info') {
        if (!toolId || !msg) return;
        const st = getState(toolId);
        st.lines.push({ ts: Date.now(), level, msg: String(msg) });
        if (st.lines.length > MAX_LINES) st.lines.splice(0, st.lines.length - MAX_LINES);
        const panel = document.querySelector(`#tool-${toolId} .live-logs-panel`);
        if (panel?.classList.contains('collapsed')) st.unread += 1;
        render(toolId);
    }

    function ingestServerLogs(toolId, logs, logSeq) {
        if (!toolId || !Array.isArray(logs) || logs.length === 0) return;
        const st = getState(toolId);
        const incoming = logs.filter(l => Number(l.seq || 0) > st.seenSeq);
        for (const l of incoming) {
            st.lines.push({
                ts: Number(l.ts || Date.now()),
                level: String(l.level || 'info'),
                msg: `[server] ${String(l.msg || '')}`,
            });
        }
        st.seenSeq = Math.max(st.seenSeq, Number(logSeq || 0));
        if (st.lines.length > MAX_LINES) st.lines.splice(0, st.lines.length - MAX_LINES);
        const panel = document.querySelector(`#tool-${toolId} .live-logs-panel`);
        if (panel?.classList.contains('collapsed') && incoming.length > 0) st.unread += incoming.length;
        render(toolId);
    }

    function reset(toolId) {
        const st = getState(toolId);
        st.lines = [];
        st.seenSeq = 0;
        st.unread = 0;
        st.autoScroll = true;
        render(toolId);
    }

    function clearView(toolId) {
        const st = getState(toolId);
        st.lines = [];
        st.unread = 0;
        render(toolId);
    }

    function markRead(toolId) {
        const st = getState(toolId);
        st.unread = 0;
        render(toolId);
    }

    function setAutoScroll(toolId, enabled) {
        const st = getState(toolId);
        st.autoScroll = !!enabled;
    }

    return { add, ingestServerLogs, reset, clearView, markRead, setAutoScroll, render };
})();

window.LiveLogs = LiveLogs;
