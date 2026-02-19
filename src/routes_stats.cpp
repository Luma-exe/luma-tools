/**
 * Luma Tools - Stats API + dashboard route handlers
 *
 * GET  /stats                  -> password-protected dashboard HTML
 * POST /stats/login            -> cookie auth
 * GET  /stats/logout           -> clear cookie
 * GET  /api/stats              -> JSON summary   (auth required)
 * GET  /api/stats/timeseries   -> day-by-day counts (auth required)
 * GET  /api/stats/visitors     -> unique visitor count (auth required)
 * GET  /api/stats/events       -> event counts (auth required)
 * POST /api/stats/event        -> record client-side event (PUBLIC)
 * POST /api/stats/digest       -> trigger Discord digest (auth required)
 */

#include "common.h"
#include "discord.h"
#include "stats.h"
#include "routes.h"

// =============================================================================
// Auth helpers
// =============================================================================

static string stats_password() {
    const char* env = std::getenv("STATS_PASSWORD");
    return env ? string(env) : "";
}

static bool is_authed(const httplib::Request& req) {
    string pw = stats_password();
    if (pw.empty()) return false;
    auto it = req.headers.find("Cookie");
    if (it == req.headers.end()) return false;
    return it->second.find("stats_auth=" + pw) != string::npos;
}

static string url_decode(const string& s) {
    string out;
    for (size_t i = 0; i < s.size(); ) {
        if (s[i] == '+') { out += ' '; i++; }
        else if (s[i] == '%' && i + 2 < s.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hex(s[i+1]), lo = hex(s[i+2]);
            if (hi >= 0 && lo >= 0) { out += (char)((hi << 4) | lo); i += 3; }
            else { out += s[i++]; }
        } else { out += s[i++]; }
    }
    return out;
}

static pair<int64_t,int64_t> parse_range(const string& range) {
    // Jan 1 2025 00:00:00 UTC
    static constexpr int64_t EPOCH_2025 = 1735689600LL;
    if (range == "today") { int64_t s = stat_today_start(); return {s, s + 86399}; }
    if (range == "week")  return {stat_days_ago(7),  INT64_MAX};
    if (range == "month") return {stat_days_ago(30), INT64_MAX};
    return {EPOCH_2025, INT64_MAX};
}

// =============================================================================
// Login HTML
// =============================================================================

static string login_html(bool show_error) {
    return string(R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Luma Tools - Stats</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{min-height:100vh;display:flex;align-items:center;justify-content:center;
     background:#09090f;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;color:#e2e2ea}
.card{background:rgba(255,255,255,.04);border:1px solid rgba(255,255,255,.09);
      border-radius:18px;padding:44px 40px;width:360px;text-align:center}
.logo{font-size:2.2rem;margin-bottom:10px}
h1{font-size:1.3rem;margin-bottom:6px}
p{font-size:.85rem;color:#555;margin-bottom:28px}
input{width:100%;padding:13px 15px;background:rgba(255,255,255,.06);
      border:1px solid rgba(255,255,255,.1);border-radius:10px;color:#e2e2ea;
      font-size:1rem;margin-bottom:14px;outline:none}
input:focus{border-color:rgba(124,92,255,.6)}
button{width:100%;padding:13px;background:#7c5cff;border:none;border-radius:10px;
       color:#fff;font-size:1rem;font-weight:600;cursor:pointer}
button:hover{background:#6a4ee8}
.err{color:#f87171;font-size:.85rem;margin-top:12px}
</style>
</head>
<body>
<div class="card">
  <div class="logo">&#x1F4CA;</div>
  <h1>Stats Dashboard</h1>
  <p>Enter the stats password to continue.</p>
  <form method="POST" action="/stats/login">
    <input type="password" name="password" placeholder="Password" autofocus>
    <button type="submit">Sign in</button>
  </form>
)HTML") + (show_error ? R"HTML(<p class="err">Incorrect password.</p>)HTML" : "") + R"HTML(
</div>
</body>
</html>)HTML";
}

// =============================================================================
// Dashboard HTML
// =============================================================================

static const char* DASHBOARD_HTML = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Luma Tools - Analytics</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.3/dist/chart.umd.min.js"></script>
<style>
*{box-sizing:border-box;margin:0;padding:0}
:root{
  --bg:#09090f;--surface:rgba(255,255,255,.04);--border:rgba(255,255,255,.08);
  --purple:#7c5cff;--purple-dim:rgba(124,92,255,.18);--text:#e2e2ea;--muted:#666;
}
body{background:var(--bg);color:var(--text);
     font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;
     min-height:100vh;padding:0 0 60px}
.header{padding:28px 32px 0;max-width:1300px;margin:auto;
        display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:12px}
.header h1{font-size:1.4rem;font-weight:700;display:flex;align-items:center;gap:10px}
.logout{font-size:.8rem;color:var(--muted);text-decoration:none;padding:6px 14px;
        border:1px solid var(--border);border-radius:8px}
.logout:hover{color:var(--text);border-color:rgba(255,255,255,.2)}
.rangebar{padding:16px 32px 0;max-width:1300px;margin:auto;
          display:flex;gap:8px;flex-wrap:wrap;align-items:center}
.range-btn{padding:7px 16px;background:var(--surface);border:1px solid var(--border);
           border-radius:8px;color:var(--muted);font-size:.875rem;cursor:pointer}
.range-btn.active,.range-btn:hover{background:var(--purple-dim);
  border-color:rgba(124,92,255,.5);color:var(--text)}
.digest-btn{margin-left:auto;padding:7px 16px;background:rgba(255,255,255,.05);
            border:1px solid var(--border);border-radius:8px;color:var(--muted);
            font-size:.875rem;cursor:pointer}
.digest-btn:hover{background:var(--purple-dim);border-color:rgba(124,92,255,.5);color:var(--text)}
.main{padding:24px 32px 0;max-width:1300px;margin:auto}
.cards{display:grid;grid-template-columns:repeat(auto-fill,minmax(170px,1fr));gap:14px;margin-bottom:28px}
.card{background:var(--surface);border:1px solid var(--border);border-radius:14px;
      padding:22px 18px;text-align:center}
.card .val{font-size:2rem;font-weight:700;color:var(--purple)}
.card.green .val{color:#34d399}
.card.red   .val{color:#f87171}
.card.blue  .val{color:#60a5fa}
.card .lbl{font-size:.78rem;color:var(--muted);margin-top:5px;letter-spacing:.02em}
.charts-grid{display:grid;grid-template-columns:1fr 1fr;gap:20px;margin-bottom:28px}
@media(max-width:820px){.charts-grid{grid-template-columns:1fr}}
.chart-box{background:var(--surface);border:1px solid var(--border);border-radius:14px;padding:22px}
.chart-box h2{font-size:.8rem;font-weight:600;color:var(--muted);text-transform:uppercase;
              letter-spacing:.07em;margin-bottom:16px}
.chart-full{grid-column:1/-1}
.section{background:var(--surface);border:1px solid var(--border);border-radius:14px;
         padding:22px;margin-bottom:20px}
.section h2{font-size:.8rem;font-weight:600;color:var(--muted);text-transform:uppercase;
            letter-spacing:.07em;margin-bottom:16px}
table{width:100%;border-collapse:collapse}
th,td{text-align:left;padding:9px 12px;font-size:.875rem;
      border-bottom:1px solid rgba(255,255,255,.05)}
th{color:var(--muted);font-weight:500}
tr:last-child td{border-bottom:none}
.bar-bg{background:rgba(255,255,255,.07);border-radius:4px;height:7px;flex:1;overflow:hidden}
.bar-fill{background:var(--purple);height:100%;border-radius:4px;transition:width .4s}
.bar-row{display:flex;align-items:center;gap:10px}
.toast{position:fixed;bottom:24px;right:24px;background:#1e1e2e;
       border:1px solid rgba(255,255,255,.1);border-radius:10px;
       padding:12px 20px;font-size:.875rem;opacity:0;
       transition:opacity .3s;pointer-events:none;z-index:9999}
.toast.show{opacity:1}
.loading{color:var(--muted);font-size:.875rem;padding:16px 0;text-align:center}
/* Admin panel */
.admin-tab-btn{margin-left:8px;padding:7px 16px;background:rgba(124,92,255,.12);
  border:1px solid rgba(124,92,255,.35);border-radius:8px;color:#a88aff;
  font-size:.875rem;cursor:pointer}
.admin-tab-btn:hover,.admin-tab-btn.active{background:var(--purple-dim);
  border-color:rgba(124,92,255,.6);color:var(--text)}
#adminPanel{padding:24px 32px 0;max-width:1300px;margin:auto}
.admin-tool-row td input[type=number]{width:72px;background:rgba(255,255,255,.06);
  border:1px solid var(--border);border-radius:6px;color:var(--text);
  padding:5px 8px;font-size:.82rem}
.admin-tool-row td input[type=text]{width:160px;background:rgba(255,255,255,.06);
  border:1px solid var(--border);border-radius:6px;color:var(--text);
  padding:5px 8px;font-size:.82rem}
.toggle-switch{position:relative;display:inline-block;width:38px;height:20px}
.toggle-switch input{opacity:0;width:0;height:0}
.toggle-slider{position:absolute;cursor:pointer;inset:0;background:#333;
  border-radius:20px;transition:.25s}
.toggle-slider::before{content:"";position:absolute;left:3px;top:3px;
  width:14px;height:14px;background:#666;border-radius:50%;transition:.25s}
.toggle-switch input:checked + .toggle-slider{background:rgba(124,92,255,.5)}
.toggle-switch input:checked + .toggle-slider::before{transform:translateX(18px);background:var(--purple)}
.save-tool-btn{padding:5px 12px;background:rgba(124,92,255,.15);
  border:1px solid rgba(124,92,255,.35);border-radius:6px;
  color:#a88aff;font-size:.8rem;cursor:pointer}
.save-tool-btn:hover{background:var(--purple-dim)}
.badge-server{font-size:.65rem;padding:1px 5px;border-radius:4px;
  background:rgba(124,92,255,.2);color:#a88aff;vertical-align:middle;margin-left:4px}
.badge-browser{font-size:.65rem;padding:1px 5px;border-radius:4px;
  background:rgba(96,165,250,.15);color:#60a5fa;vertical-align:middle;margin-left:4px}
.card.purple .val{color:#a855f7}
.ai-tag{display:inline-block;padding:2px 7px;border-radius:4px;font-size:.72rem;font-weight:600}
.ai-tag.p{background:rgba(124,92,255,.2);color:#a88aff}
.ai-tag.f{background:rgba(251,191,36,.15);color:#fbbf24}
.ai-tag.l{background:rgba(52,211,153,.15);color:#34d399}
</style>
</head>
<body>
<div class="header">
  <h1>&#x1F4CA; Luma Tools Analytics</h1>
  <a class="logout" href="/stats/logout">Sign out</a>
</div>

<div class="rangebar">
  <button class="range-btn active" data-range="today">Today</button>
  <button class="range-btn" data-range="week">Last 7 Days</button>
  <button class="range-btn" data-range="month">Last 30 Days</button>
  <button class="range-btn" data-range="all">All Time</button>
  <button class="digest-btn" id="digestBtn">&#x1F4EC; Send Digest</button>
  <button class="admin-tab-btn" id="adminTabBtn">&#x1F527; Admin</button>
</div>

<div class="main">
  <div class="cards">
    <div class="card">       <div class="val" id="cTotal">-</div>    <div class="lbl">Total Requests</div></div>
    <div class="card green"> <div class="val" id="cTools">-</div>    <div class="lbl">Tool Uses</div></div>
    <div class="card blue">  <div class="val" id="cDownloads">-</div><div class="lbl">Downloads</div></div>
    <div class="card">       <div class="val" id="cVisitors">-</div> <div class="lbl">Unique Visitors</div></div>
    <div class="card red">   <div class="val" id="cErrors">-</div>   <div class="lbl">Errors</div></div>
    <div class="card purple"><div class="val" id="cAiCalls">-</div>  <div class="lbl">AI Calls</div></div>
    <div class="card">       <div class="val" id="cAiTokens" style="font-size:1.4rem">-</div><div class="lbl">AI Tokens</div></div>
  </div>

  <div class="charts-grid">
    <div class="chart-box chart-full">
      <h2>Requests Over Time</h2>
      <canvas id="lineChart" height="90"></canvas>
    </div>
    <div class="chart-box">
      <h2>Tool Distribution</h2>
      <canvas id="donutChart"></canvas>
    </div>
    <div class="chart-box">
      <h2>Top Download Platforms</h2>
      <canvas id="barChart"></canvas>
    </div>
    <div class="chart-box">
      <h2>AI Model Distribution</h2>
      <canvas id="aiModelChart"></canvas>
    </div>
    <div class="chart-box">
      <h2>AI Tokens per Model</h2>
      <canvas id="aiTokenChart"></canvas>
    </div>
  </div>

  <div class="section">
    <h2>Top Tools</h2>
    <table>
      <thead><tr><th>#</th><th>Tool</th><th>Count</th><th style="width:38%">Usage</th></tr></thead>
      <tbody id="toolsBody"><tr><td colspan="4" class="loading">Loading...</td></tr></tbody>
    </table>
  </div>

  <div class="section" id="eventsSection" style="display:none">
    <h2>Tracked Events</h2>
    <table>
      <thead><tr><th>Event</th><th>Count</th></tr></thead>
      <tbody id="eventsBody"></tbody>
    </table>
  </div>

  <div class="section" id="aiSection" style="display:none">
    <h2>&#x1F916; AI Usage by Tool</h2>
    <table>
      <thead><tr><th>Tool</th><th>Last Model</th><th>Calls</th><th>Tokens Used</th></tr></thead>
      <tbody id="aiByToolBody"><tr><td colspan="4" class="loading">No AI data for this range.</td></tr></tbody>
    </table>
  </div>
</div>

<div class="toast" id="toast"></div>

<div id="adminPanel" style="display:none">
  <div class="section">
    <h2>&#x1F527; Tool Configuration</h2>
    <p style="color:var(--muted);font-size:.82rem;margin-bottom:16px">Changes are saved per-tool immediately. <b style="color:var(--text)">Rate/min</b> = max API calls per IP per minute (0 = unlimited). <b style="color:var(--text)">Max file</b> = MB (0 = no limit). <b style="color:var(--text)">Max chars</b> = text input limit (0 = no limit).</p>
    <div style="overflow-x:auto">
    <table id="adminToolsTable">
      <thead>
        <tr>
          <th>Tool</th><th>Type</th><th>Enabled</th>
          <th>Rate/min</th><th>Max file (MB)</th><th>Max chars</th>
          <th>Note</th><th></th>
        </tr>
      </thead>
      <tbody id="adminToolsBody">
        <tr><td colspan="8" class="loading">Loading...</td></tr>
      </tbody>
    </table>
    </div>
  </div>
</div>

)HTML"
R"HTML(<script>
Chart.defaults.color = '#666';
Chart.defaults.borderColor = 'rgba(255,255,255,0.07)';
const PURPLE = '#7c5cff';
const PURPLE_ALPHA = 'rgba(124,92,255,0.18)';
const PALETTE = ['#7c5cff','#34d399','#60a5fa','#f87171','#fbbf24','#a78bfa','#6ee7b7','#93c5fd'];

let lineChart, donutChart, barChart, aiModelChart, aiTokenChart;

function initCharts() {
  lineChart = new Chart(document.getElementById('lineChart').getContext('2d'), {
    type: 'line',
    data: { labels: [], datasets: [{ label: 'Requests', data: [],
      borderColor: PURPLE, backgroundColor: PURPLE_ALPHA,
      fill: true, tension: 0.4, pointRadius: 3, pointHoverRadius: 6 }]},
    options: { responsive: true, plugins: { legend: { display: false } },
      scales: {
        x: { grid: { color: 'rgba(255,255,255,0.05)' } },
        y: { beginAtZero: true, grid: { color: 'rgba(255,255,255,0.05)' }, ticks: { precision: 0 } }
      }}
  });

  donutChart = new Chart(document.getElementById('donutChart').getContext('2d'), {
    type: 'doughnut',
    data: { labels: [], datasets: [{ data: [], backgroundColor: PALETTE, borderWidth: 2, borderColor: '#09090f' }]},
    options: { responsive: true, cutout: '65%',
      plugins: { legend: { position: 'right', labels: { boxWidth: 12, padding: 14 } } }}
  });

  barChart = new Chart(document.getElementById('barChart').getContext('2d'), {
    type: 'bar',
    data: { labels: [], datasets: [{ data: [], backgroundColor: PALETTE, borderRadius: 6 }]},
    options: { responsive: true, indexAxis: 'y',
      plugins: { legend: { display: false } },
      scales: {
        x: { beginAtZero: true, grid: { color: 'rgba(255,255,255,0.05)' }, ticks: { precision: 0 } },
        y: { grid: { display: false } }
      }}
  });

  const AI_PALETTE = ['#a855f7','#7c5cff','#34d399','#60a5fa','#f87171','#fbbf24'];
  aiModelChart = new Chart(document.getElementById('aiModelChart').getContext('2d'), {
    type: 'doughnut',
    data: { labels: [], datasets: [{ data: [], backgroundColor: AI_PALETTE, borderWidth: 2, borderColor: '#09090f' }]},
    options: { responsive: true, cutout: '65%',
      plugins: { legend: { position: 'right', labels: { boxWidth: 12, padding: 14 } } }}
  });

  aiTokenChart = new Chart(document.getElementById('aiTokenChart').getContext('2d'), {
    type: 'bar',
    data: { labels: [], datasets: [{ label: 'Tokens', data: [], backgroundColor: AI_PALETTE, borderRadius: 6 }]},
    options: { responsive: true, indexAxis: 'y',
      plugins: { legend: { display: false } },
      scales: {
        x: { beginAtZero: true, grid: { color: 'rgba(255,255,255,0.05)' }, ticks: { precision: 0 } },
        y: { grid: { display: false } }
      }}
  });
}

let currentRange = 'today';

function showToast(msg) {
  const t = document.getElementById('toast');
  t.textContent = msg;
  t.classList.add('show');
  setTimeout(() => t.classList.remove('show'), 3000);
}

async function load() {
  try {
    const AI_MODEL_NAMES = {
      'llama-3.3-70b-versatile': 'Llama 3.3 70B',
      'deepseek-r1-distill-llama-70b': 'DeepSeek R1 70B',
      'llama-3.1-8b-instant': 'Llama 3.1 8B',
      'ollama:llama3.1:8b': 'Local (Ollama)'
    };

    const MODEL_TAG_CLASS = {
      'llama-3.3-70b-versatile': 'p',
      'deepseek-r1-distill-llama-70b': 'f',
      'llama-3.1-8b-instant': 'f',
      'ollama:llama3.1:8b': 'l'
    };

    const [allData, toolsData, dlData, tsData, visitData, evData, aiData] = await Promise.all([
      fetch(`/api/stats?range=${currentRange}`).then(r => r.json()),
      fetch(`/api/stats?range=${currentRange}&kind=tool`).then(r => r.json()),
      fetch(`/api/stats?range=${currentRange}&kind=download`).then(r => r.json()),
      fetch(`/api/stats/timeseries?range=${currentRange}`).then(r => r.json()),
      fetch(`/api/stats/visitors?range=${currentRange}`).then(r => r.json()),
      fetch(`/api/stats/events?range=${currentRange}`).then(r => r.json()),
      fetch(`/api/stats/ai?range=${currentRange}`).then(r => r.json()),
    ]);

    document.getElementById('cTotal').textContent     = allData.total     ?? '-';
    document.getElementById('cAiCalls').textContent   = aiData.total_calls  ?? '-';
    document.getElementById('cAiTokens').textContent  = (aiData.total_tokens ?? 0).toLocaleString();

    // AI Model Distribution charts
    const aiModels = aiData.by_model || [];
    aiModelChart.data.labels   = aiModels.map(m => AI_MODEL_NAMES[m.model] || m.model);
    aiModelChart.data.datasets[0].data = aiModels.map(m => m.calls);
    aiModelChart.update();

    aiTokenChart.data.labels   = aiModels.map(m => AI_MODEL_NAMES[m.model] || m.model);
    aiTokenChart.data.datasets[0].data = aiModels.map(m => m.tokens);
    aiTokenChart.update();

    // AI by Tool table
    const aiSection  = document.getElementById('aiSection');
    const aiToolBody = document.getElementById('aiByToolBody');
    const aiByTool   = aiData.by_tool || [];
    if (aiByTool.length === 0) {
      aiSection.style.display = '';
      aiToolBody.innerHTML = '<tr><td colspan="4" class="loading">No AI data for this range.</td></tr>';
    } else {
      aiSection.style.display = '';
      aiToolBody.innerHTML = '';
      aiByTool.forEach(t => {
        const cls   = MODEL_TAG_CLASS[t.last_model] || 'p';
        const label = AI_MODEL_NAMES[t.last_model] || t.last_model;
        const tr = document.createElement('tr');
        tr.innerHTML =
          '<td><code style="font-size:.82rem">' + t.tool + '</code></td>' +
          '<td><span class="ai-tag ' + cls + '">' + label + '</span></td>' +
          '<td>' + t.calls + '</td>' +
          '<td>' + (t.tokens || 0).toLocaleString() + '</td>';
        aiToolBody.appendChild(tr);
      });
    }
    document.getElementById('cTools').textContent     = toolsData.total   ?? '-';
    document.getElementById('cDownloads').textContent = dlData.total      ?? '-';
    document.getElementById('cVisitors').textContent  = visitData.unique  ?? '-';
    document.getElementById('cErrors').textContent    = allData.failures  ?? '-';

    if (tsData.days && tsData.days.length > 0) {
      lineChart.data.labels = tsData.days.map(d => d.date);
      lineChart.data.datasets[0].data = tsData.days.map(d => d.count);
      lineChart.update();
    }

    const topTools = (toolsData.by_name || []).slice(0, 8);
    donutChart.data.labels = topTools.map(([n]) => n);
    donutChart.data.datasets[0].data = topTools.map(([,c]) => c);
    donutChart.data.datasets[0].backgroundColor = PALETTE.slice(0, topTools.length);
    donutChart.update();

    const topDl = (dlData.by_name || []).slice(0, 8);
    barChart.data.labels = topDl.map(([n]) => n);
    barChart.data.datasets[0].data = topDl.map(([,c]) => c);
    barChart.data.datasets[0].backgroundColor = PALETTE.slice(0, topDl.length);
    barChart.update();

    const tbody = document.getElementById('toolsBody');
    tbody.innerHTML = '';
    const items = toolsData.by_name || [];
    const max   = items[0]?.[1] ?? 1;
    if (items.length === 0) {
      tbody.innerHTML = '<tr><td colspan="4" class="loading">No data for this range.</td></tr>';
    } else {
      items.slice(0, 25).forEach(([name, count], i) => {
        const pct = Math.round((count / max) * 100);
        const tr = document.createElement('tr');
        tr.innerHTML = '<td style="color:var(--muted)">' + (i+1) + '</td>' +
          '<td><code style="font-size:.82rem">' + name + '</code></td>' +
          '<td>' + count + '</td>' +
          '<td><div class="bar-row"><div class="bar-bg"><div class="bar-fill" style="width:' + pct + '%"></div></div></div></td>';
        tbody.appendChild(tr);
      });
    }

    const evSection = document.getElementById('eventsSection');
    const evBody    = document.getElementById('eventsBody');
    const evItems   = evData.events || [];
    if (evItems.length === 0) {
      evSection.style.display = 'none';
    } else {
      evSection.style.display = '';
      evBody.innerHTML = '';
      evItems.forEach(([name, count]) => {
        const tr = document.createElement('tr');
        tr.innerHTML = '<td><code style="font-size:.82rem">' + name + '</code></td><td>' + count + '</td>';
        evBody.appendChild(tr);
      });
    }
  } catch(e) {
    showToast('Error: ' + e.message);
  }
}

document.querySelectorAll('.range-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    document.querySelectorAll('.range-btn').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    currentRange = btn.dataset.range;
    load();
  });
});

document.getElementById('digestBtn').addEventListener('click', async () => {
  const res = await fetch('/api/stats/digest', { method: 'POST' });
  showToast(res.ok ? 'Digest sent to Discord!' : 'Failed to send digest.');
});

initCharts();
load();
)HTML"
// Split string literal to stay under MSVC's per-literal size limit
R"HTML(
// ── Admin Panel ──────────────────────────────────────────────────────────────
const ALL_TOOLS = [
  {id:'ai-study-notes',     loc:'server'},
  {id:'ai-flashcards',      loc:'server'},
  {id:'ai-quiz',            loc:'server'},
  {id:'ai-paraphrase',      loc:'server'},
  {id:'citation-gen',       loc:'server'},
  {id:'mind-map',           loc:'server'},
  {id:'youtube-summary',    loc:'server'},
  {id:'downloader',         loc:'server'},
  {id:'metadata-strip',     loc:'server'},
  {id:'image-bg-remove',    loc:'server'},
  {id:'image-watermark',    loc:'server'},
  {id:'video-compress',     loc:'server'},
  {id:'video-trim',         loc:'server'},
  {id:'video-convert',      loc:'server'},
  {id:'video-extract-audio',loc:'server'},
  {id:'video-to-gif',       loc:'server'},
  {id:'gif-to-video',       loc:'server'},
  {id:'video-remove-audio', loc:'server'},
  {id:'video-speed',        loc:'server'},
  {id:'video-frame',        loc:'server'},
  {id:'video-stabilize',    loc:'server'},
  {id:'subtitle-extract',   loc:'server'},
  {id:'audio-normalize',    loc:'server'},
  {id:'audio-trim',         loc:'server'},
  {id:'pdf-compress',       loc:'server'},
  {id:'pdf-merge',          loc:'server'},
  {id:'pdf-split',          loc:'server'},
  {id:'pdf-to-images',      loc:'server'},
  {id:'images-to-pdf',      loc:'server'},
  {id:'markdown-to-pdf',    loc:'server'},
  {id:'hash-generate',      loc:'server'},
  {id:'csv-json',           loc:'server'},
  {id:'redact',             loc:'browser'},
  {id:'image-compress',     loc:'browser'},
  {id:'image-resize',       loc:'browser'},
  {id:'image-convert',      loc:'browser'},
  {id:'favicon-generate',   loc:'browser'},
  {id:'image-crop',         loc:'browser'},
  {id:'screenshot-annotate',loc:'browser'},
  {id:'color-palette',      loc:'browser'},
  {id:'audio-convert',      loc:'browser'},
  {id:'resume-builder',     loc:'browser'},
  {id:'invoice-gen',        loc:'browser'},
  {id:'qr-generate',        loc:'browser'},
  {id:'base64',             loc:'browser'},
  {id:'json-format',        loc:'browser'},
  {id:'color-convert',      loc:'browser'},
  {id:'markdown-preview',   loc:'browser'},
  {id:'diff-checker',       loc:'browser'},
  {id:'word-counter',       loc:'browser'},
  {id:'unix-date',          loc:'browser'},
  {id:'regex-tester',       loc:'browser'},
  {id:'code-beautify',      loc:'browser'},
  {id:'uuid-gen',           loc:'browser'},
  {id:'url-encode',         loc:'browser'},
];

let adminLoaded = false;

async function loadAdminTools() {
  try {
    const data = await fetch('/api/admin/tools').then(r => r.json());
    // index by tool_id
    const cfgMap = {};
    (data.tools || []).forEach(t => cfgMap[t.tool_id] = t);

    const tbody = document.getElementById('adminToolsBody');
    tbody.innerHTML = '';
    ALL_TOOLS.forEach(t => {
      const cfg = cfgMap[t.id] || { enabled: true, rate_limit_min: 0, max_file_mb: 0, max_text_chars: 0, note: '' };
      const isServer = t.loc === 'server';
      const badge = isServer
        ? '<span class="badge-server">server</span>'
        : '<span class="badge-browser">browser</span>';
      const tr = document.createElement('tr');
      tr.className = 'admin-tool-row';
      tr.dataset.tool = t.id;
      tr.innerHTML =
        '<td><code style="font-size:.82rem">' + t.id + '</code>' + badge + '</td>' +
        '<td style="color:var(--muted);font-size:.78rem">' + t.loc + '</td>' +
        '<td><label class="toggle-switch"><input type="checkbox" class="adm-enabled"' +
          (cfg.enabled ? ' checked' : '') + '><span class="toggle-slider"></span></label></td>' +
        '<td><input type="number" class="adm-rl" min="0" max="9999" value="' + (cfg.rate_limit_min||0) + '"></td>' +
        '<td><input type="number" class="adm-mb" min="0" max="9999" value="' + (cfg.max_file_mb||0) + '"></td>' +
        '<td><input type="number" class="adm-chars" min="0" max="999999" value="' + (cfg.max_text_chars||0) + '"></td>' +
        '<td><input type="text" class="adm-note" placeholder="optional note" value="' + (cfg.note||'').replace(/"/g,'&quot;') + '"></td>' +
        '<td><button class="save-tool-btn" onclick="saveToolCfg(this)">Save</button></td>';
      tbody.appendChild(tr);
    });
  } catch(e) {
    showToast('Admin load error: ' + e.message);
  }
}

async function saveToolCfg(btn) {
  const tr = btn.closest('tr');
  const toolId = tr.dataset.tool;
  const body = {
    enabled:        tr.querySelector('.adm-enabled').checked,
    rate_limit_min: parseInt(tr.querySelector('.adm-rl').value)    || 0,
    max_file_mb:    parseInt(tr.querySelector('.adm-mb').value)    || 0,
    max_text_chars: parseInt(tr.querySelector('.adm-chars').value) || 0,
    note:           tr.querySelector('.adm-note').value,
  };
  try {
    const res = await fetch('/api/admin/tools/' + toolId, {
      method: 'POST',
      headers: {'Content-Type':'application/json'},
      body: JSON.stringify(body)
    });
    if (res.ok) {
      btn.textContent = 'Saved!';
      setTimeout(() => btn.textContent = 'Save', 1800);
    } else {
      showToast('Save failed for ' + toolId);
    }
  } catch(e) {
    showToast('Save error: ' + e.message);
  }
}

const mainDiv   = document.querySelector('.main');
const adminDiv  = document.getElementById('adminPanel');
const adminBtn  = document.getElementById('adminTabBtn');
const rangeBtns = document.querySelectorAll('.range-btn');

adminBtn.addEventListener('click', () => {
  const isAdmin = adminDiv.style.display !== 'none';
  if (isAdmin) {
    adminDiv.style.display = 'none';
    mainDiv.style.display  = '';
    adminBtn.classList.remove('active');
    rangeBtns.forEach(b => b.style.display = '');
    document.getElementById('digestBtn').style.display = '';
  } else {
    adminDiv.style.display = '';
    mainDiv.style.display  = 'none';
    adminBtn.classList.add('active');
    rangeBtns.forEach(b => b.style.display = 'none');
    document.getElementById('digestBtn').style.display = 'none';
    if (!adminLoaded) { adminLoaded = true; loadAdminTools(); }
  }
});
</script>
</body>
</html>)HTML";

// =============================================================================
// Route registration
// =============================================================================

void register_stats_routes(httplib::Server& svr) {

    // GET /stats
    svr.Get("/stats", [](const httplib::Request& req, httplib::Response& res) {
        if (stats_password().empty()) {
            res.status = 503;
            res.set_content("Stats dashboard disabled. Set STATS_PASSWORD.", "text/plain");
            return;
        }
        if (!is_authed(req)) {
            res.set_content(login_html(req.has_param("err")), "text/html");
            return;
        }
        res.set_content(DASHBOARD_HTML, "text/html");
    });

    // POST /stats/login
    svr.Post("/stats/login", [](const httplib::Request& req, httplib::Response& res) {
        string body = req.body;
        string pw;
        auto pos = body.find("password=");
        if (pos != string::npos) pw = url_decode(body.substr(pos + 9));
        if (pw == stats_password()) {
            res.set_header("Set-Cookie", "stats_auth=" + stats_password() + "; Path=/; HttpOnly");
            res.set_header("Location", "/stats");
            res.status = 302;
        } else {
            res.set_header("Location", "/stats?err=1");
            res.status = 302;
        }
    });

    // GET /stats/logout
    svr.Get("/stats/logout", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Set-Cookie", "stats_auth=; Path=/; HttpOnly; Max-Age=0");
        res.set_header("Location", "/stats");
        res.status = 302;
    });

    // GET /api/stats
    svr.Get("/api/stats", [](const httplib::Request& req, httplib::Response& res) {
        if (!is_authed(req)) { res.status = 401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        string range = req.has_param("range") ? req.get_param_value("range") : "today";
        string kind  = req.has_param("kind")  ? req.get_param_value("kind")  : "";
        auto [from_unix, to_unix] = parse_range(range);
        auto s = stat_query(from_unix, to_unix, kind);
        json by_name_arr = json::array();
        for (auto& [n, c] : s.by_name) by_name_arr.push_back({n, c});
        json resp = {{"total",s.total},{"successes",s.successes},{"failures",s.failures},{"by_name",by_name_arr}};
        res.set_content(resp.dump(), "application/json");
    });

    // GET /api/stats/timeseries
    svr.Get("/api/stats/timeseries", [](const httplib::Request& req, httplib::Response& res) {
        if (!is_authed(req)) { res.status = 401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        string range = req.has_param("range") ? req.get_param_value("range") : "week";
        string kind  = req.has_param("kind")  ? req.get_param_value("kind")  : "";
        auto [from_unix, to_unix] = parse_range(range);
        auto buckets = stat_timeseries(from_unix, to_unix, kind);
        json days = json::array();
        for (auto& b : buckets) days.push_back({{"date",b.date},{"count",b.count}});
        res.set_content(json({{"days",days}}).dump(), "application/json");
    });

    // GET /api/stats/visitors
    svr.Get("/api/stats/visitors", [](const httplib::Request& req, httplib::Response& res) {
        if (!is_authed(req)) { res.status = 401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        string range = req.has_param("range") ? req.get_param_value("range") : "today";
        auto [from_unix, to_unix] = parse_range(range);
        res.set_content(json({{"unique", stat_unique_visitors(from_unix, to_unix)}}).dump(), "application/json");
    });

    // GET /api/stats/events
    svr.Get("/api/stats/events", [](const httplib::Request& req, httplib::Response& res) {
        if (!is_authed(req)) { res.status = 401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        string range = req.has_param("range") ? req.get_param_value("range") : "today";
        auto [from_unix, to_unix] = parse_range(range);
        auto ev = stat_events(from_unix, to_unix);
        json arr = json::array();
        for (auto& [n, c] : ev) arr.push_back({n, c});
        res.set_content(json({{"events",arr}}).dump(), "application/json");
    });

    // POST /api/stats/event  (PUBLIC - called from user browsers)
    svr.Post("/api/stats/event", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        try {
            auto body = json::parse(req.body);
            string name = body.value("name", "");
            if (!name.empty() && name.size() <= 64) stat_record_event(name);
        } catch (...) {}
        res.set_content(R"({"ok":true})", "application/json");
    });

    // OPTIONS /api/stats/event  (CORS preflight)
    svr.Options("/api/stats/event", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });

    // POST /api/wasm/error  (PUBLIC — browser reports WASM/Canvas failures for Discord logging)
    svr.Post("/api/wasm/error", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        try {
            auto body  = json::parse(req.body);
            string tool  = body.value("tool",  "unknown");
            string error = body.value("error", "unknown");
            bool   coi   = body.value("crossOriginIsolated", false);
            bool   sab   = body.value("sharedArrayBuffer",   false);
            string ua    = body.value("ua", "");
            if (ua.size() > 200) ua = ua.substr(0, 200) + "…";

            string desc = "🛠️ **Tool** › `" + tool + "`\n"
                          "💥 **Error** › " + error.substr(0, 300) + "\n"
                          "🔒 **crossOriginIsolated** › " + (coi ? "✅ true" : "❌ false") + "\n"
                          "🧵 **SharedArrayBuffer** › "   + (sab ? "✅ true" : "❌ false") + "\n"
                          "🌐 **UA** › `" + ua + "`";
            discord_log("⚠️ Browser WASM / Canvas Error", desc, 0xFFA500); // orange
        } catch (...) {}
        res.set_content(R"({"ok":true})", "application/json");
    });
    svr.Options("/api/wasm/error", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });

    // POST /api/browser-tool  (PUBLIC — browser reports successful Canvas/WASM processing)
    svr.Post("/api/browser-tool", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        try {
            auto body = json::parse(req.body);
            string tool     = body.value("tool",     "unknown");
            string filename = body.value("filename", "unknown");
            if (tool.size()     > 64)  tool     = tool.substr(0, 64);
            if (filename.size() > 255) filename = filename.substr(0, 255);
            discord_log_tool(tool, filename, req.remote_addr, "browser");
        } catch (...) {}
        res.set_content(R"({"ok":true})", "application/json");
    });
    svr.Options("/api/browser-tool", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });

    // POST /api/stats/digest
    svr.Post("/api/stats/digest", [](const httplib::Request& req, httplib::Response& res) {
        if (!is_authed(req)) { res.status = 401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        thread([]() { stat_send_daily_digest(); }).detach();
        res.set_content(R"({"ok":true})", "application/json");
    });

    // GET /api/stats/ai
    svr.Get("/api/stats/ai", [](const httplib::Request& req, httplib::Response& res) {
        if (!is_authed(req)) { res.status = 401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        string range = req.has_param("range") ? req.get_param_value("range") : "today";
        auto [from_unix, to_unix] = parse_range(range);
        auto ai = stat_query_ai(from_unix, to_unix);
        json by_model = json::array();
        for (auto& b : ai.by_model)
            by_model.push_back({{"model",b.model},{"calls",b.calls},{"tokens",b.tokens}});
        json by_tool = json::array();
        for (auto& b : ai.by_tool)
            by_tool.push_back({{"tool",b.tool},{"last_model",b.last_model},{"calls",b.calls},{"tokens",b.tokens}});
        json resp = {{"total_calls",ai.total_calls},{"total_tokens",ai.total_tokens},{"by_model",by_model},{"by_tool",by_tool}};
        res.set_content(resp.dump(), "application/json");
    });

    // GET /api/admin/tools  — list all tool configs
    svr.Get("/api/admin/tools", [](const httplib::Request& req, httplib::Response& res) {
        if (!is_authed(req)) { res.status = 401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        auto configs = get_all_tool_configs();
        json arr = json::array();
        for (auto& c : configs) {
            arr.push_back({{
                {"tool_id",        c.tool_id},
                {"enabled",        c.enabled},
                {"rate_limit_min", c.rate_limit_min},
                {"max_file_mb",    c.max_file_mb},
                {"max_text_chars", c.max_text_chars},
                {"note",           c.note}
            }});
        }
        res.set_content(json({{"tools", arr}}).dump(), "application/json");
    });

    // POST /api/admin/tools/:id  — update a tool config
    svr.Post("/api/admin/tools/([^/]+)", [](const httplib::Request& req, httplib::Response& res) {
        if (!is_authed(req)) { res.status = 401; res.set_content(R"({"error":"Unauthorized"})", "application/json"); return; }
        string tool_id = req.matches[1];
        json body;
        try { body = json::parse(req.body); } catch (...) {
            res.status = 400; res.set_content(R"({"error":"Invalid JSON"})", "application/json"); return;
        }
        ToolConfig old_cfg = get_tool_config(tool_id); // snapshot before changes
        ToolConfig cfg = old_cfg;
        cfg.tool_id        = tool_id;
        if (body.contains("enabled"))        cfg.enabled        = body["enabled"].get<bool>();
        if (body.contains("rate_limit_min")) cfg.rate_limit_min = body["rate_limit_min"].get<int>();
        if (body.contains("max_file_mb"))    cfg.max_file_mb    = body["max_file_mb"].get<int>();
        if (body.contains("max_text_chars")) cfg.max_text_chars = body["max_text_chars"].get<int>();
        if (body.contains("note"))           cfg.note           = body["note"].get<string>();
        set_tool_config(cfg);

        // Build a diff description for Discord.
        // Always show enabled state unconditionally so disabling is never silent.
        string changes;
        changes += (cfg.enabled ? "✅ Enabled" : "❌ Disabled");
        if (cfg.enabled != old_cfg.enabled)
            changes += string(" *(changed from ") + (old_cfg.enabled ? "enabled" : "disabled") + ")*";
        changes += "\n";
        if (cfg.rate_limit_min != old_cfg.rate_limit_min) changes += "Rate limit: " + to_string(old_cfg.rate_limit_min) + " → " + to_string(cfg.rate_limit_min) + " req/min\n";
        if (cfg.max_file_mb    != old_cfg.max_file_mb)    changes += "Max file: " + to_string(old_cfg.max_file_mb) + " → " + to_string(cfg.max_file_mb) + " MB\n";
        if (cfg.max_text_chars != old_cfg.max_text_chars) changes += "Max chars: " + to_string(old_cfg.max_text_chars) + " → " + to_string(cfg.max_text_chars) + "\n";
        if (cfg.note           != old_cfg.note)           changes += "Note: \"" + cfg.note + "\"\n";

        discord_log("⚙️ Admin — Tool Config Updated",
            "**Tool:** `" + tool_id + "`\n**From IP:** " + req.remote_addr + "\n\n" + changes,
            0xF59E0B  /* amber */);

        res.set_content(R"({"ok":true})", "application/json");
    });
}
