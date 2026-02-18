/**
 * Luma Tools — Stats API + dashboard route handlers
 *
 * GET  /stats              → password-protected dashboard HTML page
 * GET  /api/stats?range=today|week|month|all&kind=tool|download|
 *                           → JSON summary
 * POST /api/stats/digest   → trigger an immediate Discord digest
 *
 * Password is set via the STATS_PASSWORD env var (default: "luma").
 * Always change this in production.
 */

#include "common.h"
#include "stats.h"
#include "routes.h"

// ─── Config ──────────────────────────────────────────────────────────────────

// Returns the stats password from the STATS_PASSWORD environment variable.
// Returns an empty string if the variable is not set — stats will be disabled.
static string stats_password() {
    const char* env = std::getenv("STATS_PASSWORD");
    return env ? string(env) : "";
}

// ─── Simple cookie auth ───────────────────────────────────────────────────────

static bool is_authed(const httplib::Request& req) {
    string pw = stats_password();
    if (pw.empty()) return false;

    auto it = req.headers.find("Cookie");
    if (it == req.headers.end()) return false;

    string cookie = it->second;
    string token  = "stats_auth=" + pw;
    return cookie.find(token) != string::npos;
}

// ─── Route registration ───────────────────────────────────────────────────────

void register_stats_routes(httplib::Server& svr) {

    // ── GET /stats — serve dashboard (or login form) ─────────────────────────
    svr.Get("/stats", [](const httplib::Request& req, httplib::Response& res) {
        if (stats_password().empty()) {
            res.status = 503;
            res.set_content("Stats dashboard is disabled. Set the STATS_PASSWORD environment variable to enable it.", "text/plain");
            return;
        }

        if (!is_authed(req)) {
            // Show login form
            string html = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Luma Tools — Stats</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    min-height: 100vh; display: flex; align-items: center; justify-content: center;
    background: #0d0d12;
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
    color: #e0e0e8;
  }
  .card {
    background: rgba(255,255,255,0.04);
    border: 1px solid rgba(255,255,255,0.09);
    border-radius: 16px;
    padding: 40px 36px;
    width: 340px;
    text-align: center;
  }
  h1 { font-size: 1.4rem; margin-bottom: 8px; }
  p  { font-size: 0.85rem; color: #888; margin-bottom: 24px; }
  input {
    width: 100%; padding: 12px 14px;
    background: rgba(255,255,255,0.06);
    border: 1px solid rgba(255,255,255,0.1);
    border-radius: 8px; color: #e0e0e8;
    font-size: 1rem; margin-bottom: 14px;
  }
  button {
    width: 100%; padding: 12px;
    background: #7c5cff; border: none;
    border-radius: 8px; color: #fff;
    font-size: 1rem; font-weight: 600;
    cursor: pointer;
  }
  button:hover { background: #6a4ee8; }
  .err { color: #ff6b6b; font-size: 0.85rem; margin-top: 10px; }
</style>
</head>
<body>
<div class="card">
  <h1>📊 Stats Dashboard</h1>
  <p>Enter the stats password to continue.</p>
  <form method="POST" action="/stats/login">
    <input type="password" name="password" placeholder="Password" autofocus>
    <button type="submit">Sign in</button>
  </form>
)html";
            if (req.has_param("err")) html += R"(<p class="err">Incorrect password.</p>)";
            html += "</div></body></html>";
            res.set_content(html, "text/html");
            return;
        }

        // Serve full dashboard shell — data loaded via JS from /api/stats
        string html = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Luma Tools — Stats</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    min-height: 100vh;
    background: #0d0d12;
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
    color: #e0e0e8;
    padding: 32px 24px;
  }
  h1 { font-size: 1.6rem; margin-bottom: 6px; }
  .subtitle { color: #888; font-size: 0.9rem; margin-bottom: 28px; }
  .controls { display: flex; gap: 10px; flex-wrap: wrap; margin-bottom: 28px; }
  .controls select, .controls button {
    padding: 9px 16px;
    background: rgba(255,255,255,0.06);
    border: 1px solid rgba(255,255,255,0.1);
    border-radius: 8px; color: #e0e0e8;
    font-size: 0.9rem; cursor: pointer;
  }
  .controls button.active, .controls button:hover {
    background: rgba(124,92,255,0.25);
    border-color: rgba(124,92,255,0.5);
  }
  .cards { display: grid; grid-template-columns: repeat(auto-fill, minmax(160px, 1fr)); gap: 16px; margin-bottom: 32px; }
  .stat-card {
    background: rgba(255,255,255,0.04);
    border: 1px solid rgba(255,255,255,0.08);
    border-radius: 12px; padding: 20px 16px; text-align: center;
  }
  .stat-card .val { font-size: 2rem; font-weight: 700; color: #7c5cff; }
  .stat-card .lbl { font-size: 0.8rem; color: #888; margin-top: 4px; }
  .section { margin-bottom: 32px; }
  .section h2 { font-size: 1rem; font-weight: 600; margin-bottom: 14px; color: #aaa; text-transform: uppercase; letter-spacing: 0.06em; }
  table { width: 100%; border-collapse: collapse; }
  th, td { text-align: left; padding: 10px 14px; font-size: 0.9rem; border-bottom: 1px solid rgba(255,255,255,0.06); }
  th { color: #888; font-weight: 500; }
  .bar-wrap { background: rgba(255,255,255,0.06); border-radius: 4px; height: 8px; flex: 1; overflow: hidden; }
  .bar { background: #7c5cff; height: 100%; border-radius: 4px; transition: width 0.4s; }
  .bar-row { display: flex; align-items: center; gap: 10px; }
  .digest-btn {
    padding: 10px 20px;
    background: rgba(124,92,255,0.2);
    border: 1px solid rgba(124,92,255,0.4);
    border-radius: 8px; color: #e0e0e8;
    font-size: 0.9rem; cursor: pointer;
  }
  .digest-btn:hover { background: rgba(124,92,255,0.35); }
  .toast {
    position: fixed; bottom: 24px; right: 24px;
    background: #1e1e2e; border: 1px solid rgba(255,255,255,0.1);
    border-radius: 10px; padding: 12px 18px;
    font-size: 0.9rem; opacity: 0; transition: opacity 0.3s;
    pointer-events: none;
  }
  .toast.show { opacity: 1; }
</style>
</head>
<body>
<h1>📊 Stats Dashboard</h1>
<p class="subtitle">Luma Tools usage analytics</p>

<div class="controls">
  <button class="active" data-range="today">Today</button>
  <button data-range="week">7 Days</button>
  <button data-range="month">30 Days</button>
  <button data-range="all">All Time</button>
  <select id="kindSelect">
    <option value="">All</option>
    <option value="tool">Tools Only</option>
    <option value="download">Downloads Only</option>
  </select>
  <button class="digest-btn" id="digestBtn">📬 Send Digest Now</button>
</div>

<div class="cards">
  <div class="stat-card"><div class="val" id="cTotal">—</div><div class="lbl">Total Requests</div></div>
  <div class="stat-card"><div class="val" id="cTools">—</div><div class="lbl">Tool Uses</div></div>
  <div class="stat-card"><div class="val" id="cDownloads">—</div><div class="lbl">Downloads</div></div>
  <div class="stat-card"><div class="val" id="cErrors">—</div><div class="lbl">Errors</div></div>
</div>

<div class="section">
  <h2>Top Items</h2>
  <table id="topTable">
    <thead><tr><th>#</th><th>Name</th><th>Count</th><th style="width:40%">Bar</th></tr></thead>
    <tbody id="topBody"></tbody>
  </table>
</div>

<div class="toast" id="toast"></div>

<script>
let currentRange = 'today';
let currentKind  = '';

function showToast(msg) {
  const t = document.getElementById('toast');
  t.textContent = msg;
  t.classList.add('show');
  setTimeout(() => t.classList.remove('show'), 3000);
}

async function load() {
  const res  = await fetch(`/api/stats?range=${currentRange}&kind=${currentKind}`);
  const data = await res.json();

  document.getElementById('cTotal').textContent     = data.total     ?? '—';
  document.getElementById('cErrors').textContent    = data.failures  ?? '—';

  // Fetch sub-totals for tools & downloads separately
  const [rt, rd] = await Promise.all([
    fetch(`/api/stats?range=${currentRange}&kind=tool`).then(r => r.json()),
    fetch(`/api/stats?range=${currentRange}&kind=download`).then(r => r.json()),
  ]);
  document.getElementById('cTools').textContent     = rt.total     ?? '—';
  document.getElementById('cDownloads').textContent = rd.total     ?? '—';

  // Top table
  const tbody = document.getElementById('topBody');
  tbody.innerHTML = '';
  const items = data.by_name ?? [];
  const max   = items[0]?.[1] ?? 1;
  items.slice(0, 20).forEach(([name, count], i) => {
    const pct = Math.round((count / max) * 100);
    const tr  = document.createElement('tr');
    tr.innerHTML = `
      <td>${i + 1}</td>
      <td>${name}</td>
      <td>${count}</td>
      <td><div class="bar-row"><div class="bar-wrap"><div class="bar" style="width:${pct}%"></div></div></div></td>
    `;
    tbody.appendChild(tr);
  });
}

document.querySelectorAll('[data-range]').forEach(btn => {
  btn.addEventListener('click', () => {
    document.querySelectorAll('[data-range]').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    currentRange = btn.dataset.range;
    load();
  });
});

document.getElementById('kindSelect').addEventListener('change', e => {
  currentKind = e.target.value;
  load();
});

document.getElementById('digestBtn').addEventListener('click', async () => {
  const res = await fetch('/api/stats/digest', { method: 'POST' });
  if (res.ok) showToast('Digest sent to Discord!');
  else        showToast('Failed to send digest.');
});

load();
</script>
</body>
</html>)html";
        res.set_content(html, "text/html");
    });

    // ── POST /stats/login — handle password form ──────────────────────────────
    svr.Post("/stats/login", [](const httplib::Request& req, httplib::Response& res) {
        string body = req.body;
        string pw;

        // Parse application/x-www-form-urlencoded "password=..."
        auto pos = body.find("password=");

        if (pos != string::npos) {
            pw = body.substr(pos + 9);
            // URL-decode '+' as space
            for (auto& c : pw) if (c == '+') c = ' ';
        }

        if (pw == stats_password()) {
            res.set_header("Set-Cookie", "stats_auth=" + stats_password() + "; Path=/; HttpOnly");
            res.set_header("Location", "/stats");
            res.status = 302;
        } else {
            res.set_header("Location", "/stats?err=1");
            res.status = 302;
        }
    });

    // ── GET /api/stats — JSON query ───────────────────────────────────────────
    svr.Get("/api/stats", [](const httplib::Request& req, httplib::Response& res) {
        if (!is_authed(req)) {
            res.status = 401;
            res.set_content(json({{"error", "Unauthorized"}}).dump(), "application/json");
            return;
        }

        string range = req.has_param("range") ? req.get_param_value("range") : "today";
        string kind  = req.has_param("kind")  ? req.get_param_value("kind")  : "";

        int64_t from_unix = 0;
        int64_t to_unix   = std::numeric_limits<int64_t>::max();

        if      (range == "today") { from_unix = stat_today_start(); to_unix = from_unix + 86399; }
        else if (range == "week")  { from_unix = stat_days_ago(7);  }
        else if (range == "month") { from_unix = stat_days_ago(30); }
        // "all" uses defaults (0 to max)

        auto s = stat_query(from_unix, to_unix, kind);

        json by_name_arr = json::array();

        for (auto& [name, count] : s.by_name) {
            by_name_arr.push_back({name, count});
        }

        json resp = {
            {"total",     s.total},
            {"successes", s.successes},
            {"failures",  s.failures},
            {"by_name",   by_name_arr}
        };
        res.set_content(resp.dump(), "application/json");
    });

    // ── POST /api/stats/digest — trigger digest manually ─────────────────────
    svr.Post("/api/stats/digest", [](const httplib::Request& req, httplib::Response& res) {
        if (!is_authed(req)) {
            res.status = 401;
            res.set_content(json({{"error", "Unauthorized"}}).dump(), "application/json");
            return;
        }

        thread([]() { stat_send_daily_digest(); }).detach();
        res.set_content(json({{"ok", true}}).dump(), "application/json");
    });
}
