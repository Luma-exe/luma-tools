#!/usr/bin/env node
// ─────────────────────────────────────────────────────────────────────────────
//  luma — command-line client for Luma Tools (https://tools.lumaplayground.com)
//
//  Usage:
//    luma <tool> <file...> [--out path] [--format mp3] [--quality 720p] ...
//    luma login                       (prompts for an API key, stores in ~/.luma)
//    luma whoami
//    luma list                        (list available tools)
//
//  Auth: reads $LUMA_API_KEY, else ~/.luma/key. Generate one at
//        https://tools.lumaplayground.com/account → API keys (Pro required).
//
//  Single-file, zero npm deps (uses built-in fetch + FormData + Blob from
//  Node 18+). Total ~250 lines so it's easy to audit before piping to bash.
// ─────────────────────────────────────────────────────────────────────────────
'use strict';
const fs   = require('node:fs');
const path = require('node:path');
const os   = require('node:os');
const readline = require('node:readline');

const BASE = process.env.LUMA_BASE || 'https://tools.lumaplayground.com';
const KEY_FILE = path.join(os.homedir(), '.luma', 'key');

function readKey() {
    if (process.env.LUMA_API_KEY) return process.env.LUMA_API_KEY.trim();
    try { return fs.readFileSync(KEY_FILE, 'utf8').trim(); } catch { return null; }
}
function saveKey(k) {
    fs.mkdirSync(path.dirname(KEY_FILE), { recursive: true });
    fs.writeFileSync(KEY_FILE, k.trim() + '\n', { mode: 0o600 });
}
function die(msg, code = 1) { console.error('luma: ' + msg); process.exit(code); }

async function prompt(q) {
    const rl = readline.createInterface({ input: process.stdin, output: process.stdout, terminal: true });
    return new Promise(r => rl.question(q, ans => { rl.close(); r(ans); }));
}

async function cmdLogin() {
    const k = (await prompt('Paste your API key (starts with lt_): ')).trim();
    if (!k.startsWith('lt_')) die('Invalid key format. Expected lt_xxx');
    saveKey(k);
    console.log('Saved to ' + KEY_FILE);
    await cmdWhoami();
}

async function cmdWhoami() {
    const key = readKey();
    if (!key) die('No API key. Run: luma login');
    const r = await fetch(BASE + '/api/account/quota', {
        headers: { 'Authorization': 'Bearer ' + key }
    });
    if (!r.ok) die('Auth failed (HTTP ' + r.status + '). Key revoked? Run: luma login');
    const d = await r.json();
    console.log('Plan: ' + d.plan + (d.ai && d.ai.unlimited ? ' (unlimited AI)' : ''));
    console.log('Upload cap: ' + d.upload.max_mb + ' MB');
    console.log('AI remaining: ' + (d.ai.unlimited ? '∞' : (d.ai.remaining + '/' + d.ai.quota)));
}

function parseArgs(argv) {
    const out = { _: [], flags: {} };
    for (let i = 0; i < argv.length; i++) {
        const a = argv[i];
        if (a.startsWith('--')) {
            const eq = a.indexOf('=');
            if (eq >= 0) { out.flags[a.slice(2, eq)] = a.slice(eq + 1); }
            else if (i + 1 < argv.length && !argv[i + 1].startsWith('--')) {
                out.flags[a.slice(2)] = argv[++i];
            } else { out.flags[a.slice(2)] = true; }
        } else { out._.push(a); }
    }
    return out;
}

const TOOL_DEFAULTS = {
    'compress':            { endpoint: 'image-compress',  field: 'file' },
    'image-compress':      { endpoint: 'image-compress',  field: 'file' },
    'image-resize':        { endpoint: 'image-resize',    field: 'file' },
    'image-convert':       { endpoint: 'image-convert',   field: 'file' },
    'video-compress':      { endpoint: 'video-compress',  field: 'file' },
    'video-convert':       { endpoint: 'video-convert',   field: 'file' },
    'video-trim':          { endpoint: 'video-trim',      field: 'file' },
    'video-to-gif':        { endpoint: 'video-to-gif',    field: 'file' },
    'audio-convert':       { endpoint: 'audio-convert',   field: 'file' },
    'audio-normalize':     { endpoint: 'audio-normalize', field: 'file' },
    'pdf-compress':        { endpoint: 'pdf-compress',    field: 'file' },
    'pdf-merge':           { endpoint: 'pdf-merge',       field: 'files' },
    'pdf-split':           { endpoint: 'pdf-split',       field: 'file' },
    'pdf-to-images':       { endpoint: 'pdf-to-images',   field: 'file' },
    'ocr':                 { endpoint: 'ocr',             field: 'file' },
    'hash':                { endpoint: 'hash-generate',   field: 'file' },
};

async function cmdRun(tool, args) {
    const def = TOOL_DEFAULTS[tool];
    if (!def) die('Unknown tool "' + tool + '". Run: luma list');
    if (!args._.length) die('No input file. Example: luma ' + tool + ' input.jpg');

    const key = readKey();
    if (!key) die('No API key. Run: luma login');

    const fd = new FormData();
    for (const f of args._) {
        if (!fs.existsSync(f)) die('File not found: ' + f);
        const buf = fs.readFileSync(f);
        fd.append(def.field, new Blob([buf]), path.basename(f));
    }
    // Pass any other flags through as form fields (server reads them where applicable).
    for (const [k, v] of Object.entries(args.flags)) {
        if (k === 'out') continue;
        fd.append(k, String(v));
    }

    const url = BASE + '/api/tools/' + def.endpoint;
    process.stderr.write('POST ' + url + ' ... ');
    const t0 = Date.now();
    const r = await fetch(url, {
        method: 'POST',
        headers: { 'Authorization': 'Bearer ' + key },
        body: fd
    });
    process.stderr.write('HTTP ' + r.status + ' (' + (Date.now() - t0) + 'ms)\n');

    const ct = r.headers.get('content-type') || '';
    if (!r.ok) {
        if (ct.includes('application/json')) {
            const j = await r.json();
            die((j.error || 'Request failed') + (j.plan_required ? ' (' + j.plan_required + ' required)' : ''));
        } else {
            die('HTTP ' + r.status + ': ' + (await r.text()).slice(0, 200));
        }
    }

    // If output is binary (file), save it. If JSON, print it.
    if (ct.includes('application/json')) {
        const j = await r.json();
        console.log(JSON.stringify(j, null, 2));
        return;
    }
    const outPath = args.flags.out || defaultOut(args._[0], r.headers.get('content-disposition'));
    const ab = await r.arrayBuffer();
    fs.writeFileSync(outPath, Buffer.from(ab));
    console.log('Saved → ' + outPath + ' (' + (ab.byteLength / 1024).toFixed(1) + ' KB)');
}

function defaultOut(inputPath, contentDisp) {
    // Try to use server-suggested filename from Content-Disposition
    if (contentDisp) {
        const m = /filename="?([^";]+)"?/.exec(contentDisp);
        if (m) return path.join(process.cwd(), m[1]);
    }
    const base = path.basename(inputPath, path.extname(inputPath));
    return path.join(process.cwd(), base + '.out' + path.extname(inputPath));
}

function cmdList() {
    console.log('Available tools (more on https://tools.lumaplayground.com):');
    for (const t of Object.keys(TOOL_DEFAULTS).sort()) {
        const def = TOOL_DEFAULTS[t];
        console.log('  ' + t.padEnd(20) + ' → /api/tools/' + def.endpoint);
    }
    console.log('\nUsage:');
    console.log('  luma <tool> <file> [--out PATH] [--key=val ...]');
}

function cmdHelp() {
    console.log(`luma — CLI for Luma Tools

Commands:
  login                    Store an API key in ~/.luma/key
  whoami                   Show current plan + remaining quota
  list                     List supported tools
  <tool> <file> [opts]     Run a tool on a file (saves output by default)

Examples:
  luma login
  luma whoami
  luma image-compress photo.jpg --out photo-small.jpg
  luma video-to-gif clip.mp4 --out clip.gif
  luma pdf-merge a.pdf b.pdf c.pdf --out merged.pdf

Env:
  LUMA_API_KEY    overrides ~/.luma/key
  LUMA_BASE       overrides https://tools.lumaplayground.com

Get an API key: https://tools.lumaplayground.com/account (Pro required)`);
}

async function main() {
    const argv = process.argv.slice(2);
    if (!argv.length || argv[0] === '-h' || argv[0] === '--help') return cmdHelp();
    const cmd = argv[0];
    const rest = parseArgs(argv.slice(1));
    try {
        switch (cmd) {
            case 'login':   return await cmdLogin();
            case 'whoami':  return await cmdWhoami();
            case 'list':    return cmdList();
            case 'help':    return cmdHelp();
            default:        return await cmdRun(cmd, rest);
        }
    } catch (e) {
        die(e && e.message || String(e));
    }
}
main();
