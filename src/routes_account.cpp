/**
 * Luma Tools - Account and billing routes
 */

#include <cctype>

#include "common.h"
#include "routes.h"
#include "stats.h"

static string billing_env(const char* name, const string& fallback = "") {
    const char* value = std::getenv(name);
    return value ? string(value) : fallback;
}

static string trim_copy(string text) {
    auto is_ws = [](unsigned char c) { return std::isspace(c) != 0; };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), [&](unsigned char c) { return !is_ws(c); }));
    text.erase(std::find_if(text.rbegin(), text.rend(), [&](unsigned char c) { return !is_ws(c); }).base(), text.end());
    return text;
}

static string lower_copy(string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return text;
}

static string html_escape(const string& text) {
    string out;
    out.reserve(text.size() + 16);
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

static string url_decode(const string& text) {
    string out;
    for (size_t i = 0; i < text.size(); ) {
        if (text[i] == '+') { out += ' '; i++; }
        else if (text[i] == '%' && i + 2 < text.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hex(text[i + 1]);
            int lo = hex(text[i + 2]);
            if (hi >= 0 && lo >= 0) { out.push_back((char)((hi << 4) | lo)); i += 3; }
            else { out += text[i++]; }
        } else {
            out += text[i++];
        }
    }
    return out;
}

static string form_value(const string& body, const string& key) {
    string needle = key + "=";
    size_t pos = body.find(needle);
    if (pos == string::npos) return "";
    size_t start = pos + needle.size();
    size_t end = body.find('&', start);
    return url_decode(body.substr(start, end == string::npos ? string::npos : end - start));
}

static string cookie_value(const httplib::Request& req, const string& key) {
    auto it = req.headers.find("Cookie");
    if (it == req.headers.end()) return "";
    string cookie = it->second;
    string needle = key + "=";
    size_t pos = cookie.find(needle);
    if (pos == string::npos) return "";
    size_t start = pos + needle.size();
    size_t end = cookie.find(';', start);
    return cookie.substr(start, end == string::npos ? string::npos : end - start);
}

static string session_cookie_name() {
    return "lt_session";
}

static bool current_account(const httplib::Request& req, AccountUser& user) {
    string token = cookie_value(req, session_cookie_name());
    return !token.empty() && account_get_user_by_session(token, user);
}

// ─── Shared site-aesthetic shell for all account pages ──────────────────────

static const char* ACCOUNT_PAGE_CSS = R"CSS(
*{box-sizing:border-box;margin:0;padding:0}
:root{
  --bg-primary:#0a0a0f;--bg-card:rgba(18,18,30,0.65);--bg-card-hover:rgba(25,25,40,0.75);
  --text-primary:#f0f0f5;--text-secondary:#8888a0;--text-muted:#555570;
  --accent:#7c5cff;--accent-light:#9b80ff;--accent-glow:rgba(124,92,255,0.3);
  --accent-2:#00d4ff;--accent-3:#ff6bca;
  --border:rgba(255,255,255,.08);--border-strong:rgba(255,255,255,.16);
}
html,body{height:100%}
body{
  margin:0;background:var(--bg-primary);color:var(--text-primary);
  font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;
  min-height:100vh;display:flex;align-items:center;justify-content:center;
  padding:24px;position:relative;overflow-x:hidden;
}
.bg-orbs{position:fixed;inset:0;pointer-events:none;z-index:0;overflow:hidden;filter:blur(80px);opacity:.35}
.orb{position:absolute;border-radius:50%;animation:float 22s ease-in-out infinite}
.orb-1{width:520px;height:520px;background:radial-gradient(circle,var(--accent) 0%,transparent 70%);top:-180px;right:-120px}
.orb-2{width:440px;height:440px;background:radial-gradient(circle,var(--accent-2) 0%,transparent 70%);bottom:-140px;left:-100px;animation-delay:-7s}
.orb-3{width:380px;height:380px;background:radial-gradient(circle,var(--accent-3) 0%,transparent 70%);top:50%;left:50%;transform:translate(-50%,-50%);animation-delay:-14s}
@keyframes float{0%,100%{transform:translateY(0) translateX(0) scale(1)}33%{transform:translateY(-40px) translateX(20px) scale(1.06)}66%{transform:translateY(20px) translateX(-30px) scale(.94)}}
.shell{position:relative;z-index:1;width:100%;max-width:480px}
.brand{display:flex;align-items:center;justify-content:center;gap:10px;margin-bottom:22px;color:var(--text-primary);text-decoration:none;font-weight:800;font-size:1.1rem;letter-spacing:.02em}
.brand i{color:var(--accent)}
.brand span{color:var(--text-secondary);font-weight:600}
.card{
  background:var(--bg-card);backdrop-filter:blur(20px);-webkit-backdrop-filter:blur(20px);
  border:1px solid var(--border);border-radius:18px;padding:32px;
  box-shadow:0 20px 60px rgba(0,0,0,.55);
}
.card-wide{max-width:760px}
h1{font-size:1.55rem;font-weight:700;margin-bottom:6px;letter-spacing:-.01em}
.intro{color:var(--text-secondary);font-size:.92rem;margin-bottom:22px;line-height:1.5}
.field{margin-bottom:14px}
label{display:block;font-size:.72rem;text-transform:uppercase;letter-spacing:.09em;color:var(--text-secondary);margin-bottom:6px;font-weight:600}
input[type=email],input[type=password],input[type=text]{
  width:100%;padding:12px 14px;background:rgba(255,255,255,.04);
  border:1px solid var(--border);border-radius:10px;color:var(--text-primary);
  font-size:.95rem;outline:none;transition:border-color .15s,background .15s;
}
input:focus{border-color:var(--accent);background:rgba(124,92,255,.06)}
button.primary,a.primary{
  display:inline-flex;align-items:center;justify-content:center;gap:8px;width:100%;
  padding:13px 16px;background:linear-gradient(135deg,var(--accent),var(--accent-light));
  border:0;border-radius:10px;color:#fff;font-weight:700;font-size:.96rem;cursor:pointer;
  text-decoration:none;transition:transform .12s,box-shadow .12s;
  box-shadow:0 8px 20px rgba(124,92,255,.28);
}
button.primary:hover,a.primary:hover{transform:translateY(-1px);box-shadow:0 12px 28px rgba(124,92,255,.36)}
button.ghost,a.ghost{
  display:inline-flex;align-items:center;justify-content:center;gap:8px;
  padding:11px 14px;background:rgba(255,255,255,.04);border:1px solid var(--border);
  border-radius:10px;color:var(--text-primary);text-decoration:none;font-weight:600;
  font-size:.92rem;cursor:pointer;transition:background .15s,border-color .15s;
}
button.ghost:hover,a.ghost:hover{background:rgba(255,255,255,.07);border-color:var(--border-strong)}
.divider{display:flex;align-items:center;gap:12px;margin:22px 0 18px;color:var(--text-muted);font-size:.78rem}
.divider::before,.divider::after{content:'';flex:1;height:1px;background:var(--border)}
.alt-link{text-align:center;font-size:.88rem;color:var(--text-secondary);margin-top:6px}
.alt-link a{color:var(--accent-light);text-decoration:none;font-weight:600}
.alt-link a:hover{text-decoration:underline}
.msg{padding:10px 12px;border-radius:8px;font-size:.86rem;margin-bottom:16px;display:flex;gap:8px;align-items:flex-start}
.msg-ok{background:rgba(52,211,153,.1);border:1px solid rgba(52,211,153,.3);color:#86efac}
.msg-err{background:rgba(248,113,113,.1);border:1px solid rgba(248,113,113,.3);color:#fca5a5}
.back-home{display:inline-flex;align-items:center;gap:6px;margin-top:16px;color:var(--text-secondary);text-decoration:none;font-size:.86rem}
.back-home:hover{color:var(--text-primary)}
.chip{display:inline-flex;align-items:center;gap:6px;padding:4px 10px;border-radius:999px;background:rgba(124,92,255,.14);border:1px solid rgba(124,92,255,.3);color:#d8d2ff;font-size:.74rem;font-weight:600;text-transform:uppercase;letter-spacing:.04em}
.chip-free{background:rgba(255,255,255,.05);border-color:var(--border);color:var(--text-secondary)}
.chip-pro{background:rgba(124,92,255,.18);border-color:rgba(124,92,255,.45);color:#cabaff}
.profile-grid{display:grid;grid-template-columns:1fr 1fr;gap:18px;margin-top:20px}
@media(max-width:560px){.profile-grid{grid-template-columns:1fr}}
.profile-row{padding:14px 0;border-bottom:1px solid var(--border)}
.profile-row:last-child{border-bottom:0}
.profile-label{font-size:.72rem;text-transform:uppercase;letter-spacing:.08em;color:var(--text-secondary);margin-bottom:4px;font-weight:600}
.profile-value{color:var(--text-primary);font-size:.95rem;word-break:break-word}
.actions-row{display:flex;flex-wrap:wrap;gap:10px;margin-top:22px}
.actions-row > *{flex:1;min-width:160px}
)CSS";

static string render_account_shell(const string& title, const string& body_html) {
    std::ostringstream html;
    html << "<!DOCTYPE html><html lang=\"en\"><head>"
         << "<meta charset=\"UTF-8\">"
         << "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
         << "<title>" << html_escape(title) << " — Luma Tools</title>"
         << "<link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css\">"
         << "<style>" << ACCOUNT_PAGE_CSS << "</style>"
         << "</head><body>"
         << "<div class=\"bg-orbs\"><div class=\"orb orb-1\"></div><div class=\"orb orb-2\"></div><div class=\"orb orb-3\"></div></div>"
         << "<div class=\"shell\">"
         << "<a class=\"brand\" href=\"/\"><i class=\"fas fa-bolt\"></i>LUMA<span>TOOLS</span></a>"
         << body_html
         << "<a class=\"back-home\" href=\"/\"><i class=\"fas fa-arrow-left\"></i> Back to all tools</a>"
         << "</div></body></html>";
    return html.str();
}

static string render_message_block(const string& message, const string& error) {
    string out;
    if (!message.empty()) {
        out += "<div class=\"msg msg-ok\"><i class=\"fas fa-check-circle\"></i><div>" + html_escape(message) + "</div></div>";
    }
    if (!error.empty()) {
        out += "<div class=\"msg msg-err\"><i class=\"fas fa-exclamation-circle\"></i><div>" + html_escape(error) + "</div></div>";
    }
    return out;
}

static string render_login_page(const AccountUser*, const string& message = "", const string& error = "") {
    std::ostringstream b;
    b << "<div class=\"card\">"
      << "<h1>Welcome back</h1>"
      << "<p class=\"intro\">Sign in to manage your plan and access your saved settings.</p>"
      << render_message_block(message, error)
      << "<form method=\"POST\" action=\"/account/login\" autocomplete=\"on\">"
         "<div class=\"field\"><label for=\"li-email\">Email</label>"
         "<input id=\"li-email\" type=\"email\" name=\"email\" placeholder=\"you@example.com\" autocomplete=\"email\" required></div>"
         "<div class=\"field\"><label for=\"li-pw\">Password</label>"
         "<input id=\"li-pw\" type=\"password\" name=\"password\" placeholder=\"Your password\" autocomplete=\"current-password\" required></div>"
         "<button class=\"primary\" type=\"submit\"><i class=\"fas fa-sign-in-alt\"></i> Sign in</button>"
         "</form>"
      << "<div class=\"divider\">or</div>"
      << "<div class=\"alt-link\">New here? <a href=\"/account/register\">Create an account</a></div>"
      << "</div>";
    return render_account_shell("Sign in", b.str());
}

static string render_register_page(const AccountUser*, const string& message = "", const string& error = "") {
    std::ostringstream b;
    b << "<div class=\"card\">"
      << "<h1>Create your account</h1>"
      << "<p class=\"intro\">Free forever for the core tools. Upgrade to Pro anytime for unlimited AI and 2 GB uploads.</p>"
      << render_message_block(message, error)
      << "<form method=\"POST\" action=\"/account/register\" autocomplete=\"on\">"
         "<div class=\"field\"><label for=\"r-email\">Email</label>"
         "<input id=\"r-email\" type=\"email\" name=\"email\" placeholder=\"you@example.com\" autocomplete=\"email\" required></div>"
         "<div class=\"field\"><label for=\"r-name\">Display name <span style=\"text-transform:none;letter-spacing:0;color:var(--text-muted)\">(optional)</span></label>"
         "<input id=\"r-name\" type=\"text\" name=\"display_name\" placeholder=\"What should we call you?\" autocomplete=\"nickname\"></div>"
         "<div class=\"field\"><label for=\"r-pw\">Password</label>"
         "<input id=\"r-pw\" type=\"password\" name=\"password\" placeholder=\"At least 4 characters\" autocomplete=\"new-password\" required minlength=\"4\"></div>"
         "<button class=\"primary\" type=\"submit\"><i class=\"fas fa-user-plus\"></i> Create account</button>"
         "</form>"
      << "<div class=\"divider\">or</div>"
      << "<div class=\"alt-link\">Already have an account? <a href=\"/account/login\">Sign in</a></div>"
      << "</div>";
    return render_account_shell("Create account", b.str());
}

static string fmt_unix_short(int64_t ts) {
    if (ts <= 0) return "—";
    char buf[40];
    std::time_t t = (std::time_t)ts;
    std::tm tmv;
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    std::strftime(buf, sizeof(buf), "%b %d, %Y", &tmv);
    return buf;
}

static string render_account_dashboard(const AccountUser& user, const string& message, const string& error) {
    string plan_label = user.plan.empty() ? "free" : user.plan;
    bool is_pro = (plan_label == "pro" || plan_label == "starter");
    string chip_cls = is_pro ? "chip chip-pro" : "chip chip-free";
    string plan_display = is_pro
        ? std::string("Pro — ") + (user.account_status.empty() ? "active" : user.account_status)
        : std::string("Free plan");

    std::ostringstream b;
    b << "<div class=\"card card-wide\" style=\"max-width:760px\">"
      << "<div style=\"display:flex;justify-content:space-between;gap:14px;align-items:flex-start;flex-wrap:wrap\">"
      << "<div><h1>Your account</h1>"
      << "<p class=\"intro\">Signed in as <strong style=\"color:var(--text-primary)\">"
      << html_escape(user.email) << "</strong></p></div>"
      << "<span class=\"" << chip_cls << "\"><i class=\"fas fa-"
      << (is_pro ? "crown" : "user") << "\"></i> " << html_escape(plan_display) << "</span>"
      << "</div>"
      << render_message_block(message, error)
      << "<div class=\"profile-grid\">"
         "<div>"
      << "<div class=\"profile-row\"><div class=\"profile-label\">Display name</div>"
         "<div class=\"profile-value\">" << html_escape(user.display_name.empty() ? "—" : user.display_name) << "</div></div>"
      << "<div class=\"profile-row\"><div class=\"profile-label\">Email</div>"
         "<div class=\"profile-value\">" << html_escape(user.email) << "</div></div>"
      << "<div class=\"profile-row\"><div class=\"profile-label\">Member since</div>"
         "<div class=\"profile-value\">" << html_escape(fmt_unix_short(user.created_ts)) << "</div></div>"
         "</div>"
         "<div>"
      << "<div class=\"profile-row\"><div class=\"profile-label\">Current plan</div>"
         "<div class=\"profile-value\">" << html_escape(plan_display) << "</div></div>"
      << "<div class=\"profile-row\"><div class=\"profile-label\">Billing status</div>"
         "<div class=\"profile-value\">" << html_escape(user.account_status.empty() ? "active" : user.account_status) << "</div></div>"
      << "<div class=\"profile-row\"><div class=\"profile-label\">Stripe customer</div>"
         "<div class=\"profile-value\" style=\"font-family:monospace;font-size:.82rem;color:var(--text-secondary)\">"
      << html_escape(user.stripe_customer_id.empty() ? "Not subscribed" : user.stripe_customer_id) << "</div></div>"
         "</div></div>";

    b << "<div class=\"actions-row\">";
    if (is_pro && !user.stripe_customer_id.empty()) {
        b << "<button class=\"primary\" type=\"button\" onclick=\"openBillingPortal(this)\">"
             "<i class=\"fas fa-credit-card\"></i> Manage subscription</button>";
    } else {
        b << "<button class=\"primary\" type=\"button\" onclick=\"startProCheckout(this)\">"
             "<i class=\"fas fa-bolt\"></i> Upgrade to Pro</button>";
    }
    b << "<form method=\"POST\" action=\"/account/logout\" style=\"margin:0\"><button class=\"ghost\" type=\"submit\" style=\"width:100%\"><i class=\"fas fa-sign-out-alt\"></i> Sign out</button></form>"
         "</div>"
         "</div>"
         "<script>"
         "async function openBillingPortal(btn){btn.disabled=true;const orig=btn.innerHTML;btn.innerHTML='<i class=\"fas fa-circle-notch fa-spin\"></i> Loading...';"
         "try{const r=await fetch('/api/billing/portal-session',{method:'POST',credentials:'same-origin'});const d=await r.json();"
         "if(r.ok&&d.url){window.location=d.url;return}alert(d.error||('Error '+r.status))}catch(e){alert('Network error')}finally{btn.disabled=false;btn.innerHTML=orig}}"
         "async function startProCheckout(btn){btn.disabled=true;const orig=btn.innerHTML;btn.innerHTML='<i class=\"fas fa-circle-notch fa-spin\"></i> Loading...';"
         "try{const r=await fetch('/api/billing/checkout-session',{method:'POST',headers:{'Content-Type':'application/json'},credentials:'same-origin',body:JSON.stringify({plan:'pro'})});const d=await r.json();"
         "if(r.ok&&d.url){window.location=d.url;return}alert(d.error||('Error '+r.status))}catch(e){alert('Network error')}finally{btn.disabled=false;btn.innerHTML=orig}}"
         "</script>";

    return render_account_shell("Your account", b.str());
}

static string normalize_email(string email) {
    return lower_copy(trim_copy(email));
}

static string stripe_plan_from_request(const string& plan_raw) {
    string plan = lower_copy(trim_copy(plan_raw));
    if (plan == "starter" || plan == "pro") return plan;
    return "pro";
}

// ─── Stripe HTTP helpers ────────────────────────────────────────────────────

static string url_form_enc(const string& s) {
    static const char* hex = "0123456789ABCDEF";
    string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        bool unreserved = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                          (c >= 'a' && c <= 'z') || c == '-' || c == '_' ||
                          c == '.' || c == '~';
        if (unreserved) out.push_back((char)c);
        else { out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 0xF]); }
    }
    return out;
}

static string build_form_body(const vector<pair<string,string>>& fields) {
    string body;
    bool first = true;
    for (const auto& kv : fields) {
        if (!first) body.push_back('&');
        first = false;
        body += url_form_enc(kv.first);
        body.push_back('=');
        body += url_form_enc(kv.second);
    }
    return body;
}

// POST to https://api.stripe.com/v1/<path> using HTTP Basic auth (secret key as username).
// Returns parsed JSON; on transport failure returns json::object() and sets error_out.
static json stripe_api_post(const string& path, const vector<pair<string,string>>& fields, string& error_out) {
    error_out.clear();
    string secret = billing_env("STRIPE_SECRET_KEY");
    if (secret.empty()) { error_out = "Stripe is not configured."; return json::object(); }

    string proc = get_processing_dir();
    string id = generate_job_id();
    string body_file = proc + "/stripe_" + id + "_body.txt";
    string resp_file = proc + "/stripe_" + id + "_resp.json";
    string hdr_file  = proc + "/stripe_" + id + "_hdr.txt";

    { ofstream f(body_file); f << build_form_body(fields); }
    // Stripe requires API version pinning recommended but optional; let secret act as username.
    string cmd =
        "curl -s --max-time 30 -u " + escape_arg(secret + ":") +
        " -H " + escape_arg("Stripe-Version: 2024-06-20") +
        " -H " + escape_arg("Content-Type: application/x-www-form-urlencoded") +
        " --data-binary @" + escape_arg(body_file) +
        " -o " + escape_arg(resp_file) +
        " -D " + escape_arg(hdr_file) +
        " " + escape_arg("https://api.stripe.com/v1/" + path);

    int rc;
    exec_command(cmd, rc);

    json result = json::object();
    if (fs::exists(resp_file)) {
        try {
            std::ifstream f(resp_file);
            std::ostringstream ss; ss << f.rdbuf();
            string body = ss.str();
            if (!body.empty()) result = json::parse(body);
        } catch (const std::exception& e) {
            error_out = string("Could not parse Stripe response: ") + e.what();
        }
    } else {
        error_out = "No response from Stripe (network error).";
    }

    try { fs::remove(body_file); fs::remove(resp_file); fs::remove(hdr_file); } catch (...) {}

    if (result.contains("error") && result["error"].is_object()) {
        if (error_out.empty()) error_out = result["error"].value("message", "Stripe error");
    }
    return result;
}

// ─── Stripe webhook signature verification ──────────────────────────────────

static string hmac_sha256_hex(const string& secret, const string& payload) {
    string proc = get_processing_dir();
    string id = generate_job_id();
    string in_file  = proc + "/whsig_" + id + "_in.bin";
    string out_file = proc + "/whsig_" + id + "_out.txt";
    { ofstream f(in_file, std::ios::binary); f.write(payload.data(), (std::streamsize)payload.size()); }
    string cmd = "openssl dgst -sha256 -hmac " + escape_arg(secret) +
                 " < " + escape_arg(in_file) +
                 " > " + escape_arg(out_file);
    int rc;
    exec_command(cmd, rc);

    string hex;
    if (fs::exists(out_file)) {
        std::ifstream f(out_file);
        std::ostringstream ss; ss << f.rdbuf();
        string raw = ss.str();
        // Output looks like "(stdin)= abcdef..." or just "abcdef...\n"
        size_t eq = raw.find('=');
        string tail = (eq == string::npos) ? raw : raw.substr(eq + 1);
        for (char c : tail) {
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
                hex.push_back((char)std::tolower((unsigned char)c));
            }
        }
    }
    try { fs::remove(in_file); fs::remove(out_file); } catch (...) {}
    return hex;
}

// Constant-time compare for hex digests.
static bool ct_equal_hex(const string& a, const string& b) {
    if (a.size() != b.size() || a.empty()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= (unsigned char)(a[i] ^ b[i]);
    return diff == 0;
}

// Verify Stripe-Signature header against payload. Returns true if at least one v1 signature matches.
static bool verify_stripe_signature(const string& sig_header, const string& payload, const string& secret, int tolerance_seconds = 300) {
    if (sig_header.empty() || secret.empty()) return false;

    string ts;
    vector<string> v1_sigs;
    size_t i = 0;
    while (i < sig_header.size()) {
        size_t comma = sig_header.find(',', i);
        string part = sig_header.substr(i, comma == string::npos ? string::npos : comma - i);
        size_t eq = part.find('=');
        if (eq != string::npos) {
            string k = trim_copy(part.substr(0, eq));
            string v = trim_copy(part.substr(eq + 1));
            if (k == "t") ts = v;
            else if (k == "v1") v1_sigs.push_back(v);
        }
        if (comma == string::npos) break;
        i = comma + 1;
    }
    if (ts.empty() || v1_sigs.empty()) return false;

    int64_t ts_int = 0;
    try { ts_int = std::stoll(ts); } catch (...) { return false; }
    int64_t now = (int64_t)std::time(nullptr);
    if (tolerance_seconds > 0 && std::llabs((long long)(now - ts_int)) > tolerance_seconds) return false;

    string signed_payload = ts + "." + payload;
    string expected = hmac_sha256_hex(secret, signed_payload);
    if (expected.empty()) return false;
    for (const auto& sig : v1_sigs) {
        if (ct_equal_hex(expected, lower_copy(sig))) return true;
    }
    return false;
}

static string auth_feedback_text(const string& code, bool is_error) {
    if (code == "registration_success") return "Registration successful. Please sign in with your new account.";
    if (code == "signed_in") return "Signed in successfully.";
    if (code == "signed_out") return "Signed out successfully.";
    if (code == "invalid_email_or_password") return "Email or password was incorrect.";
    if (code == "invalid_email") return "Please enter a valid email address.";
    if (code == "password_too_short") return "Password must be at least 4 characters long.";
    if (code == "duplicate_account") return "An account with that email already exists. Please sign in instead.";
    if (code == "checkout_success") return "Payment received! Your plan will activate within a few seconds.";
    if (code == "checkout_canceled") return "Checkout was canceled. You can try again any time.";
    if (is_error && !code.empty()) return code;
    return "";
}

// ─── Public plan-resolution helpers (declared in routes.h) ──────────────────

string account_plan_for_request(const httplib::Request& req) {
    AccountUser user;
    if (!current_account(req, user)) return "free";
    string p = lower_copy(trim_copy(user.plan));
    if (p.empty()) return "free";
    // Only treat the user as paid if their subscription is currently active.
    string s = lower_copy(trim_copy(user.account_status));
    if (p == "pro" || p == "starter") {
        if (s == "active" || s == "trialing" || s == "past_due") return p;
        return "free";
    }
    return "free";
}

int account_user_id_for_request(const httplib::Request& req) {
    AccountUser user;
    if (!current_account(req, user)) return 0;
    return user.id;
}

void register_account_routes(httplib::Server& svr) {
    svr.Get("/account", [](const httplib::Request& req, httplib::Response& res) {
        AccountUser user;
        if (!current_account(req, user)) {
            res.set_header("Location", "/account/login");
            res.status = 302;
            return;
        }
        string message = req.has_param("msg") ? req.get_param_value("msg") : "";
        string error   = req.has_param("err") ? req.get_param_value("err") : "";
        message = auth_feedback_text(message, false);
        error   = auth_feedback_text(error, true);
        res.set_content(render_account_dashboard(user, message, error), "text/html");
    });

    svr.Get("/account/register", [](const httplib::Request& req, httplib::Response& res) {
        AccountUser user;
        bool has_user = current_account(req, user);
        string message = req.has_param("msg") ? req.get_param_value("msg") : "";
        string error = req.has_param("err") ? req.get_param_value("err") : "";
        message = auth_feedback_text(message, false);
        error = auth_feedback_text(error, true);
        res.set_content(render_register_page(has_user ? &user : nullptr, message, error), "text/html");
    });

    svr.Get("/account/login", [](const httplib::Request& req, httplib::Response& res) {
        AccountUser user;
        bool has_user = current_account(req, user);
        string message = req.has_param("msg") ? req.get_param_value("msg") : "";
        string error = req.has_param("err") ? req.get_param_value("err") : "";
        message = auth_feedback_text(message, false);
        error = auth_feedback_text(error, true);
        res.set_content(render_login_page(has_user ? &user : nullptr, message, error), "text/html");
    });

    svr.Post("/account/register", [](const httplib::Request& req, httplib::Response& res) {
        string email = normalize_email(form_value(req.body, "email"));
        string password = form_value(req.body, "password");
        string display_name = trim_copy(form_value(req.body, "display_name"));
        
        if (email.empty() || email.find('@') == string::npos) {
            res.set_header("Location", "/account?err=invalid_email");
            res.status = 302;
            return;
        }
        if (password.empty() || password.length() < 4) {
            res.set_header("Location", "/account?err=password_too_short");
            res.status = 302;
            return;
        }

        AccountUser user;
        if (!account_upsert_user(email, display_name, password, user)) {
            res.set_header("Location", "/account/register?err=duplicate_account");
            res.status = 302;
            return;
        }

        string token;
        int64_t expires_ts = 0;
        if (!account_create_session(user.id, req.remote_addr, req.get_header_value("User-Agent"), token, expires_ts)) {
            res.status = 500;
            res.set_content(R"({"error":"Could not create session."})", "application/json");
            return;
        }

        int max_age = (int)std::max<int64_t>(0, expires_ts - std::time(nullptr));
        string cookie = session_cookie_name() + "=" + token + "; Path=/; HttpOnly; SameSite=Lax; Max-Age=" + to_string(max_age);
        res.set_header("Set-Cookie", cookie);
        res.set_header("Location", "/account/login?msg=registration_success");
        res.status = 302;
    });

    svr.Post("/account/login", [](const httplib::Request& req, httplib::Response& res) {
        string email = normalize_email(form_value(req.body, "email"));
        string password = form_value(req.body, "password");
        
        if (email.empty() || email.find('@') == string::npos) {
            res.set_header("Location", "/account/login?err=invalid_email_or_password");
            res.status = 302;
            return;
        }
        if (password.empty()) {
            res.set_header("Location", "/account/login?err=invalid_email_or_password");
            res.status = 302;
            return;
        }

        AccountUser user;
        if (!account_verify_password(email, password, user)) {
            res.set_header("Location", "/account/login?err=invalid_email_or_password");
            res.status = 302;
            return;
        }

        string token;
        int64_t expires_ts = 0;
        if (!account_create_session(user.id, req.remote_addr, req.get_header_value("User-Agent"), token, expires_ts)) {
            res.status = 500;
            res.set_content(R"({"error":"Could not create session."})", "application/json");
            return;
        }

        int max_age = (int)std::max<int64_t>(0, expires_ts - std::time(nullptr));
        string cookie = session_cookie_name() + "=" + token + "; Path=/; HttpOnly; SameSite=Lax; Max-Age=" + to_string(max_age);
        res.set_header("Set-Cookie", cookie);
        res.set_header("Location", "/account?msg=signed_in");
        res.status = 302;
    });

    svr.Post("/account/logout", [](const httplib::Request& req, httplib::Response& res) {
        string token = cookie_value(req, session_cookie_name());
        if (!token.empty()) account_delete_session(token);
        res.set_header("Set-Cookie", session_cookie_name() + "=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0");
        res.set_header("Location", "/account?msg=signed_out");
        res.status = 302;
    });

    svr.Get("/api/account/me", [](const httplib::Request& req, httplib::Response& res) {
        AccountUser user;
        if (!current_account(req, user)) {
            res.status = 401;
            res.set_content(R"({"error":"Not signed in"})", "application/json");
            return;
        }

        json payload = {
            {"user", {
                {"id", user.id},
                {"email", user.email},
                {"display_name", user.display_name},
                {"account_status", user.account_status},
                {"plan", user.plan},
                {"stripe_customer_id", user.stripe_customer_id},
                {"stripe_subscription_id", user.stripe_subscription_id}
            }}
        };
        res.set_content(payload.dump(), "application/json");
    });

    svr.Post("/api/billing/checkout-session", [](const httplib::Request& req, httplib::Response& res) {
        if (billing_env("STRIPE_SECRET_KEY").empty()) {
            res.status = 503;
            res.set_content(R"({"error":"Stripe is not configured yet."})", "application/json");
            return;
        }

        AccountUser user;
        if (!current_account(req, user)) {
            res.status = 401;
            res.set_content(R"({"error":"Please sign in first."})", "application/json");
            return;
        }

        json body;
        try { body = json::parse(req.body); } catch (...) {}
        string plan = stripe_plan_from_request(body.value("plan", "pro"));
        string price_id = (plan == "starter") ? billing_env("STRIPE_PRICE_STARTER") : billing_env("STRIPE_PRICE_PRO");
        if (price_id.empty()) {
            res.status = 503;
            res.set_content(R"({"error":"Missing Stripe price IDs for the selected plan."})", "application/json");
            return;
        }

        string app_base = billing_env("APP_BASE_URL", "http://localhost:8080");
        string success_url = app_base + "/account/login?msg=checkout_success&session_id={CHECKOUT_SESSION_ID}";
        string cancel_url  = app_base + "/account/login?err=checkout_canceled";

        vector<pair<string,string>> fields = {
            {"mode", "subscription"},
            {"line_items[0][price]", price_id},
            {"line_items[0][quantity]", "1"},
            {"success_url", success_url},
            {"cancel_url", cancel_url},
            {"client_reference_id", to_string(user.id)},
            {"allow_promotion_codes", "true"},
            {"metadata[user_id]", to_string(user.id)},
            {"metadata[plan]", plan},
            {"subscription_data[metadata][user_id]", to_string(user.id)},
            {"subscription_data[metadata][plan]", plan},
        };
        if (!user.stripe_customer_id.empty()) {
            fields.push_back({"customer", user.stripe_customer_id});
        } else if (!user.email.empty()) {
            fields.push_back({"customer_email", user.email});
        }

        string err;
        json session = stripe_api_post("checkout/sessions", fields, err);
        string url = session.value("url", "");
        if (url.empty()) {
            res.status = 502;
            json payload = { {"error", err.empty() ? "Stripe did not return a checkout URL." : err} };
            res.set_content(payload.dump(), "application/json");
            return;
        }

        json payload = {
            {"ok", true},
            {"url", url},
            {"plan", plan},
            {"session_id", session.value("id", "")}
        };
        res.set_content(payload.dump(), "application/json");
    });

    svr.Post("/api/billing/portal-session", [](const httplib::Request& req, httplib::Response& res) {
        if (billing_env("STRIPE_SECRET_KEY").empty()) {
            res.status = 503;
            res.set_content(R"({"error":"Stripe is not configured yet."})", "application/json");
            return;
        }
        AccountUser user;
        if (!current_account(req, user)) {
            res.status = 401;
            res.set_content(R"({"error":"Please sign in first."})", "application/json");
            return;
        }
        if (user.stripe_customer_id.empty()) {
            res.status = 400;
            res.set_content(R"({"error":"No billing customer on file. Subscribe first."})", "application/json");
            return;
        }

        string app_base = billing_env("APP_BASE_URL", "http://localhost:8080");
        vector<pair<string,string>> fields = {
            {"customer", user.stripe_customer_id},
            {"return_url", app_base + "/account/login"}
        };
        string err;
        json sess = stripe_api_post("billing_portal/sessions", fields, err);
        string url = sess.value("url", "");
        if (url.empty()) {
            res.status = 502;
            json payload = { {"error", err.empty() ? "Stripe did not return a portal URL." : err} };
            res.set_content(payload.dump(), "application/json");
            return;
        }
        json payload = { {"ok", true}, {"url", url} };
        res.set_content(payload.dump(), "application/json");
    });

    svr.Post("/api/billing/webhook", [](const httplib::Request& req, httplib::Response& res) {
        // Verify signature first — never trust the body until we know it came from Stripe.
        string webhook_secret = billing_env("STRIPE_WEBHOOK_SECRET");
        if (webhook_secret.empty()) {
            res.status = 503;
            res.set_content(R"({"error":"Webhook not configured."})", "application/json");
            return;
        }
        string sig_header = req.get_header_value("Stripe-Signature");
        if (!verify_stripe_signature(sig_header, req.body, webhook_secret)) {
            cerr << "[Luma Tools] Rejected webhook with invalid Stripe signature." << endl;
            res.status = 400;
            res.set_content(R"({"error":"Invalid signature."})", "application/json");
            return;
        }

        json event;
        try { event = json::parse(req.body); } catch (...) {
            res.status = 400;
            res.set_content(R"({"error":"Invalid JSON webhook payload."})", "application/json");
            return;
        }

        string type = event.value("type", "");
        json obj = event.contains("data") && event["data"].contains("object") ? event["data"]["object"] : json{};

        auto plan_for_price = [](const string& price_id) {
            if (price_id.empty()) return string("free");
            if (price_id == billing_env("STRIPE_PRICE_STARTER")) return string("starter");
            if (price_id == billing_env("STRIPE_PRICE_PRO")) return string("pro");
            return string("free");
        };

        if (type == "checkout.session.completed") {
            // Stripe sends customer + subscription IDs here. Tie them to our user via client_reference_id
            // so subsequent subscription.* events can find the user by customer_id even before the
            // subscription record exists.
            int user_id = 0;
            try { user_id = std::stoi(obj.value("client_reference_id", "0")); } catch (...) {}
            string customer_id = obj.value("customer", "");
            string subscription_id = obj.value("subscription", "");
            if (user_id > 0 && !customer_id.empty()) {
                // Seed a minimal active row so the user's plan flips immediately.
                // The subscription.updated webhook that follows will fill in price/period.
                account_upsert_subscription(user_id, "pro", "active",
                    customer_id, subscription_id, "", 0, event.dump());
            }
            res.set_content(R"({"ok":true})", "application/json");
            return;
        }

        if (type != "customer.subscription.created" &&
            type != "customer.subscription.updated" &&
            type != "customer.subscription.deleted") {
            // Acknowledge other events so Stripe doesn't retry; nothing to do.
            res.set_content(R"({"ok":true,"ignored":true})", "application/json");
            return;
        }

        string customer_id = obj.value("customer", "");
        string subscription_id = obj.value("id", "");
        string status = obj.value("status", "inactive");
        string price_id;
        int64_t current_period_end = obj.value("current_period_end", (int64_t)0);
        if (obj.contains("items") && obj["items"].contains("data") && obj["items"]["data"].is_array() && !obj["items"]["data"].empty()) {
            auto first_item = obj["items"]["data"][0];
            if (first_item.contains("price")) price_id = first_item["price"].value("id", "");
            // Newer API versions moved current_period_end onto the subscription item.
            if (current_period_end == 0) current_period_end = first_item.value("current_period_end", (int64_t)0);
        }

        string plan = plan_for_price(price_id);

        AccountUser user;
        bool have_user = false;
        if (!subscription_id.empty()) have_user = account_find_user_by_stripe_subscription_id(subscription_id, user);
        if (!have_user && !customer_id.empty()) have_user = account_find_user_by_stripe_customer_id(customer_id, user);

        if (!have_user) {
            // Fall back to metadata.user_id which we set when creating the checkout session.
            int meta_uid = 0;
            if (obj.contains("metadata") && obj["metadata"].is_object()) {
                try { meta_uid = std::stoi(obj["metadata"].value("user_id", "0")); } catch (...) {}
            }
            if (meta_uid > 0 && account_get_user_by_id(meta_uid, user)) have_user = true;
        }

        if (have_user) {
            if (type == "customer.subscription.deleted" || status == "canceled" || status == "incomplete_expired") {
                status = "canceled";
                plan = "free";
            }
            account_upsert_subscription(
                user.id, plan, status, customer_id, subscription_id, price_id,
                current_period_end, event.dump()
            );
        } else {
            cerr << "[Luma Tools] Webhook " << type << " for sub=" << subscription_id
                 << " cust=" << customer_id << " could not be matched to a user." << endl;
        }

        res.set_content(R"({"ok":true})", "application/json");
    });
}
