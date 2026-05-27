// ════════════════════════════════════════════════════════════════════════
// LUMA TOOLS — v2 ToolPage renderer
//
// Builds the design's pixel-perfect tool page from a declarative spec
// (see tool-specs.js). When a spec exists for a tool, this renderer
// takes over and the legacy tool-panel is hidden. When no spec exists
// (custom tools like crop, redact, frame scrubber, screenshot annotator,
// resume builder, invoice gen), we fall through to the legacy panel.
// ════════════════════════════════════════════════════════════════════════
(function () {
  const $ = (s, r) => (r || document).querySelector(s);

  const SPECS = window.LUMA_TOOL_SPECS || {};
  const STATE = {}; // per-tool live state for in-flight forms

  // ── DOM builder helpers ───────────────────────────────────────────────
  function el(tag, attrs, kids) {
    const e = document.createElement(tag);
    if (attrs) for (const k in attrs) {
      if (k === 'class') e.className = attrs[k];
      else if (k === 'style') Object.assign(e.style, attrs[k]);
      else if (k.startsWith('on') && typeof attrs[k] === 'function') e.addEventListener(k.slice(2).toLowerCase(), attrs[k]);
      else if (k === 'html') e.innerHTML = attrs[k];
      else e.setAttribute(k, attrs[k]);
    }
    if (kids) (Array.isArray(kids) ? kids : [kids]).forEach(k => {
      if (k == null) return;
      e.appendChild(typeof k === 'string' ? document.createTextNode(k) : k);
    });
    return e;
  }
  function fa(name, extra) {
    return `<i class="fas ${name}${extra ? ' '+extra : ''}"></i>`;
  }

  // ── Intake: file drop zone / text / url / textarea ────────────────────
  function buildIntake(spec, state) {
    if (!spec.intake) return null;
    const wrap = el('div', { class: 'tpv-intake' });
    const i = spec.intake;
    if (i.kind === 'file' || i.kind === 'fileOrText') {
      const id = 'tpv-f-' + Math.random().toString(36).slice(2,8);
      const drop = el('div', { class: 'drop' });
      drop.innerHTML = `
        <div class="drop-ic">${fa('fa-cloud-upload-alt', 'fa-2x')}</div>
        <div class="drop-title">Drop ${i.multiple ? 'files' : 'a file'} here, or <label for="${id}" style="color:var(--accent);cursor:pointer">choose ${i.multiple?'them':'one'}</label></div>
        <div class="drop-sub">${i.accept ? humanAccept(i.accept) : 'any file'}${i.maxMb ? ' — Max ' + i.maxMb + ' MB' : ''}</div>
        <input id="${id}" type="file" ${i.multiple ? 'multiple' : ''} accept="${i.accept||''}" style="position:absolute;inset:0;opacity:0;cursor:pointer">
      `;
      const input = drop.querySelector('input[type=file]');
      input.addEventListener('change', () => {
        state.files = Array.from(input.files || []);
        renderFiles(filesPreview, state.files);
      });
      drop.addEventListener('dragover', e => { e.preventDefault(); drop.style.borderColor = 'var(--accent)'; drop.style.background = 'var(--accent-soft)'; });
      ['dragleave','drop'].forEach(ev => drop.addEventListener(ev, e => { e.preventDefault(); drop.style.borderColor = ''; drop.style.background = ''; }));
      drop.addEventListener('drop', e => {
        const f = Array.from(e.dataTransfer.files || []);
        state.files = i.multiple ? f : f.slice(0,1);
        renderFiles(filesPreview, state.files);
      });
      wrap.appendChild(drop);
      const filesPreview = el('div', { class: 'tpv-files' });
      wrap.appendChild(filesPreview);

      if (i.kind === 'fileOrText') {
        wrap.appendChild(el('div', { class: 'tpv-or', html: '<span>or</span>' }));
        const ta = el('textarea', { class: 'tpv-textarea', rows: '5', placeholder: i.placeholder || 'Paste text…' });
        ta.addEventListener('input', () => { state.text = ta.value; });
        wrap.appendChild(ta);
      }
    } else if (i.kind === 'text') {
      const input = el('input', { class: 'tpv-input', type: 'text', placeholder: i.placeholder || '' });
      input.addEventListener('input', () => { state.text = input.value; });
      wrap.appendChild(input);
    } else if (i.kind === 'url') {
      const input = el('input', { class: 'tpv-input', type: 'url', placeholder: i.placeholder || 'https://…' });
      input.addEventListener('input', () => { state.url = input.value; });
      wrap.appendChild(input);
    } else if (i.kind === 'textarea') {
      const ta = el('textarea', { class: 'tpv-textarea' + (i.mono ? ' tpv-mono' : ''), rows: '8', placeholder: i.placeholder || '' });
      ta.addEventListener('input', () => { state.text = ta.value; });
      wrap.appendChild(ta);
    } else if (i.kind === 'dualTextarea') {
      const row = el('div', { class: 'tpv-dual' });
      const a = el('textarea', { class: 'tpv-textarea', rows: '8', placeholder: i.placeholderA });
      const b = el('textarea', { class: 'tpv-textarea', rows: '8', placeholder: i.placeholderB });
      a.addEventListener('input', () => { state.textA = a.value; });
      b.addEventListener('input', () => { state.textB = b.value; });
      row.appendChild(a); row.appendChild(b);
      wrap.appendChild(row);
    } else if (i.kind === 'dualFileOrText') {
      // Two side-by-side panels, each with its own file drop + textarea
      const row = el('div', { class: 'tpv-dual tpv-dual-upload' });
      ['A','B'].forEach(side => {
        const lbl = side === 'A' ? (i.labelA||'Source') : (i.labelB||'Notes');
        const ph  = side === 'A' ? (i.placeholderA||'') : (i.placeholderB||'');
        const acc = side === 'A' ? (i.acceptA||'*/*') : (i.acceptB||'*/*');
        const fileKey = 'file'+side;  // fileA or fileB
        const textKey = 'text'+side;  // textA or textB
        const uid = 'tpv-df-' + side + '-' + Math.random().toString(36).slice(2,6);
        const panel = el('div', { class: 'tpv-dual-panel' });
        panel.innerHTML = `
          <div class="tpv-dual-label">${lbl}</div>
          <div class="tpv-dual-drop" id="${uid}-drop">
            <div class="drop-ic"><i class="fas fa-cloud-upload-alt" style="font-size:1.2rem"></i></div>
            <div class="drop-title">Drop a file or <label for="${uid}-file" style="color:var(--accent);cursor:pointer">browse</label></div>
            <div class="drop-sub">${acc.replace(/\./g,'').replace(/,/g,', ').toUpperCase()}</div>
            <input id="${uid}-file" type="file" accept="${acc}" style="position:absolute;inset:0;opacity:0;cursor:pointer;width:100%;height:100%">
          </div>
          <div class="tpv-dual-or"><span>or paste text</span></div>
          <textarea class="tpv-textarea" rows="5" placeholder="${ph}"></textarea>
          <div class="tpv-dual-file-name" id="${uid}-fname" style="display:none"></div>
        `;
        const dropEl = panel.querySelector(`#${uid}-drop`);
        const fileIn  = panel.querySelector(`#${uid}-file`);
        const fname   = panel.querySelector(`#${uid}-fname`);
        const ta      = panel.querySelector('textarea');
        function setFile(f) {
          state[fileKey] = f; state[textKey] = '';
          ta.disabled = true; ta.style.opacity = '0.4';
          fname.textContent = `📎 ${f.name} (${fmtBytes(f.size)})`;
          fname.style.display = ''; fname.style.color = 'var(--accent-light)';
          fname.style.fontSize = '12px'; fname.style.marginTop = '4px';
          dropEl.style.borderColor = 'var(--accent-line)';
          dropEl.style.background = 'var(--accent-soft)';
          // add clear button
          if (!fname.querySelector('button')) {
            const clr = document.createElement('button');
            clr.textContent = '✕'; clr.style.cssText = 'margin-left:8px;background:none;border:0;color:var(--text-3);cursor:pointer;font-size:13px;';
            clr.onclick = () => { state[fileKey]=null; fileIn.value=''; ta.disabled=false; ta.style.opacity=''; fname.style.display='none'; dropEl.style.borderColor=''; dropEl.style.background=''; };
            fname.appendChild(clr);
          }
        }
        fileIn.addEventListener('change', () => { if (fileIn.files[0]) setFile(fileIn.files[0]); });
        ta.addEventListener('input', () => { state[textKey] = ta.value; state[fileKey] = null; });
        ['dragover','dragenter'].forEach(ev => dropEl.addEventListener(ev, e => { e.preventDefault(); dropEl.style.borderColor='var(--accent-line)'; dropEl.style.background='var(--accent-soft)'; }));
        ['dragleave'].forEach(ev => dropEl.addEventListener(ev, () => { if (!state[fileKey]) { dropEl.style.borderColor=''; dropEl.style.background=''; } }));
        dropEl.addEventListener('drop', e => { e.preventDefault(); const f=e.dataTransfer.files[0]; if(f) setFile(f); });
        row.appendChild(panel);
      });
      wrap.appendChild(row);
    }
    return wrap;
  }
  function humanAccept(accept) {
    return accept
      .replace(/image\/\*/g, 'Images')
      .replace(/video\/\*/g, 'Videos')
      .replace(/audio\/\*/g, 'Audio')
      .replace(/application\/pdf/g, 'PDF')
      .replace(/\./g,'')
      .replace(/,/g, ', ')
      .toUpperCase();
  }
  function renderFiles(host, files) {
    host.innerHTML = '';
    if (!files || !files.length) return;
    files.forEach(f => {
      host.appendChild(el('div', { class: 'tpv-file-row' }, [
        el('span', { class: 'tpv-file-name' }, f.name),
        el('span', { class: 'tpv-file-size' }, fmtBytes(f.size)),
      ]));
    });
  }
  function fmtBytes(b) {
    if (b < 1024) return b + ' B';
    if (b < 1048576) return (b/1024).toFixed(1) + ' KB';
    if (b < 1073741824) return (b/1048576).toFixed(1) + ' MB';
    return (b/1073741824).toFixed(2) + ' GB';
  }

  // ── Options: segs / slider / select / text / number / toggle ───────────
  function buildOptions(spec, state) {
    if (!spec.options || !spec.options.length) return null;
    state.options = state.options || {};
    const wrap = el('div', { class: 'tpv-opts' });
    spec.options.forEach(opt => {
      if (opt.default !== undefined && state.options[opt.id] === undefined) state.options[opt.id] = opt.default;
      wrap.appendChild(buildOption(opt, state));
    });
    return wrap;
  }
  function buildOption(opt, state) {
    const row = el('div', { class: 'opt' });
    row.appendChild(el('span', { class: 'opt-label' }, opt.label));

    if (opt.kind === 'segs') {
      const segs = el('div', { class: 'opt-segs' });
      opt.values.forEach(([v, label]) => {
        const b = el('button', { class: 'opt-seg' + (state.options[opt.id] === v ? ' on' : '') }, label);
        b.addEventListener('click', () => {
          state.options[opt.id] = v;
          segs.querySelectorAll('.opt-seg').forEach(s => s.classList.remove('on'));
          b.classList.add('on');
        });
        segs.appendChild(b);
      });
      row.appendChild(segs);
    } else if (opt.kind === 'slider') {
      const valSpan = el('span', { class: 'opt-slider-val' }, opt.format ? opt.format(state.options[opt.id]) : String(state.options[opt.id]));
      const input = el('input', { class: 'opt-slider', type: 'range', min: opt.min, max: opt.max, step: opt.step || 1, value: state.options[opt.id] });
      input.addEventListener('input', () => {
        const v = parseFloat(input.value);
        state.options[opt.id] = v;
        valSpan.textContent = opt.format ? opt.format(v) : String(v);
      });
      const grp = el('div', { class: 'opt-slider-grp' }, [input, valSpan]);
      row.appendChild(grp);
    } else if (opt.kind === 'select') {
      const sel = el('select', { class: 'opt-select' });
      opt.values.forEach(([v, label]) => {
        const o = el('option', { value: v }, label);
        if (state.options[opt.id] === v) o.selected = true;
        sel.appendChild(o);
      });
      sel.addEventListener('change', () => { state.options[opt.id] = sel.value; });
      row.appendChild(sel);
    } else if (opt.kind === 'text' || opt.kind === 'number') {
      const input = el('input', { class: 'opt-input', type: opt.kind, placeholder: opt.placeholder || '' });
      if (state.options[opt.id] !== undefined && state.options[opt.id] !== '') input.value = state.options[opt.id];
      input.addEventListener('input', () => {
        state.options[opt.id] = opt.kind === 'number' ? (input.value === '' ? '' : parseFloat(input.value)) : input.value;
      });
      row.appendChild(input);
    } else if (opt.kind === 'toggle') {
      const lab = el('label', { class: 'opt-toggle' });
      const input = el('input', { type: 'checkbox' });
      input.checked = !!state.options[opt.id];
      input.addEventListener('change', () => { state.options[opt.id] = input.checked; });
      lab.appendChild(input);
      lab.appendChild(el('span', {}, input.checked ? 'On' : 'Off'));
      input.addEventListener('change', () => { lab.lastChild.textContent = input.checked ? 'On' : 'Off'; });
      row.appendChild(lab);
    }
    return row;
  }

  // ── Actions: run / reset ──────────────────────────────────────────────
  function buildActions(spec, state, onRun, onReset) {
    const wrap = el('div', { class: 'tpv-actions' });
    const run = el('button', { class: 'btn-primary btn-lg tpv-run' });
    run.innerHTML = `${fa(spec.run.icon)} ${spec.run.label}`;
    run.addEventListener('click', () => onRun(run));
    const reset = el('button', { class: 'btn-ghost btn-lg btn-outline tpv-reset' }, 'Reset');
    reset.addEventListener('click', () => onReset());
    wrap.appendChild(run);
    wrap.appendChild(reset);
    return wrap;
  }

  // ── Output area: file download / text / json preview ───────────────────
  function buildOutput() {
    return el('div', { class: 'tpv-output', id: 'tpvOutput' });
  }

  function showOutput(kind, payload) {
    const out = $('#tpvOutput');
    if (!out) return;
    out.innerHTML = '';
    if (kind === 'progress') {
      out.appendChild(el('div', { class: 'tpv-progress' }, [
        el('div', { class: 'tpv-progress-bar', style: { width: (payload||0)+'%' } }),
      ]));
    } else if (kind === 'error') {
      out.appendChild(el('div', { class: 'tpv-error' }, payload || 'Something went wrong.'));
    } else if (kind === 'text') {
      out.appendChild(el('div', { class: 'tpv-text-out' }, payload));
    } else if (kind === 'json') {
      out.appendChild(el('pre', { class: 'tpv-json-out' }, JSON.stringify(payload, null, 2)));
    } else if (kind === 'file') {
      // payload: { url, filename }
      out.appendChild(el('div', { class: 'tpv-file-out' }, [
        el('div', { class: 'tpv-file-name' }, payload.filename),
        el('a', { class: 'btn-primary btn-lg', href: payload.url, download: payload.filename || true }, ['Download ', fa('fa-download') && '']),
      ]));
    } else if (kind === 'busy') {
      out.appendChild(el('div', { class: 'tpv-busy' }, [
        el('span', { class: 'tpv-spinner', html: '<i class="fas fa-circle-notch fa-spin"></i>' }),
        el('span', {}, payload || 'Working…'),
      ]));
    }
  }

  // ── Runners ────────────────────────────────────────────────────────────
  async function runSpec(tool, spec, state) {
    const r = spec.run;
    if (r.via === 'browser' && r.handler) {
      return runBrowserHandler(tool, spec, state, r.handler);
    }
    if (r.via === 'server' && r.endpoint) {
      return runServerEndpoint(tool, spec, state);
    }
    showOutput('error', 'This tool is not yet wired.');
  }

  async function runServerEndpoint(tool, spec, state) {
    const r = spec.run;
    showOutput('busy', 'Uploading…');
    try {
      let body, headers = {};
      if (r.jsonBody) {
        body = JSON.stringify(r.buildBody ? r.buildBody(state) : { text: state.text || '' });
        headers['Content-Type'] = 'application/json';
      } else if (r.formData) {
        if (r.buildFormData) {
          body = r.buildFormData(state);
        } else {
          body = new FormData();
          // attach files
          (state.files || []).forEach((f, i) => {
            if (state.files.length === 1) body.append('file', f);
            else body.append('file' + i, f);
          });
          if (state.files && state.files.length > 1) body.append('filecount', state.files.length);
          // attach options
          if (state.options) for (const k in state.options) body.append(k, state.options[k]);
          // attach text/url
          if (state.text) body.append('text', state.text);
          if (state.url)  body.append('url',  state.url);
        }
      }
      const res = await fetch(r.endpoint, { method: r.method || 'POST', headers, body });
      const ct = res.headers.get('content-type') || '';
      if (!res.ok) {
        let msg = res.status + ' ' + res.statusText;
        if (ct.includes('application/json')) {
          try { const j = await res.json(); msg = j.error || msg; } catch {}
        }
        showOutput('error', msg);
        return;
      }
      if (r.outputFile) {
        // either a direct file download or a JSON with a download URL
        if (ct.includes('application/json')) {
          const j = await res.json();
          if (j.download_url) {
            showOutput('file', { url: j.download_url, filename: j.filename || tool });
          } else if (j.job_id) {
            pollJob(j.job_id, tool, spec);
          } else {
            showOutput('json', j);
          }
        } else {
          const blob = await res.blob();
          const url = URL.createObjectURL(blob);
          const filename = guessFilename(res, tool);
          showOutput('file', { url, filename });
          // Before/After size diff bar for compress/convert tools
          const origSize = (state.files && state.files[0]) ? state.files[0].size : 0;
          if (origSize > 0 && blob.size > 0) {
            const pct = Math.round((1 - blob.size / origSize) * 100);
            const smaller = blob.size < origSize;
            const out = $('#tpvOutput');
            if (out) {
              const bar = document.createElement('div');
              bar.className = 'luma-diff';
              bar.innerHTML = `
                <span class="luma-diff-label">Before</span>
                <span class="luma-diff-size">${fmtBytes(origSize)}</span>
                <span class="luma-diff-arrow">→</span>
                <span class="luma-diff-size">${fmtBytes(blob.size)}</span>
                <span class="luma-diff-pct ${smaller ? 'is-smaller' : 'is-larger'}">${smaller ? '−' : '+'}${Math.abs(pct)}%</span>
                <div class="luma-diff-bar"><div class="luma-diff-fill" style="width:${Math.min(100,blob.size/origSize*100).toFixed(1)}%;background:${smaller?'var(--ok)':'#f87171'}"></div></div>
              `;
              out.appendChild(bar);
            }
          }
        }
      } else if (r.outputText) {
        if (ct.includes('application/json')) {
          const j = await res.json();
          showOutput('text', j.result || j.text || j.citation || JSON.stringify(j));
        } else {
          showOutput('text', await res.text());
        }
      } else if (r.outputJson) {
        const j = await res.json();
        // Special rendered output for Coverage Check
        if (j && typeof j.overall_score === 'number') {
          showCoverageResult(j);
        } else {
          showOutput('json', j);
        }
      } else {
        showOutput('text', await res.text());
      }
    } catch (e) {
      showOutput('error', String(e));
    }
  }

  function showCoverageResult(j) {
    const out = $('#tpvOutput');
    if (!out) return;
    // Use local HTML escape — NOT the browser's global escape() which URL-encodes
    const esc = s => String(s == null ? '' : s).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
    const scoreColor = j.overall_score >= 80 ? 'var(--ok)' : j.overall_score >= 60 ? '#fbbf24' : '#f87171';
    const concepts = (j.key_concepts || []);
    const covered = concepts.filter(c => c.covered).length;
    const html = `
      <div class="cov-result">
        <div class="cov-header">
          <div class="cov-score" style="border-color:${scoreColor};color:${scoreColor}">${j.overall_score}<span>/100</span></div>
          <div class="cov-verdict-block">
            <div class="cov-verdict">${esc(j.verdict||'')}</div>
            <div class="cov-summary">${esc(j.summary||'')}</div>
          </div>
        </div>
        <div class="cov-progress-row">
          <span class="cov-prog-label">Concepts covered: ${covered} / ${concepts.length}</span>
          <div class="cov-prog-bar"><div class="cov-prog-fill" style="width:${concepts.length?Math.round(covered/concepts.length*100):0}%;background:${scoreColor}"></div></div>
        </div>
        ${j.gaps && j.gaps.length ? `<div class="cov-section"><div class="cov-section-head">🔴 Gaps to address</div><ul class="cov-list">${j.gaps.map(g=>`<li>${esc(g)}</li>`).join('')}</ul></div>` : ''}
        ${j.strengths && j.strengths.length ? `<div class="cov-section"><div class="cov-section-head">🟢 Strengths</div><ul class="cov-list">${j.strengths.map(s=>`<li>${esc(s)}</li>`).join('')}</ul></div>` : ''}
        ${j.study_tips && j.study_tips.length ? `<div class="cov-section"><div class="cov-section-head">💡 Study tips</div><ul class="cov-list">${j.study_tips.map(t=>`<li>${esc(t)}</li>`).join('')}</ul></div>` : ''}
        ${concepts.length ? `<details class="cov-concepts"><summary>All concepts (${concepts.length})</summary><div class="cov-concepts-grid">${concepts.map(c=>`<div class="cov-concept ${c.covered?'ok':'gap'}">${c.covered?'✓':'✗'} <span>${esc(c.topic)}</span><small>${esc(c.importance)}</small></div>`).join('')}</div></details>` : ''}
      </div>`;
    out.innerHTML = html;
    out.style.display = '';
  }

  function guessFilename(res, tool) {
    const cd = res.headers.get('content-disposition') || '';
    const m = cd.match(/filename="?([^";]+)"?/);
    if (m) return m[1];
    return tool + '-result';
  }

  async function pollJob(jobId, tool, spec) {
    showOutput('busy', 'Processing…');
    for (let i = 0; i < 300; i++) {
      await new Promise(r => setTimeout(r, 1500));
      try {
        const r = await fetch('/api/jobs/' + encodeURIComponent(jobId));
        if (!r.ok) continue;
        const j = await r.json();
        if (j.status === 'completed') {
          showOutput('file', { url: j.download_url || ('/api/jobs/' + jobId + '/download'), filename: j.filename || tool });
          return;
        }
        if (j.status === 'error' || j.status === 'failed') {
          showOutput('error', j.error || 'Job failed');
          return;
        }
        showOutput('busy', (j.stage || 'Processing') + (j.progress ? ' · ' + j.progress + '%' : ''));
      } catch {}
    }
    showOutput('error', 'Job timed out.');
  }

  // ── Browser-side runners: delegate to existing tool JS where possible ─
  async function runBrowserHandler(tool, spec, state, handlerKey) {
    showOutput('busy', 'Processing…');
    try {
      const fn = BROWSER_HANDLERS[handlerKey];
      if (!fn) return showOutput('error', 'Browser handler missing: ' + handlerKey);
      const out = await fn(state);
      if (!out) showOutput('text', 'Done.');
      else if (out.kind) {
        showOutput(out.kind, out.payload);
        // Before/After diff bar for file results
        if (out.kind === 'file') {
          const origSize = state.files && state.files[0] ? state.files[0].size : 0;
          const outSize  = out.payload && out.payload.size ? out.payload.size : 0;
          if (origSize > 0 && outSize > 0) {
            const pct = Math.round((1 - outSize / origSize) * 100);
            const smaller = outSize < origSize;
            const outel = $('#tpvOutput');
            if (outel) {
              const bar = document.createElement('div');
              bar.className = 'luma-diff';
              bar.innerHTML = `
                <span class="luma-diff-label">Before</span>
                <span class="luma-diff-size">${fmtBytes(origSize)}</span>
                <span class="luma-diff-arrow">→</span>
                <span class="luma-diff-size">${fmtBytes(outSize)}</span>
                <span class="luma-diff-pct ${smaller?'is-smaller':'is-larger'}">${smaller?'−':'+'}${Math.abs(pct)}%</span>
                <div class="luma-diff-bar"><div class="luma-diff-fill" style="width:${Math.min(100,outSize/origSize*100).toFixed(1)}%;background:${smaller?'var(--ok)':'#f87171'}"></div></div>
              `;
              outel.appendChild(bar);
            }
          }
        }
      }
      else showOutput('text', out);
    } catch (e) {
      showOutput('error', String(e));
    }
  }

  const BROWSER_HANDLERS = {
    base64: (s) => {
      const m = s.options && s.options.mode === 'decode' ? 'decode' : 'encode';
      try {
        if (m === 'encode') return { kind: 'text', payload: btoa(unescape(encodeURIComponent(s.text || ''))) };
        return { kind: 'text', payload: decodeURIComponent(escape(atob(s.text || ''))) };
      } catch (e) { return { kind: 'error', payload: 'Bad input: ' + e.message }; }
    },
    jsonFormat: (s) => {
      try {
        const obj = JSON.parse(s.text || 'null');
        const mode = (s.options && s.options.mode) || 'pretty';
        const indent = s.options && s.options.indent === 'tab' ? '\t' : parseInt((s.options && s.options.indent) || '2', 10);
        if (mode === 'minify') return { kind: 'text', payload: JSON.stringify(obj) };
        if (mode === 'validate') return { kind: 'text', payload: '✓ Valid JSON' };
        return { kind: 'text', payload: JSON.stringify(obj, null, indent) };
      } catch (e) { return { kind: 'error', payload: 'Invalid JSON: ' + e.message }; }
    },
    urlEncode: (s) => {
      const m = s.options && s.options.mode === 'decode' ? 'decode' : 'encode';
      try {
        return { kind: 'text', payload: m === 'encode' ? encodeURIComponent(s.text || '') : decodeURIComponent(s.text || '') };
      } catch (e) { return { kind: 'error', payload: 'Bad input: ' + e.message }; }
    },
    wordCount: (s) => {
      const t = s.text || '';
      const words = (t.match(/\b\w+\b/g) || []).length;
      const chars = t.length;
      const sentences = (t.match(/[.!?]+/g) || []).length;
      const lines = t.split(/\n/).length;
      return { kind: 'json', payload: { words, chars, sentences, lines } };
    },
    unixDate: (s) => {
      const v = (s.text || '').trim();
      if (!v) return { kind: 'error', payload: 'Enter a value' };
      let d, asEpoch;
      if (/^\d{9,13}$/.test(v)) {
        asEpoch = +v < 1e12 ? +v * 1000 : +v;
        d = new Date(asEpoch);
        return { kind: 'json', payload: { input: v, iso: d.toISOString(), local: d.toString(), unix_sec: Math.floor(asEpoch/1000) } };
      }
      d = new Date(v);
      if (isNaN(d)) return { kind: 'error', payload: 'Unrecognised date' };
      return { kind: 'json', payload: { input: v, iso: d.toISOString(), local: d.toString(), unix_sec: Math.floor(d.getTime()/1000), unix_ms: d.getTime() } };
    },
    passwordGen: (s) => {
      const len = (s.options && s.options.length) || 20;
      let pool = '';
      if (s.options.lower)  pool += 'abcdefghijklmnopqrstuvwxyz';
      if (s.options.upper)  pool += 'ABCDEFGHIJKLMNOPQRSTUVWXYZ';
      if (s.options.digits) pool += '0123456789';
      if (s.options.symbols) pool += '!@#$%^&*()-_=+[]{}<>?/.,;:';
      if (!pool) return { kind: 'error', payload: 'Pick at least one charset' };
      const a = new Uint32Array(len);
      crypto.getRandomValues(a);
      let pw = '';
      for (let i = 0; i < len; i++) pw += pool[a[i] % pool.length];
      return { kind: 'text', payload: pw };
    },
    uuidGen: (s) => {
      const kind = (s.options && s.options.kind) || 'v4';
      const count = parseInt((s.options && s.options.count) || '1', 10);
      const out = [];
      for (let i = 0; i < count; i++) {
        if (kind === 'v4') out.push(crypto.randomUUID());
        else if (kind === 'v7') out.push(uuidV7());
        else if (kind === 'ulid') out.push(ulid());
        else out.push(nanoid());
      }
      return { kind: 'text', payload: out.join('\n') };
    },
    colorConvert: (s) => {
      const c = parseColor(s.text || '');
      if (!c) return { kind: 'error', payload: 'Unrecognised color' };
      return { kind: 'json', payload: c };
    },
    diffCheck: (s) => {
      const a = (s.textA || '').split('\n');
      const b = (s.textB || '').split('\n');
      const out = [];
      const max = Math.max(a.length, b.length);
      for (let i = 0; i < max; i++) {
        if (a[i] === b[i]) out.push('  ' + (a[i] || ''));
        else { if (a[i] != null) out.push('- ' + a[i]); if (b[i] != null) out.push('+ ' + b[i]); }
      }
      return { kind: 'text', payload: out.join('\n') };
    },
    mdPreview: (s) => ({ kind: 'text', payload: s.text || '' }),
    codeBeautify: (s) => {
      // crude indent; for now just return as-is unless JSON which we can format
      if ((s.options && s.options.lang) === 'json') {
        try { return { kind: 'text', payload: JSON.stringify(JSON.parse(s.text||''), null, parseInt(s.options.indent||'2',10) || 2) }; }
        catch { return { kind: 'error', payload: 'Invalid JSON' }; }
      }
      return { kind: 'text', payload: s.text || '' };
    },
    regexTest: (s) => {
      const pat = (s.options && s.options.pattern) || '';
      const m = pat.match(/^\/(.*)\/([gimsuy]*)$/);
      try {
        const re = m ? new RegExp(m[1], m[2]) : new RegExp(pat);
        const hits = (s.text || '').match(re);
        return { kind: 'json', payload: { pattern: pat, matches: hits || [] } };
      } catch (e) { return { kind: 'error', payload: e.message }; }
    },
    qrGenerate: async (s) => {
      if (!window.qrcode) return { kind: 'error', payload: 'QR library not loaded' };
      const size = ({small:4, medium:6, large:10})[(s.options && s.options.size) || 'medium'];
      const level = (s.options && s.options.level) || 'M';
      const qr = window.qrcode(0, level);
      qr.addData(s.text || '');
      qr.make();
      const img = qr.createImgTag(size, 12);
      return { kind: 'text', payload: img };
    },
    imageCompress: async (s) => {
      if (!s.files || !s.files.length) return { kind: 'error', payload: 'Pick an image' };
      const q = ({light:0.5, medium:0.75, high:0.9, max:1})[(s.options && s.options.quality) || 'medium'];
      const fmt = (s.options && s.options.format) || 'auto';
      const file = s.files[0];
      const img = await loadImage(file);
      const canvas = document.createElement('canvas');
      canvas.width = img.width; canvas.height = img.height;
      canvas.getContext('2d').drawImage(img, 0, 0);
      const type = fmt === 'auto' ? file.type : ('image/' + fmt);
      return new Promise(res => {
        canvas.toBlob(b => {
          const url = URL.createObjectURL(b);
          res({ kind: 'file', payload: { url, size: b.size, filename: stripExt(file.name) + '_LumaTools.' + (fmt === 'auto' ? file.name.split('.').pop() : fmt) } });
        }, type, q);
      });
    },
    imageResize: async (s) => {
      if (!s.files || !s.files.length) return { kind: 'error', payload: 'Pick an image' };
      const w = parseInt(s.options.width, 10);
      const h = parseInt(s.options.height, 10);
      if (!w && !h) return { kind: 'error', payload: 'Set width or height' };
      const file = s.files[0];
      const img = await loadImage(file);
      const ratio = img.width / img.height;
      const nw = w || Math.round(h * ratio);
      const nh = h || Math.round(w / ratio);
      const c = document.createElement('canvas'); c.width = nw; c.height = nh;
      c.getContext('2d').drawImage(img, 0, 0, nw, nh);
      return new Promise(res => c.toBlob(b => {
        res({ kind: 'file', payload: { url: URL.createObjectURL(b), size: b.size, filename: stripExt(file.name) + '_LumaTools-' + nw + 'x' + nh + '.' + (file.name.split('.').pop()||'png') } });
      }, file.type, 0.92));
    },
    imageConvert: async (s) => {
      if (!s.files || !s.files.length) return { kind: 'error', payload: 'Pick an image' };
      const fmt = (s.options && s.options.format) || 'webp';
      const file = s.files[0];
      const img = await loadImage(file);
      const c = document.createElement('canvas'); c.width = img.width; c.height = img.height;
      c.getContext('2d').drawImage(img, 0, 0);
      return new Promise(res => c.toBlob(b => {
        res({ kind: 'file', payload: { url: URL.createObjectURL(b), size: b.size, filename: stripExt(file.name) + '_LumaTools.' + fmt } });
      }, 'image/' + fmt, 0.9));
    },
    audioConvert: async (s) => ({ kind: 'error', payload: 'Audio convert requires ffmpeg.wasm — falling back to server. Use the existing form or contact support.' }),
    bulkInstall: async (s) => {
      const urls = (s.text || '').split(/\r?\n/).map(l => l.trim()).filter(l => /^https?:\/\//i.test(l));
      if (!urls.length) return { kind: 'error', payload: 'Paste at least one URL (one per line, starting with http).' };
      const results = [];
      for (const url of urls) {
        try {
          const r = await fetch('/api/download', {
            method: 'POST', headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ url, format: s.options.format, quality: s.options.quality }),
          });
          if (r.ok) { const j = await r.json(); results.push({ url, ...j }); }
          else { let msg = r.status; try { msg = (await r.json()).error || msg; } catch {} results.push({ url, error: String(msg) }); }
        } catch (e) { results.push({ url, error: String(e) }); }
      }
      return { kind: 'json', payload: { queued: urls.length, results } };
    },
    downloader: async (s) => {
      if (!s.url) return { kind: 'error', payload: 'Paste a URL first' };
      // Delegate to existing downloader logic if available
      try {
        const r = await fetch('/api/download', {
          method: 'POST', headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ url: s.url, format: s.options.format, quality: s.options.quality }),
        });
        if (!r.ok) {
          let msg = r.status + ' ' + r.statusText;
          try { const j = await r.json(); msg = j.error || msg; } catch {}
          return { kind: 'error', payload: msg };
        }
        const j = await r.json();
        if (j.job_id) { pollJob(j.job_id, 'downloader', SPECS['downloader']); return null; }
        if (j.download_url) return { kind: 'file', payload: { url: j.download_url, filename: j.filename || 'download' } };
        return { kind: 'json', payload: j };
      } catch (e) { return { kind: 'error', payload: String(e) }; }
    },
  };
  function loadImage(file) {
    return new Promise((res, rej) => {
      const url = URL.createObjectURL(file);
      const img = new Image();
      img.onload = () => { res(img); };
      img.onerror = rej;
      img.src = url;
    });
  }
  function stripExt(n) { return n.replace(/\.[^.]+$/, ''); }
  // Minimal UUID v7
  function uuidV7() {
    const t = BigInt(Date.now());
    const r = crypto.getRandomValues(new Uint8Array(10));
    const hex = (n) => n.toString(16).padStart(2,'0');
    const ts = t.toString(16).padStart(12,'0');
    return `${ts.slice(0,8)}-${ts.slice(8,12)}-7${hex(r[0]).slice(1)}${hex(r[1])}-${(0x80 | (r[2] & 0x3f)).toString(16)}${hex(r[3])}-${[...r.slice(4)].map(hex).join('')}`;
  }
  function ulid() {
    const A='0123456789ABCDEFGHJKMNPQRSTVWXYZ';
    let t = Date.now(), out = '';
    for (let i = 0; i < 10; i++) { out = A[t & 31] + out; t = Math.floor(t / 32); }
    const r = crypto.getRandomValues(new Uint8Array(16));
    for (let i = 0; i < 16; i++) out += A[r[i] & 31];
    return out;
  }
  function nanoid() {
    const A='useandom-26T198340PX75pxJACKVERYMINDBUSHWOLF_GQZbfghjklqvwyzrict';
    const r = crypto.getRandomValues(new Uint8Array(21));
    let out = '';
    for (let i = 0; i < 21; i++) out += A[r[i] & 63];
    return out;
  }
  function parseColor(s) {
    s = s.trim();
    let r,g,b;
    let m = s.match(/^#?([0-9a-f]{6})$/i); if (m) { const n = parseInt(m[1],16); r=(n>>16)&255; g=(n>>8)&255; b=n&255; }
    if (!m) { m = s.match(/rgb\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)/i); if (m) { r=+m[1]; g=+m[2]; b=+m[3]; } }
    if (r==null) return null;
    const max=Math.max(r,g,b)/255, min=Math.min(r,g,b)/255, l=(max+min)/2;
    let h=0,s2=0;
    if (max!==min) {
      const d=max-min;
      s2 = l > 0.5 ? d/(2-max-min) : d/(max+min);
      switch (max) {
        case r/255: h = ((g/255-b/255)/d + (g<b?6:0)); break;
        case g/255: h = ((b/255-r/255)/d + 2); break;
        default:    h = ((r/255-g/255)/d + 4); break;
      }
      h *= 60;
    }
    return {
      hex: '#'+((1<<24)+(r<<16)+(g<<8)+b).toString(16).slice(1),
      rgb: `rgb(${r}, ${g}, ${b})`,
      hsl: `hsl(${Math.round(h)}, ${Math.round(s2*100)}%, ${Math.round(l*100)}%)`,
    };
  }

  // ── Public renderer: builds the whole tpage-main contents for a tool ──
  function renderToolPage(tool) {
    const spec = SPECS[tool];
    if (!spec) return null;  // caller falls back to legacy panel
    const state = STATE[tool] = { files: [], text: '', textA: '', textB: '', url: '', options: {} };

    const main = el('div', { class: 'tpv' });
    const intake = buildIntake(spec, state);
    if (intake) main.appendChild(intake);
    const opts = buildOptions(spec, state);
    if (opts) main.appendChild(opts);
    const actions = buildActions(spec, state,
      (btn) => { btn.disabled = true; runSpec(tool, spec, state).finally(() => { btn.disabled = false; }); },
      () => { STATE[tool] = null; rerender(tool); });
    main.appendChild(actions);
    const out = buildOutput();
    main.appendChild(out);
    return main;
  }

  function rerender(tool) {
    const host = $('#toolHost');
    if (!host) return;
    host.innerHTML = '';
    const built = renderToolPage(tool);
    if (built) host.appendChild(built);
  }

  // Expose to app-shell.js
  window.LumaToolPage = {
    has: (tool) => !!SPECS[tool],
    render: renderToolPage,
    rerender,
  };
})();
