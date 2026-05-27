// ════════════════════════════════════════════════════════════════════════
// LUMA TOOLS — Web Notification helper
//
// Usage:
//   lumaNotify('Video compress complete', 'Your file is ready to download.', 'video-compress');
//
// • Requests permission once on first call (browsers require user gesture
//   to prompt — we trigger it after the user clicks a "Run" button so
//   the gesture requirement is satisfied).
// • Falls back silently if the browser doesn't support Notifications.
// • Only fires when the tab is not visible (no point interrupting).
// ════════════════════════════════════════════════════════════════════════
(function () {
  let _permAsked = false;

  async function requestPermission() {
    if (!('Notification' in window)) return false;
    if (Notification.permission === 'granted') return true;
    if (Notification.permission === 'denied' || _permAsked) return false;
    _permAsked = true;
    const result = await Notification.requestPermission();
    return result === 'granted';
  }

  // Expose: ask for permission immediately when the user first starts a job
  // (called from file-tools.js / ai-tools.js on first "Run" click).
  window.lumaNotifyRequestPermission = requestPermission;

  // Fire a notification if the tab is hidden or the window is blurred.
  window.lumaNotify = async function (title, body, toolId) {
    if (!('Notification' in window)) return;
    if (Notification.permission !== 'granted') {
      await requestPermission();
    }
    if (Notification.permission !== 'granted') return;
    // Only notify when tab is not the active foreground tab
    if (document.visibilityState === 'visible' && document.hasFocus()) return;

    const opts = {
      body: body || 'Your file is ready.',
      icon: '/favicon.svg',
      badge: '/favicon.svg',
      tag: 'luma-job-' + (toolId || 'generic'),
      renotify: true,         // Replace same-tag notifications for batch jobs
      requireInteraction: false,
    };
    const n = new Notification(title || 'Luma Tools', opts);
    n.onclick = () => {
      window.focus();
      // If toolId known, navigate to it
      if (toolId && window.LumaShell && window.LUMA_TOOL_BY_ID && window.LUMA_TOOL_BY_ID[toolId]) {
        window.LumaShell.openTool(toolId);
      }
      n.close();
    };
  };

  // Convenience: call when any async job polling loop finishes
  window.lumaNotifyJobDone = function (toolName, toolId) {
    window.lumaNotify(
      toolName + ' complete ✓',
      'Your result is ready — click to open Luma Tools.',
      toolId
    );
  };
})();
