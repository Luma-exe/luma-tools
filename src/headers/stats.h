#pragma once
/**
 * Luma Tools — Statistics tracking
 *
 * Logs every tool use and download to a JSON-lines file on disk.
 * Provides query functions for the dashboard and daily Discord digest.
 *
 * Storage: one JSON object per line in get_processing_dir()/../stats.jsonl
 * Each record:
 *   { "ts": <unix_seconds>, "kind": "tool"|"download", "name": "...", "ok": true|false }
 */

#include "common.h"

// ─── Record a stat event ─────────────────────────────────────────────────────

// Call after a tool completes (ok = true) or fails (ok = false).
void stat_record(const string& kind, const string& name, bool ok = true);

// ─── Query ───────────────────────────────────────────────────────────────────

struct StatSummary {
    int  total     = 0;
    int  successes = 0;
    int  failures  = 0;
    // Per-name counts sorted descending
    vector<pair<string, int>> by_name;
};

// Returns summary for events whose timestamp falls within [from_unix, to_unix].
// Pass 0 for from_unix to mean "beginning of time".
// Pass INT64_MAX for to_unix to mean "now".
StatSummary stat_query(int64_t from_unix, int64_t to_unix, const string& kind = "");

// Helper: unix timestamp for start of today (UTC midnight)
int64_t stat_today_start();

// Helper: unix timestamp for N days ago (UTC midnight of that day)
int64_t stat_days_ago(int n);

// ─── Daily digest ─────────────────────────────────────────────────────────────

// Fires the daily Discord embed. Called by the scheduler in main.cpp.
void stat_send_daily_digest();

// ─── Schedule ────────────────────────────────────────────────────────────────

// Starts a background thread that calls stat_send_daily_digest() at UTC midnight
// every day. Call once from main().
void stat_start_daily_scheduler();
