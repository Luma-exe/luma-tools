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

static string render_account_page(const AccountUser* user, const string& message = "", const string& error = "") {
    std::ostringstream html;
    html << R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Luma Tools - Account</title>
<style>
body{margin:0;min-height:100vh;background:#0b0b12;color:#e7e7ee;font-family:Segoe UI,Arial,sans-serif;display:grid;place-items:center;padding:24px}
.card{width:min(760px,100%);background:rgba(255,255,255,.04);border:1px solid rgba(255,255,255,.09);border-radius:20px;padding:28px}
.hero{display:flex;justify-content:space-between;gap:16px;align-items:flex-start;flex-wrap:wrap}
h1{margin:0 0 6px;font-size:2rem}
p{margin:0;color:#a7a7b5;line-height:1.5}
.grid{display:grid;grid-template-columns:1fr 1fr .8fr;gap:18px;margin-top:22px}
.panel{background:rgba(255,255,255,.03);border:1px solid rgba(255,255,255,.07);border-radius:16px;padding:20px}
label{display:block;font-size:.82rem;text-transform:uppercase;letter-spacing:.08em;color:#9c9caf;margin:0 0 8px}
input{width:100%;box-sizing:border-box;padding:12px 14px;border-radius:10px;border:1px solid rgba(255,255,255,.12);background:rgba(255,255,255,.04);color:#fff;margin-bottom:12px}
button,a.btn{display:inline-flex;align-items:center;justify-content:center;padding:11px 14px;border-radius:10px;border:0;background:#7c5cff;color:#fff;text-decoration:none;font-weight:700;cursor:pointer}
.muted{color:#9ca3af;font-size:.9rem;margin:0 0 8px}
.chip{display:inline-flex;align-items:center;padding:6px 10px;border-radius:999px;background:rgba(124,92,255,.14);color:#d8d2ff;font-size:.8rem;margin-right:6px;margin-bottom:6px}
.error{color:#fca5a5;margin-top:10px}
.ok{color:#86efac;margin-top:10px}
@media (max-width: 1000px){.grid{grid-template-columns:1fr}}
</style>
</head>
<body>
<div class="card">
  <div class="hero">
    <div>
      <h1>Account</h1>
      <p>Create a local account or sign in with email and password.</p>
    </div>
    <div>
      <span class="chip">Plan: )HTML";
    html << html_escape(user ? user->plan : "free");
    html << R"HTML(</span>
      <span class="chip">Status: )HTML";
    html << html_escape(user ? user->account_status : "signed out");
    html << R"HTML(</span>
    </div>
  </div>

  <div class="grid">
    <div class="panel">
      <label>Create Account</label>
      <form method="POST" action="/account/register">
        <input type="email" name="email" placeholder="you@example.com" required>
        <input type="password" name="password" placeholder="Password" required>
        <input type="text" name="display_name" placeholder="Display name (optional)">
        <button type="submit">Register</button>
      </form>
)HTML";
    if (!message.empty()) {
        html << "      <div class=\"ok\">" << html_escape(message) << "</div>\n";
    }
    if (!error.empty()) {
        html << "      <div class=\"error\">" << html_escape(error) << "</div>\n";
    }
    html << R"HTML(
    </div>

    <div class="panel">
      <label>Sign In</label>
      <form method="POST" action="/account/login">
        <input type="email" name="email" placeholder="you@example.com" required>
        <input type="password" name="password" placeholder="Password" required>
        <button type="submit">Sign In</button>
      </form>
    </div>

    <div class="panel">
      <label>Current Session</label>
      <p class="muted">Email: )HTML";
    html << html_escape(user ? user->email : "Not signed in");
    html << R"HTML(</p>
      <p class="muted">Name: )HTML";
    html << html_escape(user ? user->display_name : "-");
    html << R"HTML(</p>
      <p class="muted">Plan: )HTML";
    html << html_escape(user ? user->plan : "free");
    html << R"HTML(</p>
      <div style="display:flex;gap:10px;flex-wrap:wrap;margin-top:14px">
        <a class="btn" href="/">Back to tools</a>
        <form method="POST" action="/account/logout" style="margin:0">
          <button type="submit" style="background:#2b2b3f">Sign out</button>
        </form>
      </div>
    </div>
  </div>
</div>
</body>
</html>)HTML";
    return html.str();
}

static string normalize_email(string email) {
    return lower_copy(trim_copy(email));
}

static string stripe_plan_from_request(const string& plan_raw) {
    string plan = lower_copy(trim_copy(plan_raw));
    if (plan == "starter" || plan == "pro") return plan;
    return "pro";
}

void register_account_routes(httplib::Server& svr) {
    svr.Get("/account", [](const httplib::Request& req, httplib::Response& res) {
        AccountUser user;
        bool has_user = current_account(req, user);
        string message = req.has_param("msg") ? req.get_param_value("msg") : "";
        string error = req.has_param("err") ? req.get_param_value("err") : "";
        res.set_content(render_account_page(has_user ? &user : nullptr, message, error), "text/html");
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
            res.status = 500;
            res.set_content(R"({"error":"Could not create account."})", "application/json");
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
        res.set_header("Location", "/account?msg=registered_and_signed_in");
        res.status = 302;
    });

    svr.Post("/account/login", [](const httplib::Request& req, httplib::Response& res) {
        string email = normalize_email(form_value(req.body, "email"));
        string password = form_value(req.body, "password");
        
        if (email.empty() || email.find('@') == string::npos) {
            res.set_header("Location", "/account?err=invalid_credentials");
            res.status = 302;
            return;
        }
        if (password.empty()) {
            res.set_header("Location", "/account?err=invalid_credentials");
            res.status = 302;
            return;
        }

        AccountUser user;
        if (!account_verify_password(email, password, user)) {
            res.set_header("Location", "/account?err=invalid_credentials");
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

        json payload = {
            {"ok", false},
            {"plan", plan},
            {"user_id", user.id},
            {"checkout_ready", false},
            {"message", "Stripe checkout session creation will be wired in the next billing slice."},
            {"price_id", price_id},
            {"account_url", billing_env("APP_BASE_URL", "http://localhost:8080") + "/account"}
        };
        res.status = 501;
        res.set_content(payload.dump(), "application/json");
    });

    svr.Post("/api/billing/webhook", [](const httplib::Request& req, httplib::Response& res) {
        json event;
        try { event = json::parse(req.body); } catch (...) {
            res.status = 400;
            res.set_content(R"({"error":"Invalid JSON webhook payload."})", "application/json");
            return;
        }

        string type = event.value("type", "");
        json obj = event.contains("data") && event["data"].contains("object") ? event["data"]["object"] : json{};
        string customer_id = obj.value("customer", "");
        string subscription_id = obj.value("id", "");
        string status = obj.value("status", "inactive");
        string price_id;
        if (obj.contains("items") && obj["items"].contains("data") && obj["items"]["data"].is_array() && !obj["items"]["data"].empty()) {
            auto first_item = obj["items"]["data"][0];
            if (first_item.contains("price")) price_id = first_item["price"].value("id", "");
        }

        string plan = "free";
        if (!price_id.empty()) {
            if (price_id == billing_env("STRIPE_PRICE_STARTER")) plan = "starter";
            else if (price_id == billing_env("STRIPE_PRICE_PRO")) plan = "pro";
        }

        AccountUser user;
        bool have_user = false;
        if (!subscription_id.empty()) have_user = account_find_user_by_stripe_subscription_id(subscription_id, user);
        if (!have_user && !customer_id.empty()) have_user = account_find_user_by_stripe_customer_id(customer_id, user);

        if (have_user) {
            int64_t current_period_end = obj.value("current_period_end", (int64_t)0);
            if (type == "customer.subscription.deleted" || status == "canceled" || status == "incomplete_expired") {
                status = "canceled";
                plan = "free";
            }

            account_upsert_subscription(
                user.id,
                plan,
                status,
                customer_id,
                subscription_id,
                price_id,
                current_period_end,
                event.dump()
            );
        }

        res.set_content(R"({"ok":true})", "application/json");
    });
}
