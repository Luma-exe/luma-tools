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
/* Password field with show/hide eye toggle */
.pw-wrap{position:relative}
.pw-wrap input{padding-right:42px}
.pw-eye{position:absolute;right:10px;top:50%;transform:translateY(-50%);background:transparent;border:0;color:var(--text-secondary);cursor:pointer;padding:6px 8px;border-radius:6px;font-size:.95rem;line-height:1}
.pw-eye:hover{color:var(--text-primary);background:rgba(255,255,255,.05)}
.pw-eye:focus{outline:none;color:var(--accent-light)}
.pw-mismatch{color:#fca5a5;font-size:.8rem;margin-top:-4px;margin-bottom:10px;display:none}
.pw-mismatch.show{display:block}
)CSS";

static string pw_field(const string& id, const string& name, const string& placeholder,
                       const string& autocomplete, bool required = true, int min_len = 0) {
    std::ostringstream f;
    f << "<div class=\"pw-wrap\">"
      << "<input id=\"" << id << "\" type=\"password\" name=\"" << name
      << "\" placeholder=\"" << placeholder << "\" autocomplete=\"" << autocomplete << "\""
      << (required ? " required" : "")
      << (min_len > 0 ? (" minlength=\"" + to_string(min_len) + "\"") : "")
      << ">"
      << "<button type=\"button\" class=\"pw-eye\" aria-label=\"Show password\" "
         "onclick=\"(function(b){var i=b.previousElementSibling;var on=i.type==='password';"
         "i.type=on?'text':'password';b.innerHTML=on?'<i class=\\'fas fa-eye-slash\\'></i>':'<i class=\\'fas fa-eye\\'></i>';"
         "b.setAttribute('aria-label',on?'Hide password':'Show password');})(this)\">"
         "<i class=\"fas fa-eye\"></i></button>"
      << "</div>";
    return f.str();
}

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

static string render_oauth_buttons() {
    bool discord_on = !billing_env("DISCORD_OAUTH_CLIENT_ID").empty();
    bool google_on  = !billing_env("GOOGLE_OAUTH_CLIENT_ID").empty();
    if (!discord_on && !google_on) return "";
    string out = "<div style=\"display:flex;flex-direction:column;gap:10px\">";
    if (google_on) {
        out += "<a class=\"ghost\" href=\"/account/oauth/google\" "
               "style=\"width:100%;background:#fff;border-color:rgba(0,0,0,.15);color:#222\">"
               "<i class=\"fab fa-google\" style=\"color:#ea4335\"></i> Continue with Google</a>";
    }
    if (discord_on) {
        out += "<a class=\"ghost\" href=\"/account/oauth/discord\" "
               "style=\"width:100%;background:rgba(88,101,242,.12);border-color:rgba(88,101,242,.4);color:#c6cdff\">"
               "<i class=\"fab fa-discord\" style=\"color:#5865f2\"></i> Continue with Discord</a>";
    }
    out += "</div><div class=\"divider\">or</div>";
    return out;
}

static string render_login_page(const AccountUser*, const string& message = "", const string& error = "") {
    std::ostringstream b;
    b << "<div class=\"card\">"
      << "<h1>Welcome back</h1>"
      << "<p class=\"intro\">Sign in to manage your plan and access your saved settings.</p>"
      << render_message_block(message, error)
      << render_oauth_buttons()
      << "<form method=\"POST\" action=\"/account/login\" autocomplete=\"on\">"
         "<div class=\"field\"><label for=\"li-email\">Email</label>"
         "<input id=\"li-email\" type=\"email\" name=\"email\" placeholder=\"you@example.com\" autocomplete=\"email\" required></div>"
         "<div class=\"field\"><label for=\"li-pw\">Password</label>"
      << pw_field("li-pw", "password", "Your password", "current-password", true)
      << "</div>"
         "<button class=\"primary\" type=\"submit\"><i class=\"fas fa-sign-in-alt\"></i> Sign in</button>"
         "</form>"
      << "<div style=\"text-align:right;margin-top:-4px;margin-bottom:4px\"><a href=\"/account/forgot\" style=\"color:var(--text-secondary);font-size:.84rem;text-decoration:none\">Forgot password?</a></div>"
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
      << render_oauth_buttons()
      << "<form method=\"POST\" action=\"/account/register\" autocomplete=\"on\" onsubmit=\"return ltCheckMatch(this)\">"
         "<div class=\"field\"><label for=\"r-email\">Email</label>"
         "<input id=\"r-email\" type=\"email\" name=\"email\" placeholder=\"you@example.com\" autocomplete=\"email\" required></div>"
         "<div class=\"field\"><label for=\"r-name\">Display name <span style=\"text-transform:none;letter-spacing:0;color:var(--text-muted)\">(optional)</span></label>"
         "<input id=\"r-name\" type=\"text\" name=\"display_name\" placeholder=\"What should we call you?\" autocomplete=\"nickname\"></div>"
         "<div class=\"field\"><label for=\"r-pw\">Password</label>"
      << pw_field("r-pw", "password", "At least 8 characters", "new-password", true, 8)
      << "</div>"
         "<div class=\"field\"><label for=\"r-pw2\">Confirm password</label>"
      << pw_field("r-pw2", "password_confirm", "Re-type your password", "new-password", true, 8)
      << "<div id=\"pwMismatch\" class=\"pw-mismatch\"><i class=\"fas fa-exclamation-circle\"></i> Passwords don't match</div>"
         "</div>"
         "<button class=\"primary\" type=\"submit\"><i class=\"fas fa-user-plus\"></i> Create account</button>"
         "</form>"
         "<script>function ltCheckMatch(f){var a=f.password.value,b=f.password_confirm.value,w=document.getElementById('pwMismatch');"
         "if(a!==b){w.classList.add('show');f.password_confirm.focus();return false}"
         "w.classList.remove('show');return true}"
         "document.getElementById('r-pw2').addEventListener('input',function(){"
         "var a=document.getElementById('r-pw').value,b=this.value,w=document.getElementById('pwMismatch');"
         "if(b&&a!==b)w.classList.add('show');else w.classList.remove('show')});</script>"
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

// Unified profile renderer used by BOTH /account (private view of own row)
// and /u/<name> (public view, possibly looking at your own).
// is_own = true → full dashboard (email, billing buttons, Stripe customer,
//                  Sign out, API keys card).
// is_own = false → public view (no email/Stripe/billing/keys, "Try the tools"
//                  CTA, just identity + counters + share).
// Style follows /account: card-wide, profile-grid, actions-row. The public
// view reuses the same chrome — just hides the private fields.
static string render_profile_page(const AccountUser& target, bool is_own,
                                   const AccountStats& stats,
                                   const string& message, const string& error) {
    string plan_label = target.plan.empty() ? "free" : target.plan;
    bool is_pro = (plan_label == "pro" || plan_label == "starter");
    string chip_cls = is_pro ? "chip chip-pro" : "chip chip-free";
    string plan_display = is_pro
        ? std::string("Pro member")
        : std::string("Free plan");
    string display = target.display_name.empty()
        ? target.email.substr(0, target.email.find('@'))
        : target.display_name;
    string slug = display;  // for share URL
    // Avatar circle initial
    string initial = display.empty() ? std::string("?")
                                     : std::string(1, (char)std::toupper((unsigned char)display[0]));

    std::ostringstream b;
    b << "<div class=\"card card-wide\" style=\"max-width:760px\">";

    // ── Hero row: avatar + name/email + plan chip ──────────────────────────
    b << "<div style=\"display:flex;justify-content:space-between;gap:16px;align-items:flex-start;flex-wrap:wrap\">"
         "<div style=\"display:flex;gap:14px;align-items:center;min-width:0\">"
         "<div style=\"width:56px;height:56px;border-radius:50%;"
         "background:linear-gradient(135deg,var(--accent),var(--accent-3));"
         "display:flex;align-items:center;justify-content:center;font-size:1.5rem;"
         "font-weight:800;color:#fff;flex-shrink:0\">"
      << html_escape(initial) << "</div>"
         "<div style=\"min-width:0\">"
      << "<h1 style=\"margin:0\">" << html_escape(display) << "</h1>";
    if (is_own) {
        b << "<p class=\"intro\" style=\"margin:2px 0 0\">Signed in as "
             "<strong style=\"color:var(--text-primary)\">" << html_escape(target.email) << "</strong></p>";
    } else {
        b << "<p class=\"intro\" style=\"margin:2px 0 0\">Member since "
          << html_escape(fmt_unix_short(target.created_ts)) << "</p>";
    }
    b << "</div></div>"
         "<span class=\"" << chip_cls << "\" style=\"flex-shrink:0\">"
         "<i class=\"fas fa-" << (is_pro ? "crown" : "user") << "\"></i> "
      << html_escape(plan_display) << "</span>"
         "</div>";

    if (is_own) b << render_message_block(message, error);

    // ── Counter cards (public-safe, shown to everyone) ─────────────────────
    b << "<div style=\"display:grid;grid-template-columns:1fr 1fr;gap:14px;margin-top:20px\">"
         "<div style=\"padding:18px;background:rgba(124,92,255,.08);border:1px solid rgba(124,92,255,.25);"
         "border-radius:14px;text-align:center\">"
         "<div style=\"font-size:2rem;font-weight:800;color:var(--accent-light)\">" << stats.tools_used << "</div>"
         "<div style=\"font-size:.74rem;text-transform:uppercase;letter-spacing:.08em;"
         "color:var(--text-secondary);margin-top:4px\">files processed</div></div>"
         "<div style=\"padding:18px;background:rgba(0,212,255,.08);border:1px solid rgba(0,212,255,.25);"
         "border-radius:14px;text-align:center\">"
         "<div style=\"font-size:2rem;font-weight:800;color:var(--accent-2)\">" << stats.downloads << "</div>"
         "<div style=\"font-size:.74rem;text-transform:uppercase;letter-spacing:.08em;"
         "color:var(--text-secondary);margin-top:4px\">downloads</div></div>"
         "</div>";

    // ── Private profile grid (own only) — email, billing status, Stripe id
    if (is_own) {
        b << "<div class=\"profile-grid\" style=\"margin-top:18px\">"
             "<div>"
          << "<div class=\"profile-row\"><div class=\"profile-label\">Display name</div>"
             "<div class=\"profile-value\">" << html_escape(target.display_name.empty() ? "—" : target.display_name) << "</div></div>"
          << "<div class=\"profile-row\"><div class=\"profile-label\">Email</div>"
             "<div class=\"profile-value\">" << html_escape(target.email) << "</div></div>"
          << "<div class=\"profile-row\"><div class=\"profile-label\">Member since</div>"
             "<div class=\"profile-value\">" << html_escape(fmt_unix_short(target.created_ts)) << "</div></div>"
             "</div>"
             "<div>"
          << "<div class=\"profile-row\"><div class=\"profile-label\">Current plan</div>"
             "<div class=\"profile-value\">" << html_escape(is_pro ? (std::string("Pro — ") + (target.account_status.empty() ? "active" : target.account_status)) : std::string("Free plan")) << "</div></div>"
          << "<div class=\"profile-row\"><div class=\"profile-label\">Billing status</div>"
             "<div class=\"profile-value\">" << html_escape(target.account_status.empty() ? "active" : target.account_status) << "</div></div>"
          << "<div class=\"profile-row\"><div class=\"profile-label\">Stripe customer</div>"
             "<div class=\"profile-value\" style=\"font-family:monospace;font-size:.82rem;color:var(--text-secondary)\">"
          << html_escape(target.stripe_customer_id.empty() ? "Not subscribed" : target.stripe_customer_id) << "</div></div>"
             "</div></div>";
    }

    // ── Action row ─────────────────────────────────────────────────────────
    b << "<div class=\"actions-row\">";
    // Share is always available
    b << "<button class=\"ghost\" type=\"button\" onclick=\""
         "var u='" << html_escape("https://tools.lumaplayground.com/u/" + slug) << "';"
         "navigator.share?navigator.share({title:'Luma Tools',text:'" << html_escape(display) << " on Luma Tools',url:u}):"
         "navigator.clipboard.writeText(u).then(function(){this.innerHTML='<i class=\\'fas fa-check\\'></i> Copied'}.bind(this))"
         "\"><i class=\"fas fa-share-nodes\"></i> Share profile</button>";
    if (is_own) {
        if (is_pro && !target.stripe_customer_id.empty()) {
            b << "<button class=\"primary\" type=\"button\" onclick=\"openBillingPortal(this)\">"
                 "<i class=\"fas fa-credit-card\"></i> Manage subscription</button>";
        } else if (!is_pro) {
            b << "<button class=\"primary\" type=\"button\" onclick=\"startProCheckout(this)\">"
                 "<i class=\"fas fa-bolt\"></i> Upgrade to Pro</button>";
        }
        b << "<form method=\"POST\" action=\"/account/logout\" style=\"margin:0\">"
             "<button class=\"ghost\" type=\"submit\" style=\"width:100%\">"
             "<i class=\"fas fa-sign-out-alt\"></i> Sign out</button></form>";
    } else {
        b << "<a class=\"primary\" href=\"/\" style=\"min-width:160px\">"
             "<i class=\"fas fa-bolt\"></i> Try the tools</a>";
    }
    b << "</div></div>";

    // API keys section — only on own view.
    if (is_own) {
        b << "<div class=\"card card-wide\" style=\"max-width:760px;margin-top:18px\">"
             "<div style=\"display:flex;justify-content:space-between;align-items:flex-start;flex-wrap:wrap;gap:14px;margin-bottom:8px\">"
             "<div><h1 style=\"font-size:1.2rem\">API keys</h1>"
             "<p class=\"intro\" style=\"margin-bottom:0\">Programmatic access for scripts and the <code>luma</code> CLI. "
             "<a href=\"#\" onclick=\"document.getElementById('apiDocs').classList.toggle('hidden');return false\" "
             "style=\"color:var(--accent-light)\">View docs</a></p></div>";
        if (is_pro) {
            b << "<button class=\"primary\" style=\"max-width:200px\" onclick=\"createApiKey()\">"
                 "<i class=\"fas fa-plus\"></i> New key</button>";
        } else {
            b << "<span class=\"chip\" style=\"align-self:center\"><i class=\"fas fa-lock\"></i> Pro only</span>";
        }
        b << "</div>"
             "<div id=\"apiDocs\" class=\"hidden\" style=\"background:rgba(0,0,0,.25);border:1px solid var(--border);border-radius:10px;padding:14px 16px;margin:10px 0 14px;font-size:.85rem;color:var(--text-secondary)\">"
             "<div style=\"color:var(--text-primary);font-weight:600;margin-bottom:6px\">Authenticate any tool endpoint with your key:</div>"
             "<pre style=\"background:rgba(255,255,255,.04);padding:10px;border-radius:6px;font-family:monospace;font-size:.78rem;margin:0;white-space:pre-wrap\">"
             "curl https://tools.lumaplayground.com/api/tools/image-compress \\\n"
             "  -H \"Authorization: Bearer lt_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\" \\\n"
             "  -F \"file=@photo.jpg\" \\\n"
             "  -o compressed.jpg</pre>"
             "<div style=\"margin-top:8px\">Or with the CLI: <code>npx luma-tools-cli compress photo.jpg</code></div>"
             "</div>"
             "<div id=\"apiKeysList\" style=\"margin-top:8px\">Loading…</div>"
             "</div>";
    }

    if (!is_own) {
        // Public view: skip the inline JS (saves bytes + nothing to wire up).
        return render_account_shell(display + " — Luma Tools", b.str());
    }
    return render_account_shell("Your account", b.str() +
        "<script>" + R"JS(
async function loadApiKeys() {
    const list = document.getElementById('apiKeysList');
    if (!list) return;
    try {
        const r = await fetch('/api/account/api-keys', { credentials: 'same-origin' });
        if (!r.ok) { list.innerHTML = ''; return; }
        const data = await r.json();
        if (!data.keys || !data.keys.length) {
            list.innerHTML = '<div style="color:var(--text-secondary);font-size:.86rem;padding:10px 0">No API keys yet.</div>';
            return;
        }
        const fmt = ts => ts ? new Date(ts*1000).toLocaleDateString() : '—';
        list.innerHTML = data.keys.map(k => `
            <div style="display:flex;align-items:center;gap:12px;padding:10px 12px;background:rgba(255,255,255,.03);border:1px solid var(--border);border-radius:10px;margin-bottom:8px">
                <div style="flex:1;min-width:0">
                    <div style="font-weight:600;font-size:.9rem">${escapeHtml(k.name||'Untitled key')}</div>
                    <div style="font-family:monospace;font-size:.78rem;color:var(--text-secondary)">${escapeHtml(k.prefix)}… &middot; created ${fmt(k.created_ts)} &middot; last used ${k.last_used_ts?fmt(k.last_used_ts):'never'}</div>
                </div>
                <button onclick="revokeApiKey(${k.id})" style="padding:6px 12px;background:rgba(239,68,68,.12);border:1px solid rgba(239,68,68,.35);border-radius:6px;color:#fca5a5;font-size:.78rem;cursor:pointer">Revoke</button>
            </div>`).join('');
    } catch(e) { list.innerHTML = '<div style="color:#fca5a5">Failed to load keys.</div>'; }
}
function escapeHtml(s){return String(s||'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
async function createApiKey() {
    const name = prompt('Name for this key (e.g. "Laptop CLI", "n8n workflow"):');
    if (name === null) return;
    try {
        const r = await fetch('/api/account/api-keys', { method:'POST', headers:{'Content-Type':'application/json'}, credentials:'same-origin', body: JSON.stringify({name}) });
        const data = await r.json();
        if (!r.ok) { alert(data.error || 'Could not create key'); return; }
        // Show the key once, in a copyable prompt.
        prompt('Your new API key (save it now — you will NOT see it again):', data.key);
        loadApiKeys();
    } catch(e) { alert('Network error'); }
}
async function revokeApiKey(id) {
    if (!confirm('Revoke this key? Anything using it will stop working immediately.')) return;
    try {
        const r = await fetch('/api/account/api-keys/' + id + '/revoke', { method:'POST', credentials:'same-origin' });
        if (!r.ok) { alert('Could not revoke'); return; }
        loadApiKeys();
    } catch(e) { alert('Network error'); }
}
loadApiKeys();
)JS" + R"JS(
async function openBillingPortal(btn){btn.disabled=true;const orig=btn.innerHTML;btn.innerHTML='<i class="fas fa-circle-notch fa-spin"></i> Loading...';
try{const r=await fetch('/api/billing/portal-session',{method:'POST',credentials:'same-origin'});const d=await r.json();
if(r.ok&&d.url){window.location=d.url;return}alert(d.error||('Error '+r.status))}catch(e){alert('Network error')}finally{btn.disabled=false;btn.innerHTML=orig}}
async function startProCheckout(btn){btn.disabled=true;const orig=btn.innerHTML;btn.innerHTML='<i class="fas fa-circle-notch fa-spin"></i> Loading...';
try{const r=await fetch('/api/billing/checkout-session',{method:'POST',headers:{'Content-Type':'application/json'},credentials:'same-origin',body:JSON.stringify({plan:'pro'})});const d=await r.json();
if(r.ok&&d.url){window.location=d.url;return}alert(d.error||('Error '+r.status))}catch(e){alert('Network error')}finally{btn.disabled=false;btn.innerHTML=orig}}
)JS" + "</script>");
}

static string normalize_email(string email) {
    return lower_copy(trim_copy(email));
}

// Local helpers used by the OAuth handlers.
static string random_token_hex_inline(size_t bytes = 16) {
    static const char* hex = "0123456789abcdef";
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    std::uniform_int_distribution<int> dist(0, 255);
    string out;
    out.reserve(bytes * 2);
    for (size_t i = 0; i < bytes; ++i) {
        int b = dist(gen);
        out.push_back(hex[(b >> 4) & 0xF]);
        out.push_back(hex[b & 0xF]);
    }
    return out;
}

// Alias so the OAuth code below can call this without forward-declaration
// gymnastics: url_form_enc is defined later in the file.
static string url_form_enc(const string& s);
static string url_form_enc_inline(const string& s) { return url_form_enc(s); }

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

// ─── Transactional email via Resend (used for password reset) ───────────────
// Returns true on a 2xx response from Resend. Silently no-ops if RESEND_API_KEY
// isn't configured (callers can still treat the reset as initiated).
static bool send_email_via_resend(const string& to, const string& subject,
                                   const string& html_body, const string& text_body,
                                   string& error_out) {
    error_out.clear();
    string api_key = billing_env("RESEND_API_KEY");
    if (api_key.empty()) { error_out = "RESEND_API_KEY not configured."; return false; }
    string from = billing_env("RESEND_FROM",
        "Luma Tools <noreply@" +
        billing_env("APP_BASE_URL", "tools.lumaplayground.com")
            .substr(billing_env("APP_BASE_URL", "https://tools.lumaplayground.com").find("//") + 2) +
        ">");
    // Strip any trailing path from APP_BASE_URL when synthesizing the From.
    auto slash = from.find('/', from.find('@'));
    if (slash != string::npos) from = from.substr(0, slash) + ">";

    json payload = {
        {"from", from},
        {"to", json::array({ to })},
        {"subject", subject},
        {"html", html_body},
        {"text", text_body}
    };

    string proc = get_processing_dir();
    string id = generate_job_id();
    string body_file = proc + "/em_" + id + "_body.json";
    string resp_file = proc + "/em_" + id + "_resp.json";
    string hdr_file  = proc + "/em_" + id + "_hdr.txt";
    string code_file = proc + "/em_" + id + "_code.txt";
    { ofstream f(body_file); f << payload.dump(); }

    string cmd =
        "curl -s --max-time 12 -X POST "
        "-H " + escape_arg("Authorization: Bearer " + api_key) +
        " -H " + escape_arg("Content-Type: application/json") +
        " --data-binary @" + escape_arg(body_file) +
        " -o " + escape_arg(resp_file) +
        " -D " + escape_arg(hdr_file) +
        " -w \"%{http_code}\""
        " https://api.resend.com/emails > " + escape_arg(code_file);

    int rc;
    exec_command(cmd, rc);

    int status = 0;
    if (fs::exists(code_file)) {
        std::ifstream f(code_file);
        std::ostringstream ss; ss << f.rdbuf();
        try { status = std::stoi(trim_copy(ss.str())); } catch (...) {}
    }
    bool ok = (status >= 200 && status < 300);
    if (!ok) {
        // Grab the body so we can log what Resend complained about.
        string body;
        if (fs::exists(resp_file)) {
            std::ifstream f(resp_file);
            std::ostringstream ss; ss << f.rdbuf();
            body = ss.str();
        }
        cerr << "[Luma Tools] Resend send failed: HTTP " << status << "  body=" << body << endl;
        error_out = "Email provider returned HTTP " + to_string(status);
    }
    try { fs::remove(body_file); fs::remove(resp_file); fs::remove(hdr_file); fs::remove(code_file); } catch (...) {}
    return ok;
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
    if (code == "password_mismatch") return "Passwords don't match. Please re-type them.";
    if (code == "password_too_short") return "Password must be at least 8 characters long.";
    if (code == "reset_sent") return "If that email is registered, a reset link is on its way (check spam).";
    if (code == "reset_invalid") return "That reset link is invalid or has expired. Please request a new one.";
    if (code == "reset_success") return "Password updated. You can sign in now.";
    if (is_error && !code.empty()) return code;
    return "";
}

// ─── Public plan-resolution helpers (declared in routes.h) ──────────────────

// Resolve the current request to an AccountUser by either:
//   1. Authorization: Bearer lt_xxx  (programmatic / CLI / API key path)
//   2. lt_session cookie             (web browser path)
// Returns true on either match; user is populated.
static bool current_account_any(const httplib::Request& req, AccountUser& user) {
    string auth = req.get_header_value("Authorization");
    if (auth.rfind("Bearer ", 0) == 0) {
        string key = trim_copy(auth.substr(7));
        if (!key.empty() && account_find_user_by_api_key(key, user)) return true;
    }
    return current_account(req, user);
}

string account_plan_for_request(const httplib::Request& req) {
    AccountUser user;
    if (!current_account_any(req, user)) return "free";
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
    if (!current_account_any(req, user)) return 0;
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
        // Unified profile renderer — own view (all private fields + API keys card)
        AccountStats stats = account_get_user_stats(user.id);
        res.set_header("Cache-Control", "no-store");
        res.set_content(render_profile_page(user, /*is_own=*/true, stats, message, error),
                        "text/html");
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
        string password_confirm = form_value(req.body, "password_confirm");
        string display_name = trim_copy(form_value(req.body, "display_name"));

        if (email.empty() || email.find('@') == string::npos) {
            res.set_header("Location", "/account?err=invalid_email");
            res.status = 302;
            return;
        }
        if (password.empty() || password.length() < 8) {
            res.set_header("Location", "/account?err=password_too_short");
            res.status = 302;
            return;
        }
        if (!password_confirm.empty() && password != password_confirm) {
            res.set_header("Location", "/account/register?err=password_mismatch");
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

    // ── Forgot password ─────────────────────────────────────────────────────
    auto render_forgot_page = [](const string& message, const string& error) {
        std::ostringstream b;
        b << "<div class=\"card\">"
          << "<h1>Reset your password</h1>"
          << "<p class=\"intro\">Enter the email on your account. We'll send a one-click reset link that expires in 30 minutes.</p>"
          << render_message_block(message, error)
          << "<form method=\"POST\" action=\"/account/forgot\" autocomplete=\"on\">"
             "<div class=\"field\"><label for=\"f-email\">Email</label>"
             "<input id=\"f-email\" type=\"email\" name=\"email\" placeholder=\"you@example.com\" autocomplete=\"email\" required></div>"
             "<button class=\"primary\" type=\"submit\"><i class=\"fas fa-paper-plane\"></i> Send reset link</button>"
             "</form>"
          << "<div class=\"divider\">or</div>"
          << "<div class=\"alt-link\">Remembered it? <a href=\"/account/login\">Sign in</a></div>"
          << "</div>";
        return render_account_shell("Reset password", b.str());
    };

    svr.Get("/account/forgot", [render_forgot_page](const httplib::Request& req, httplib::Response& res) {
        string message = req.has_param("msg") ? req.get_param_value("msg") : "";
        string error   = req.has_param("err") ? req.get_param_value("err") : "";
        message = auth_feedback_text(message, false);
        error   = auth_feedback_text(error, true);
        res.set_content(render_forgot_page(message, error), "text/html");
    });

    svr.Post("/account/forgot", [](const httplib::Request& req, httplib::Response& res) {
        string email = normalize_email(form_value(req.body, "email"));
        // Always 302 with the same generic message so attackers can't enumerate emails.
        res.set_header("Location", "/account/forgot?msg=reset_sent");
        res.status = 302;

        if (email.empty() || email.find('@') == string::npos) return;
        AccountUser user;
        if (!account_get_user_by_email(email, user) || user.id <= 0) return;

        string token;
        if (!account_create_password_reset(user.id, token)) return;

        string base = billing_env("APP_BASE_URL", "http://localhost:8080");
        string reset_url = base + "/account/reset?token=" + token;
        string name = user.display_name.empty() ? user.email.substr(0, user.email.find('@')) : user.display_name;

        string text =
            "Hi " + name + ",\n\n"
            "Someone (hopefully you) asked to reset the password on your Luma Tools account.\n"
            "Click the link below to choose a new one — it works once and expires in 30 minutes:\n\n"
            + reset_url + "\n\n"
            "If you didn't request this, you can safely ignore this email — your password won't change.\n\n"
            "— Luma Tools";

        string html =
            "<div style=\"font-family:-apple-system,Segoe UI,Roboto,sans-serif;max-width:520px;margin:0 auto;color:#222\">"
            "<h2 style=\"color:#7c5cff\">Reset your Luma Tools password</h2>"
            "<p>Hi " + html_escape(name) + ",</p>"
            "<p>Someone (hopefully you) asked to reset the password on your Luma Tools account. Click the button below to choose a new one. The link works once and expires in <strong>30 minutes</strong>.</p>"
            "<p style=\"margin:24px 0\"><a href=\"" + html_escape(reset_url) + "\" "
            "style=\"display:inline-block;background:#7c5cff;color:#fff;padding:12px 24px;border-radius:10px;text-decoration:none;font-weight:700\">"
            "Choose a new password</a></p>"
            "<p style=\"color:#888;font-size:.85rem\">Or copy this link:<br><code style=\"word-break:break-all\">" + html_escape(reset_url) + "</code></p>"
            "<hr style=\"border:0;border-top:1px solid #eee;margin:24px 0\">"
            "<p style=\"color:#888;font-size:.85rem\">Didn't request this? You can safely ignore this email — your password won't change.</p>"
            "</div>";

        string err;
        send_email_via_resend(user.email, "Reset your Luma Tools password", html, text, err);
    });

    // ── Reset password (token from email) ───────────────────────────────────
    auto render_reset_page = [](const string& token, const string& message, const string& error) {
        std::ostringstream b;
        b << "<div class=\"card\">"
          << "<h1>Choose a new password</h1>"
          << "<p class=\"intro\">Pick something memorable. Your existing sessions will be signed out after the change.</p>"
          << render_message_block(message, error)
          << "<form method=\"POST\" action=\"/account/reset\" autocomplete=\"on\" onsubmit=\"return ltCheckMatchR(this)\">"
          << "<input type=\"hidden\" name=\"token\" value=\"" << html_escape(token) << "\">"
             "<div class=\"field\"><label for=\"rs-pw\">New password</label>"
          << pw_field("rs-pw", "password", "At least 8 characters", "new-password", true, 8)
          << "</div>"
             "<div class=\"field\"><label for=\"rs-pw2\">Confirm new password</label>"
          << pw_field("rs-pw2", "password_confirm", "Re-type your new password", "new-password", true, 8)
          << "<div id=\"pwMismatchR\" class=\"pw-mismatch\"><i class=\"fas fa-exclamation-circle\"></i> Passwords don't match</div>"
             "</div>"
             "<button class=\"primary\" type=\"submit\"><i class=\"fas fa-key\"></i> Update password</button>"
             "</form>"
          << "<script>function ltCheckMatchR(f){var a=f.password.value,b=f.password_confirm.value,w=document.getElementById('pwMismatchR');"
             "if(a!==b){w.classList.add('show');f.password_confirm.focus();return false}w.classList.remove('show');return true}"
             "document.getElementById('rs-pw2').addEventListener('input',function(){"
             "var a=document.getElementById('rs-pw').value,b=this.value,w=document.getElementById('pwMismatchR');"
             "if(b&&a!==b)w.classList.add('show');else w.classList.remove('show')});</script>"
          << "</div>";
        return render_account_shell("Reset password", b.str());
    };

    svr.Get("/account/reset", [render_reset_page](const httplib::Request& req, httplib::Response& res) {
        string token = req.has_param("token") ? req.get_param_value("token") : "";
        if (token.empty() || account_consume_password_reset(token, /*mark_used=*/false) == 0) {
            res.set_header("Location", "/account/forgot?err=reset_invalid");
            res.status = 302;
            return;
        }
        res.set_content(render_reset_page(token, "", ""), "text/html");
    });

    svr.Post("/account/reset", [](const httplib::Request& req, httplib::Response& res) {
        string token            = form_value(req.body, "token");
        string password         = form_value(req.body, "password");
        string password_confirm = form_value(req.body, "password_confirm");

        if (token.empty() || password.empty() || password.length() < 8) {
            res.set_header("Location", "/account/reset?token=" + token + "&err=password_too_short");
            res.status = 302;
            return;
        }
        if (!password_confirm.empty() && password != password_confirm) {
            res.set_header("Location", "/account/reset?token=" + token + "&err=password_mismatch");
            res.status = 302;
            return;
        }

        int user_id = account_consume_password_reset(token, /*mark_used=*/true);
        if (user_id == 0) {
            res.set_header("Location", "/account/forgot?err=reset_invalid");
            res.status = 302;
            return;
        }
        if (!account_update_password(user_id, password)) {
            res.set_header("Location", "/account/forgot?err=Could+not+update+password.");
            res.status = 302;
            return;
        }
        account_invalidate_all_sessions(user_id);
        res.set_header("Location", "/account/login?msg=reset_success");
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

    // ── API keys (Pro feature) ──────────────────────────────────────────────
    //    Issue, list, revoke. The plaintext key is returned ONCE on creation;
    //    afterwards only the prefix is stored for display ("lt_abc12345…").
    svr.Get("/api/account/api-keys", [](const httplib::Request& req, httplib::Response& res) {
        AccountUser user;
        if (!current_account(req, user)) {
            res.status = 401;
            res.set_content(R"({"error":"Not signed in"})", "application/json");
            return;
        }
        auto keys = account_api_key_list(user.id);
        json arr = json::array();
        for (const auto& k : keys) {
            arr.push_back({
                {"id", k.id}, {"name", k.name}, {"prefix", k.key_prefix},
                {"created_ts", k.created_ts}, {"last_used_ts", k.last_used_ts}
            });
        }
        res.set_header("Cache-Control", "no-store");
        res.set_content(json({{"keys", arr}}).dump(), "application/json");
    });

    svr.Post("/api/account/api-keys", [](const httplib::Request& req, httplib::Response& res) {
        AccountUser user;
        if (!current_account(req, user)) {
            res.status = 401;
            res.set_content(R"({"error":"Not signed in"})", "application/json");
            return;
        }
        // Pro-only feature.
        string plan = lower_copy(trim_copy(user.plan));
        string status = lower_copy(trim_copy(user.account_status));
        bool is_pro = (plan == "pro" || plan == "starter") &&
                      (status == "active" || status == "trialing" || status == "past_due");
        if (!is_pro) {
            res.status = 402;
            res.set_content(json({
                {"error", "API keys are a Pro feature. Upgrade at /account to unlock programmatic access."},
                {"plan_required", "pro"}
            }).dump(), "application/json");
            return;
        }
        json body;
        try { body = json::parse(req.body); } catch (...) {}
        string name = trim_copy(body.value("name", string("")));
        if (name.empty()) name = "Untitled key";
        if (name.size() > 80) name = name.substr(0, 80);

        // Cap at 10 active keys per user to discourage sprawl.
        if ((int)account_api_key_list(user.id).size() >= 10) {
            res.status = 400;
            res.set_content(R"({"error":"You already have 10 active API keys. Revoke one before creating another."})", "application/json");
            return;
        }

        string plaintext;
        int key_id = 0;
        if (!account_api_key_create(user.id, name, plaintext, key_id)) {
            res.status = 500;
            res.set_content(R"({"error":"Could not create API key."})", "application/json");
            return;
        }
        res.set_header("Cache-Control", "no-store");
        res.set_content(json({
            {"ok", true},
            {"id", key_id},
            {"name", name},
            {"key", plaintext},
            {"warning", "Save this key now — it will not be shown again."}
        }).dump(), "application/json");
    });

    svr.Post(R"(/api/account/api-keys/(\d+)/revoke)", [](const httplib::Request& req, httplib::Response& res) {
        AccountUser user;
        if (!current_account(req, user)) {
            res.status = 401;
            res.set_content(R"({"error":"Not signed in"})", "application/json");
            return;
        }
        int key_id = std::atoi(req.matches[1].str().c_str());
        bool ok = account_api_key_revoke(user.id, key_id);
        if (!ok) {
            res.status = 404;
            res.set_content(R"({"error":"Key not found or already revoked"})", "application/json");
            return;
        }
        res.set_content(R"({"ok":true})", "application/json");
    });

    // ── Public profile pages /u/:display_name ──────────────────────────────
    //    Read-only, no auth. Shows counters only (tools used + downloads).
    svr.Get(R"(/u/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        string name = url_decode(req.matches[1].str());
        AccountUser target;
        bool found = !name.empty() && account_get_user_by_display_name(name, target);
        if (!found) {
            res.status = 404;
            std::ostringstream b;
            b << "<div class=\"card\" style=\"text-align:center\">"
              << "<h1>Profile not found</h1>"
              << "<p class=\"intro\">No public profile for <code>" << html_escape(name) << "</code>.</p>"
              << "<a class=\"primary\" href=\"/\" style=\"max-width:200px;margin:0 auto\">Back to tools</a>"
              << "</div>";
            res.set_content(render_account_shell("Profile not found", b.str()), "text/html");
            return;
        }
        // If the visitor IS the target user, show the full dashboard (= /account view).
        AccountUser viewer;
        bool viewer_signed_in = current_account(req, viewer);
        bool is_own = viewer_signed_in && viewer.id == target.id;
        AccountStats stats = account_get_user_stats(target.id);
        res.set_header("Cache-Control", "no-store");
        res.set_content(render_profile_page(target, is_own, stats, "", ""), "text/html");
    });

    // ── Shared OAuth helpers (used by Discord + Google) ─────────────────────
    // After we have a verified email + provider-specific user id, this code
    // upserts the user, links the oauth identity, creates a session, and
    // redirects to /account.
    auto oauth_finish_login = [](const httplib::Request& req, httplib::Response& res,
                                  const string& provider,
                                  const string& provider_user_id,
                                  const string& email,
                                  const string& display_name) {
        AccountUser user;
        // Prefer the oauth-linked row (same provider account used before),
        // otherwise look up by email so we merge with any password-based
        // account on the same address.
        if (!account_find_user_by_oauth(provider, provider_user_id, user) &&
            !account_get_user_by_email(email, user)) {
            account_upsert_user(email, display_name, /*password=*/"", user);
        }
        if (user.id <= 0) {
            res.set_header("Location", "/account/login?err=Could+not+create+account.");
            res.status = 302;
            return;
        }
        account_link_oauth_identity(provider, provider_user_id, user.id);

        string token; int64_t expires_ts = 0;
        if (!account_create_session(user.id, req.remote_addr,
                                     req.get_header_value("User-Agent"),
                                     token, expires_ts)) {
            res.status = 500;
            res.set_content("Could not create session", "text/plain");
            return;
        }
        int max_age = (int)std::max<int64_t>(0, expires_ts - std::time(nullptr));
        res.set_header("Set-Cookie",
            session_cookie_name() + "=" + token +
            "; Path=/; HttpOnly; SameSite=Lax; Max-Age=" + to_string(max_age));
        res.set_header("Location", "/account?msg=signed_in");
        res.status = 302;
    };

    // ── Discord OAuth ───────────────────────────────────────────────────────
    // Free signup option: redirects to Discord, comes back with the user's
    // Discord id + email. We upsert the user, link the oauth_identities row,
    // and create a session cookie just like a normal login.
    auto disc_cid = []() { return billing_env("DISCORD_OAUTH_CLIENT_ID"); };
    auto disc_sec = []() { return billing_env("DISCORD_OAUTH_CLIENT_SECRET"); };
    auto disc_redirect = []() {
        return billing_env("APP_BASE_URL", "http://localhost:8080") + "/account/oauth/discord/callback";
    };

    svr.Get("/account/oauth/discord", [disc_cid, disc_redirect](const httplib::Request&, httplib::Response& res) {
        string cid = disc_cid();
        if (cid.empty()) {
            res.set_header("Location", "/account/login?err=Discord+sign-in+is+not+configured+yet.");
            res.status = 302;
            return;
        }
        // CSRF state: tiny random hex stored in a short-lived cookie.
        string state = random_token_hex_inline();
        res.set_header("Set-Cookie", "lt_oauth_state=" + state + "; Path=/; HttpOnly; SameSite=Lax; Max-Age=600");
        string url = "https://discord.com/oauth2/authorize"
                     "?response_type=code"
                     "&client_id=" + url_form_enc_inline(cid) +
                     "&scope=identify%20email"
                     "&redirect_uri=" + url_form_enc_inline(disc_redirect()) +
                     "&state=" + state;
        res.set_header("Location", url);
        res.status = 302;
    });

    svr.Get("/account/oauth/discord/callback", [disc_cid, disc_sec, disc_redirect, oauth_finish_login](const httplib::Request& req, httplib::Response& res) {
        string code  = req.has_param("code")  ? req.get_param_value("code")  : "";
        string state = req.has_param("state") ? req.get_param_value("state") : "";
        string cookie_state = cookie_value(req, "lt_oauth_state");
        if (code.empty() || state.empty() || cookie_state.empty() || state != cookie_state) {
            res.set_header("Location", "/account/login?err=Discord+sign-in+failed+(state+mismatch).");
            res.status = 302;
            return;
        }
        string cid = disc_cid(), sec = disc_sec();
        if (cid.empty() || sec.empty()) {
            res.set_header("Location", "/account/login?err=Discord+sign-in+is+not+configured.");
            res.status = 302;
            return;
        }

        // Exchange code → access_token
        string proc = get_processing_dir();
        string id   = generate_job_id();
        string tok_file = proc + "/oa_" + id + "_tok.json";
        string body =
            "grant_type=authorization_code"
            "&code=" + url_form_enc_inline(code) +
            "&redirect_uri=" + url_form_enc_inline(disc_redirect());
        string body_path = proc + "/oa_" + id + "_body.txt";
        { ofstream f(body_path); f << body; }
        string cmd =
            "curl -s --max-time 12 -X POST "
            "-u " + escape_arg(cid + ":" + sec) +
            " -H \"Content-Type: application/x-www-form-urlencoded\""
            " --data @" + escape_arg(body_path) +
            " -o " + escape_arg(tok_file) +
            " https://discord.com/api/oauth2/token";
        int rc;
        exec_command(cmd, rc);
        string access_token;
        try {
            std::ifstream f(tok_file);
            std::ostringstream ss; ss << f.rdbuf();
            auto j = json::parse(ss.str());
            access_token = j.value("access_token", "");
        } catch (...) {}
        try { fs::remove(body_path); fs::remove(tok_file); } catch (...) {}
        if (access_token.empty()) {
            res.set_header("Location", "/account/login?err=Discord+sign-in+failed+(token+exchange).");
            res.status = 302;
            return;
        }

        // Fetch user
        string me_file = proc + "/oa_" + id + "_me.json";
        string mecmd =
            "curl -s --max-time 8 "
            "-H \"Authorization: Bearer " + access_token + "\""
            " -o " + escape_arg(me_file) +
            " https://discord.com/api/users/@me";
        exec_command(mecmd, rc);
        string discord_id, discord_email, discord_username;
        bool email_verified = false;
        try {
            std::ifstream f(me_file);
            std::ostringstream ss; ss << f.rdbuf();
            auto j = json::parse(ss.str());
            discord_id       = j.value("id", "");
            discord_email    = lower_copy(trim_copy(j.value("email", "")));
            discord_username = j.value("global_name", j.value("username", ""));
            email_verified   = j.value("verified", false);
        } catch (...) {}
        try { fs::remove(me_file); } catch (...) {}
        if (discord_id.empty() || discord_email.empty() || !email_verified) {
            res.set_header("Location", "/account/login?err=Discord+account+has+no+verified+email.");
            res.status = 302;
            return;
        }

        oauth_finish_login(req, res, "discord", discord_id, discord_email, discord_username);
    });

    // ── Google OAuth ────────────────────────────────────────────────────────
    auto goog_cid = []() { return billing_env("GOOGLE_OAUTH_CLIENT_ID"); };
    auto goog_sec = []() { return billing_env("GOOGLE_OAUTH_CLIENT_SECRET"); };
    auto goog_redirect = []() {
        return billing_env("APP_BASE_URL", "http://localhost:8080") + "/account/oauth/google/callback";
    };

    svr.Get("/account/oauth/google", [goog_cid, goog_redirect](const httplib::Request&, httplib::Response& res) {
        string cid = goog_cid();
        if (cid.empty()) {
            res.set_header("Location", "/account/login?err=Google+sign-in+is+not+configured+yet.");
            res.status = 302;
            return;
        }
        string state = random_token_hex_inline();
        res.set_header("Set-Cookie", "lt_oauth_state=" + state + "; Path=/; HttpOnly; SameSite=Lax; Max-Age=600");
        string url = "https://accounts.google.com/o/oauth2/v2/auth"
                     "?response_type=code"
                     "&client_id=" + url_form_enc_inline(cid) +
                     "&scope=openid%20email%20profile"
                     "&redirect_uri=" + url_form_enc_inline(goog_redirect()) +
                     "&access_type=online"
                     "&prompt=select_account"
                     "&state=" + state;
        res.set_header("Location", url);
        res.status = 302;
    });

    svr.Get("/account/oauth/google/callback",
            [goog_cid, goog_sec, goog_redirect, oauth_finish_login]
            (const httplib::Request& req, httplib::Response& res) {
        string code  = req.has_param("code")  ? req.get_param_value("code")  : "";
        string state = req.has_param("state") ? req.get_param_value("state") : "";
        string cookie_state = cookie_value(req, "lt_oauth_state");
        if (code.empty() || state.empty() || cookie_state.empty() || state != cookie_state) {
            res.set_header("Location", "/account/login?err=Google+sign-in+failed+(state+mismatch).");
            res.status = 302;
            return;
        }
        string cid = goog_cid(), sec = goog_sec();
        if (cid.empty() || sec.empty()) {
            res.set_header("Location", "/account/login?err=Google+sign-in+is+not+configured.");
            res.status = 302;
            return;
        }

        // Exchange code → access_token + id_token
        string proc = get_processing_dir();
        string id   = generate_job_id();
        string tok_file  = proc + "/oa_" + id + "_gtok.json";
        string body =
            "code="          + url_form_enc_inline(code) +
            "&client_id="    + url_form_enc_inline(cid) +
            "&client_secret=" + url_form_enc_inline(sec) +
            "&redirect_uri=" + url_form_enc_inline(goog_redirect()) +
            "&grant_type=authorization_code";
        string body_path = proc + "/oa_" + id + "_gbody.txt";
        { ofstream f(body_path); f << body; }
        string cmd =
            "curl -s --max-time 12 -X POST "
            "-H \"Content-Type: application/x-www-form-urlencoded\""
            " --data @" + escape_arg(body_path) +
            " -o " + escape_arg(tok_file) +
            " https://oauth2.googleapis.com/token";
        int rc; exec_command(cmd, rc);
        string access_token;
        try {
            std::ifstream f(tok_file);
            std::ostringstream ss; ss << f.rdbuf();
            auto j = json::parse(ss.str());
            access_token = j.value("access_token", "");
        } catch (...) {}
        try { fs::remove(body_path); fs::remove(tok_file); } catch (...) {}
        if (access_token.empty()) {
            res.set_header("Location", "/account/login?err=Google+sign-in+failed+(token+exchange).");
            res.status = 302;
            return;
        }

        // Fetch userinfo (OIDC userinfo endpoint).
        string me_file = proc + "/oa_" + id + "_gme.json";
        string mecmd =
            "curl -s --max-time 8 "
            "-H \"Authorization: Bearer " + access_token + "\""
            " -o " + escape_arg(me_file) +
            " https://www.googleapis.com/oauth2/v3/userinfo";
        exec_command(mecmd, rc);
        string g_sub, g_email, g_name;
        bool g_email_verified = false;
        try {
            std::ifstream f(me_file);
            std::ostringstream ss; ss << f.rdbuf();
            auto j = json::parse(ss.str());
            g_sub            = j.value("sub", "");
            g_email          = lower_copy(trim_copy(j.value("email", "")));
            g_name           = j.value("name", j.value("given_name", ""));
            g_email_verified = j.value("email_verified", false);
        } catch (...) {}
        try { fs::remove(me_file); } catch (...) {}
        if (g_sub.empty() || g_email.empty() || !g_email_verified) {
            res.set_header("Location", "/account/login?err=Google+account+has+no+verified+email.");
            res.status = 302;
            return;
        }

        oauth_finish_login(req, res, "google", g_sub, g_email, g_name);
    });
}
