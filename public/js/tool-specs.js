// ════════════════════════════════════════════════════════════════════════
// LUMA TOOLS — declarative tool specs for the v2 ToolPage renderer
//
// Each spec describes the form: a drop zone (or URL/text input), a set
// of option rows (segments, sliders, selects), and which endpoint to
// hit. The renderer in tool-page.js turns these specs into the design's
// pixel-perfect ToolPage layout.
//
// Tools NOT covered here keep their existing legacy panel (still
// rendered inside .tpage-main with the polished CSS). The renderer
// detects whether a spec exists; if not, it falls back to the legacy
// panel.
// ════════════════════════════════════════════════════════════════════════

window.LUMA_TOOL_SPECS = {

  // ── Image ──────────────────────────────────────────────────────────────
  'image-compress': {
    intake: { kind: 'file', accept: 'image/*,.heic,.heif', multiple: true, maxMb: 50 },
    options: [
      { id: 'quality', label: 'Quality', kind: 'segs',
        values: [['light','Light'],['medium','Medium'],['high','High'],['max','Max']],
        default: 'medium' },
      { id: 'format', label: 'Output format', kind: 'segs',
        values: [['auto','Auto'],['jpg','JPG'],['png','PNG'],['webp','WebP'],['avif','AVIF']],
        default: 'auto' },
    ],
    run: { label: 'Compress', icon: 'fa-compress-alt', via: 'browser', handler: 'imageCompress' },
  },

  'image-resize': {
    intake: { kind: 'file', accept: 'image/*', multiple: true, maxMb: 50 },
    options: [
      { id: 'mode',   label: 'Mode',  kind: 'segs',
        values: [['fit','Fit'],['stretch','Stretch'],['crop','Crop']], default: 'fit' },
      { id: 'width',  label: 'Width (px)',  kind: 'number', placeholder: '1920' },
      { id: 'height', label: 'Height (px)', kind: 'number', placeholder: '1080' },
    ],
    run: { label: 'Resize', icon: 'fa-expand-arrows-alt', via: 'browser', handler: 'imageResize' },
  },

  'image-convert': {
    intake: { kind: 'file', accept: 'image/*,.heic,.heif', multiple: true, maxMb: 50 },
    options: [
      { id: 'format', label: 'Target format', kind: 'segs',
        values: [['jpg','JPG'],['png','PNG'],['webp','WebP'],['avif','AVIF'],['bmp','BMP'],['tiff','TIFF']],
        default: 'jpg' },
    ],
    run: { label: 'Convert', icon: 'fa-exchange-alt', via: 'browser', handler: 'imageConvert' },
  },

  'image-watermark': {
    intake: { kind: 'file', accept: 'image/*' },
    options: [
      { id: 'text', label: 'Watermark text', kind: 'text', placeholder: '© Your name', required: true },
      { id: 'position', label: 'Position', kind: 'segs',
        values: [['nw','TL'],['n','T'],['ne','TR'],['w','L'],['c','Center'],['e','R'],['sw','BL'],['s','B'],['se','BR']],
        default: 'se' },
      { id: 'opacity', label: 'Opacity', kind: 'slider', min: 0.1, max: 1, step: 0.05, default: 0.5, format: v => Math.round(v*100)+'%' },
    ],
    run: { label: 'Apply watermark', icon: 'fa-stamp', via: 'server', endpoint: '/api/tools/image-watermark', method: 'POST', formData: true, outputFile: true },
  },

  'image-bg-remove': {
    intake: { kind: 'file', accept: 'image/*' },
    options: [],
    run: { label: 'Remove background', icon: 'fa-user-alt', via: 'server', endpoint: '/api/tools/image-bg-remove', method: 'POST', formData: true, outputFile: true },
  },

  'image-upscale': {
    intake: { kind: 'file', accept: 'image/*' },
    options: [
      { id: 'scale', label: 'Scale', kind: 'segs',
        values: [['2','2×'],['3','3×'],['4','4×']], default: '2' },
    ],
    run: { label: 'Upscale', icon: 'fa-expand', via: 'server', endpoint: '/api/tools/image-upscale', method: 'POST', formData: true, outputFile: true },
  },

  'ocr': {
    intake: { kind: 'file', accept: 'image/*,.pdf' },
    options: [
      { id: 'lang', label: 'Language', kind: 'select',
        values: [['eng','English'],['spa','Spanish'],['fra','French'],['deu','German'],['ita','Italian'],['por','Portuguese'],['chi_sim','Chinese (simplified)'],['jpn','Japanese'],['kor','Korean'],['ara','Arabic']],
        default: 'eng' },
    ],
    run: { label: 'Extract text', icon: 'fa-font', via: 'server', endpoint: '/api/tools/ocr', method: 'POST', formData: true, outputText: true },
  },

  'metadata-strip': {
    intake: { kind: 'file', accept: 'image/*,video/*,audio/*,.pdf,.docx' },
    options: [],
    run: { label: 'Strip metadata', icon: 'fa-eraser', via: 'server', endpoint: '/api/tools/metadata-strip', method: 'POST', formData: true, outputFile: true },
  },

  'favicon-generate': {
    intake: { kind: 'file', accept: 'image/*,.svg' },
    options: [],
    run: { label: 'Generate pack', icon: 'fa-star', via: 'server', endpoint: '/api/tools/favicon-generate', method: 'POST', formData: true, outputFile: true },
  },

  // ── Video ──────────────────────────────────────────────────────────────
  'video-compress': {
    intake: { kind: 'file', accept: 'video/*', maxMb: 500 },
    options: [
      { id: 'preset', label: 'Compression', kind: 'segs',
        values: [['light','Light'],['medium','Medium'],['heavy','Heavy']], default: 'medium' },
    ],
    run: { label: 'Compress', icon: 'fa-file-video', via: 'server', endpoint: '/api/tools/video-compress', method: 'POST', formData: true, outputFile: true },
  },
  'video-trim': {
    intake: { kind: 'file', accept: 'video/*' },
    options: [
      { id: 'start', label: 'Start (s)', kind: 'number', default: 0 },
      { id: 'end',   label: 'End (s)',   kind: 'number', default: 10 },
    ],
    run: { label: 'Trim', icon: 'fa-cut', via: 'server', endpoint: '/api/tools/video-trim', method: 'POST', formData: true, outputFile: true },
  },
  'video-convert': {
    intake: { kind: 'file', accept: 'video/*' },
    options: [
      { id: 'format', label: 'Format', kind: 'segs',
        values: [['mp4','MP4'],['webm','WebM'],['mkv','MKV'],['avi','AVI'],['mov','MOV']], default: 'mp4' },
    ],
    run: { label: 'Convert', icon: 'fa-film', via: 'server', endpoint: '/api/tools/video-convert', method: 'POST', formData: true, outputFile: true },
  },
  'video-extract-audio': {
    intake: { kind: 'file', accept: 'video/*' },
    options: [
      { id: 'format', label: 'Audio format', kind: 'segs',
        values: [['mp3','MP3'],['m4a','M4A'],['wav','WAV'],['flac','FLAC'],['opus','Opus']], default: 'mp3' },
    ],
    run: { label: 'Extract audio', icon: 'fa-volume-up', via: 'server', endpoint: '/api/tools/video-extract-audio', method: 'POST', formData: true, outputFile: true },
  },
  'video-to-gif': {
    intake: { kind: 'file', accept: 'video/*' },
    options: [
      { id: 'fps',   label: 'FPS',    kind: 'slider', min: 5, max: 30, step: 1, default: 15, format: v => v+' fps' },
      { id: 'width', label: 'Width',  kind: 'segs',
        values: [['320','320'],['480','480'],['640','640'],['800','800']], default: '480' },
    ],
    run: { label: 'Make GIF', icon: 'fa-magic', via: 'server', endpoint: '/api/tools/video-to-gif', method: 'POST', formData: true, outputFile: true },
  },
  'gif-optimise': {
    intake: { kind: 'file', accept: 'image/gif,.gif' },
    options: [
      { id: 'quality', label: 'Quality', kind: 'slider', min: 20, max: 100, step: 5, default: 80, format: v => v + '%' },
    ],
    run: { label: 'Optimise', icon: 'fa-compress', via: 'server', endpoint: '/api/tools/gif-optimise', method: 'POST', formData: true, outputFile: true },
  },
  'subtitle-burn': {
    intake: { kind: 'dualFileOrText',
      labelA: 'Video file', placeholderA: 'Drop your MP4 / MKV…',
      acceptA: 'video/*',
      labelB: 'Subtitle file', placeholderB: 'Drop your .srt or .vtt…',
      acceptB: '.srt,.vtt,.ass' },
    options: [],
    run: { label: 'Burn subtitles', icon: 'fa-closed-captioning', via: 'server',
           endpoint: '/api/tools/subtitle-burn', method: 'POST', formData: true, outputFile: true,
           buildFormData: (s) => {
             const f = new FormData();
             if (s.fileA) f.append('file', s.fileA);
             if (s.fileB) f.append('subs', s.fileB);
             return f;
           }},
  },
  'gif-to-video': {
    intake: { kind: 'file', accept: 'image/gif,.gif' },
    options: [
      { id: 'format', label: 'Output', kind: 'segs', values: [['mp4','MP4'],['webm','WebM']], default: 'mp4' },
    ],
    run: { label: 'Convert', icon: 'fa-photo-video', via: 'server', endpoint: '/api/tools/gif-to-video', method: 'POST', formData: true, outputFile: true },
  },
  'video-remove-audio': {
    intake: { kind: 'file', accept: 'video/*' },
    options: [],
    run: { label: 'Mute video', icon: 'fa-volume-mute', via: 'server', endpoint: '/api/tools/video-remove-audio', method: 'POST', formData: true, outputFile: true },
  },
  'video-speed': {
    intake: { kind: 'file', accept: 'video/*' },
    options: [
      { id: 'speed', label: 'Speed', kind: 'segs',
        values: [['0.25','¼×'],['0.5','½×'],['1','1×'],['2','2×'],['4','4×']], default: '2' },
    ],
    run: { label: 'Change speed', icon: 'fa-tachometer-alt', via: 'server', endpoint: '/api/tools/video-speed', method: 'POST', formData: true, outputFile: true },
  },
  'video-frame': {
    intake: { kind: 'file', accept: 'video/*' },
    options: [
      { id: 'time',   label: 'Time (s)',  kind: 'number', default: 0 },
      { id: 'format', label: 'Format',    kind: 'segs',
        values: [['png','PNG'],['jpg','JPG'],['webp','WebP']], default: 'png' },
    ],
    run: { label: 'Extract frame', icon: 'fa-camera', via: 'server', endpoint: '/api/tools/video-frame', method: 'POST', formData: true, outputFile: true },
  },
  'video-stabilize': {
    intake: { kind: 'file', accept: 'video/*' },
    options: [
      { id: 'strength', label: 'Strength', kind: 'segs', values: [['low','Low'],['medium','Medium'],['high','High']], default: 'medium' },
    ],
    run: { label: 'Stabilize', icon: 'fa-hand-paper', via: 'server', endpoint: '/api/tools/video-stabilize', method: 'POST', formData: true, outputFile: true },
  },
  'subtitle-extract': {
    intake: { kind: 'file', accept: 'video/*' },
    options: [
      { id: 'format', label: 'Format', kind: 'segs', values: [['srt','SRT'],['vtt','VTT'],['ass','ASS'],['txt','Plain']], default: 'srt' },
    ],
    run: { label: 'Extract subtitles', icon: 'fa-closed-captioning', via: 'server', endpoint: '/api/tools/subtitle-extract', method: 'POST', formData: true, outputFile: true },
  },

  // ── Audio ──────────────────────────────────────────────────────────────
  'audio-convert': {
    intake: { kind: 'file', accept: 'audio/*', multiple: true, maxMb: 200 },
    options: [
      { id: 'format', label: 'Target format', kind: 'segs',
        values: [['mp3','MP3'],['aac','AAC'],['wav','WAV'],['flac','FLAC'],['ogg','OGG'],['opus','Opus'],['m4a','M4A']], default: 'mp3' },
    ],
    run: { label: 'Convert', icon: 'fa-headphones', via: 'browser', handler: 'audioConvert' },
  },
  'audio-normalize': {
    intake: { kind: 'file', accept: 'audio/*' },
    options: [
      { id: 'preset', label: 'Preset', kind: 'segs',
        values: [['ebu-r128','EBU R128'],['podcast','Podcast'],['music','Music'],['voice','Voice']], default: 'ebu-r128' },
    ],
    run: { label: 'Normalize', icon: 'fa-balance-scale-right', via: 'server', endpoint: '/api/tools/audio-normalize', method: 'POST', formData: true, outputFile: true },
  },
  'audio-trim': {
    intake: { kind: 'file', accept: 'audio/*' },
    options: [
      { id: 'start', label: 'Start (s)', kind: 'number', default: 0 },
      { id: 'end',   label: 'End (s)',   kind: 'number', default: 30 },
    ],
    run: { label: 'Trim', icon: 'fa-cut', via: 'server', endpoint: '/api/tools/audio-trim', method: 'POST', formData: true, outputFile: true },
  },
  'audio-separate': {
    intake: { kind: 'file', accept: 'audio/*' },
    options: [
      { id: 'stems', label: 'Stems', kind: 'segs', values: [['2','Vocal / Inst'],['4','4-stem']], default: '2' },
    ],
    run: { label: 'Separate', icon: 'fa-guitar', via: 'server', endpoint: '/api/tools/audio-separate', method: 'POST', formData: true, outputFile: true, longRunning: true },
  },

  // ── Document ───────────────────────────────────────────────────────────
  'pdf-compress': {
    intake: { kind: 'file', accept: '.pdf,application/pdf' },
    options: [
      { id: 'preset', label: 'Preset', kind: 'segs',
        values: [['screen','Screen'],['ebook','eBook'],['printer','Print']], default: 'ebook' },
    ],
    run: { label: 'Compress PDF', icon: 'fa-file-pdf', via: 'server', endpoint: '/api/tools/pdf-compress', method: 'POST', formData: true, outputFile: true },
  },
  'pdf-merge': {
    intake: { kind: 'file', accept: '.pdf', multiple: true },
    options: [],
    run: { label: 'Merge PDFs', icon: 'fa-object-group', via: 'server', endpoint: '/api/tools/pdf-merge', method: 'POST', formData: true, outputFile: true },
  },
  'pdf-split': {
    intake: { kind: 'file', accept: '.pdf' },
    options: [
      { id: 'range', label: 'Page range', kind: 'text', placeholder: 'e.g. 1-3,5,7-9' },
    ],
    run: { label: 'Split', icon: 'fa-scissors', via: 'server', endpoint: '/api/tools/pdf-split', method: 'POST', formData: true, outputFile: true },
  },
  'pdf-to-images': {
    intake: { kind: 'file', accept: '.pdf' },
    options: [
      { id: 'format', label: 'Format', kind: 'segs', values: [['png','PNG'],['jpg','JPG'],['webp','WebP'],['tiff','TIFF']], default: 'png' },
      { id: 'dpi',    label: 'DPI',    kind: 'segs', values: [['72','72'],['150','150'],['300','300']], default: '150' },
    ],
    run: { label: 'Convert', icon: 'fa-images', via: 'server', endpoint: '/api/tools/pdf-to-images', method: 'POST', formData: true, outputFile: true },
  },
  'pdf-to-word': {
    intake: { kind: 'file', accept: '.pdf' },
    options: [],
    run: { label: 'Convert to Word', icon: 'fa-file-word', via: 'server', endpoint: '/api/tools/pdf-to-word', method: 'POST', formData: true, outputFile: true },
  },
  'word-to-pdf': {
    intake: { kind: 'file', accept: '.doc,.docx,.odt' },
    options: [],
    run: { label: 'Convert to PDF', icon: 'fa-file-pdf', via: 'server', endpoint: '/api/tools/word-to-pdf', method: 'POST', formData: true, outputFile: true },
  },
  'images-to-pdf': {
    intake: { kind: 'file', accept: 'image/*', multiple: true },
    options: [
      { id: 'size', label: 'Page size', kind: 'segs', values: [['a4','A4'],['letter','Letter'],['legal','Legal'],['fit','Fit']], default: 'a4' },
    ],
    run: { label: 'Make PDF', icon: 'fa-file-image', via: 'server', endpoint: '/api/tools/images-to-pdf', method: 'POST', formData: true, outputFile: true },
  },
  'markdown-to-pdf': {
    intake: { kind: 'text', placeholder: '# Heading\n\nWrite Markdown here…', minChars: 10 },
    options: [
      { id: 'theme', label: 'Theme', kind: 'segs', values: [['github','GitHub'],['obsidian','Obsidian'],['minimal','Minimal']], default: 'github' },
    ],
    run: { label: 'Render PDF', icon: 'fa-markdown', via: 'server', endpoint: '/api/tools/markdown-to-pdf', method: 'POST', formData: true, outputFile: true },
  },

  // ── AI ─────────────────────────────────────────────────────────────────
  'ai-coverage': {
    intake: { kind: 'dualFileOrText',
      labelA: 'Source material', placeholderA: 'Paste lecture notes, textbook, slides…',
      acceptA: '.pdf,.docx,.pptx,.txt,.md,.epub',
      labelB: 'Your notes',     placeholderB: 'Paste the notes you want to check…',
      acceptB: '.pdf,.docx,.txt,.md' },
    options: [],
    run: { label: 'Check coverage', icon: 'fa-chart-bar', via: 'server',
           endpoint: '/api/tools/ai-coverage-standalone', method: 'POST', formData: true, outputJson: true, aiBadge: true,
           buildFormData: (s) => {
             const f = new FormData();
             if (s.fileA) f.append('source_file', s.fileA);
             else f.append('source_text', s.textA || '');
             if (s.fileB) f.append('notes_file', s.fileB);
             else f.append('notes_text', s.textB || '');
             return f;
           }},
  },

  'ai-study-notes': {
    intake: { kind: 'fileOrText', accept: '.pdf,.docx,.pptx,.txt,.md,.epub', placeholder: 'Paste lecture text, or upload a file…', minChars: 50, multiple: true },
    options: [
      { id: 'depth',     label: 'Depth',          kind: 'segs', values: [['simple','Simple'],['indepth','In-depth'],['eli6','ELI6']], default: 'indepth' },
      { id: 'format',    label: 'Output format',  kind: 'segs', values: [['markdown','Markdown'],['plain','Plain text']],            default: 'markdown' },
      { id: 'math_fmt',  label: 'Math notation',  kind: 'segs', values: [['latex','LaTeX'],['dollar','$ syntax'],['none','None']],   default: 'dollar' },
      { id: 'numbering', label: 'Heading numbers',kind: 'segs', values: [['full','All levels'],['titles','Top only'],['none','None']],default: 'titles' },
    ],
    run: { label: 'Generate notes', icon: 'fa-brain', via: 'server', endpoint: '/api/tools/ai-study-notes', method: 'POST', formData: true, outputFile: true, longRunning: true, aiBadge: true },
  },

  'ai-improve-notes': {
    intake: { kind: 'textarea', placeholder: 'Paste your existing study notes…', minChars: 200, mono: false },
    options: [],
    run: { label: 'Improve notes', icon: 'fa-sync-alt', via: 'server', endpoint: '/api/tools/ai-improve-notes', method: 'POST', formData: true, outputText: true, aiBadge: true,
           buildFormData: (s) => { const f = new FormData(); f.append('current_notes', s.text || ''); f.append('feedback', '{}'); return f; } },
  },

  // bulk-install: kept on its legacy panel (app .bat installer generator).

  'ai-paraphrase': {
    intake: { kind: 'text', placeholder: 'Paste the text you want rewritten…', minChars: 20 },
    options: [
      { id: 'tone', label: 'Tone', kind: 'segs',
        values: [['formal','Formal'],['casual','Casual'],['simplified','Simple'],['academic','Academic'],['concise','Concise'],['expand','Expand']],
        default: 'formal' },
    ],
    run: { label: 'Paraphrase', icon: 'fa-sync-alt', via: 'server', endpoint: '/api/tools/ai-paraphrase', method: 'POST', formData: true, outputText: true, aiBadge: true },
  },
  'ai-flashcards': {
    intake: { kind: 'fileOrText', accept: '.pdf,.docx,.pptx,.txt,.md,.epub', placeholder: 'Paste source text…', minChars: 50 },
    options: [
      { id: 'count', label: 'How many', kind: 'segs', values: [['3','3'],['5','5'],['10','10'],['20','20'],['max','Max']], default: '10' },
    ],
    run: { label: 'Generate flashcards', icon: 'fa-clone', via: 'server', endpoint: '/api/tools/ai-flashcards', method: 'POST', formData: true, outputJson: true, aiBadge: true },
  },
  'ai-quiz': {
    intake: { kind: 'fileOrText', accept: '.pdf,.docx,.pptx,.txt,.md,.epub', placeholder: 'Paste source text…', minChars: 50 },
    options: [
      { id: 'count',      label: 'Questions',  kind: 'segs', values: [['3','3'],['5','5'],['10','10'],['15','15'],['20','20']], default: '10' },
      { id: 'difficulty', label: 'Difficulty', kind: 'segs', values: [['easy','Easy'],['medium','Medium'],['hard','Hard']], default: 'medium' },
    ],
    run: { label: 'Build quiz', icon: 'fa-question-circle', via: 'server', endpoint: '/api/tools/ai-quiz', method: 'POST', formData: true, outputJson: true, aiBadge: true },
  },
  'mind-map': {
    intake: { kind: 'text', placeholder: 'Paste content to map…', minChars: 50 },
    options: [],
    run: { label: 'Generate map', icon: 'fa-project-diagram', via: 'server', endpoint: '/api/mind-map', method: 'POST', jsonBody: true, outputJson: true, aiBadge: true,
           buildBody: (state) => ({ text: state.text || '' }) },
  },
  'youtube-summary': {
    intake: { kind: 'url', placeholder: 'YouTube URL…', validate: /youtube\.com|youtu\.be/ },
    options: [
      { id: 'length', label: 'Length', kind: 'segs', values: [['short','Short'],['medium','Medium'],['detailed','Detailed']], default: 'medium' },
    ],
    run: { label: 'Summarise', icon: 'fa-youtube', via: 'server', endpoint: '/api/youtube-summary', method: 'POST', formData: true, outputText: true, aiBadge: true },
  },

  // ── Utility (browser-only quick ones) ─────────────────────────────────
  'qr-generate': {
    intake: { kind: 'text', placeholder: 'Text, URL, email, phone number…' },
    options: [
      { id: 'size',  label: 'Size',     kind: 'segs', values: [['small','Small'],['medium','Medium'],['large','Large']], default: 'medium' },
      { id: 'level', label: 'Error correction', kind: 'segs', values: [['L','Low'],['M','Med'],['Q','Q'],['H','High']], default: 'M' },
    ],
    run: { label: 'Generate QR', icon: 'fa-qrcode', via: 'browser', handler: 'qrGenerate' },
  },
  'base64': {
    intake: { kind: 'textarea', placeholder: 'Text or base64 to convert…' },
    options: [
      { id: 'mode', label: 'Mode', kind: 'segs', values: [['encode','Encode'],['decode','Decode']], default: 'encode' },
    ],
    run: { label: 'Convert', icon: 'fa-code', via: 'browser', handler: 'base64' },
  },
  'json-format': {
    intake: { kind: 'textarea', placeholder: '{ "paste": "your JSON here" }', mono: true },
    options: [
      { id: 'mode', label: 'Mode', kind: 'segs', values: [['pretty','Pretty'],['minify','Minify'],['validate','Validate']], default: 'pretty' },
      { id: 'indent', label: 'Indent', kind: 'segs', values: [['2','2'],['4','4'],['tab','Tab']], default: '2' },
    ],
    run: { label: 'Format', icon: 'fa-file-code', via: 'browser', handler: 'jsonFormat' },
  },
  'url-encode': {
    intake: { kind: 'textarea', placeholder: 'Text or URL to encode/decode…' },
    options: [
      { id: 'mode', label: 'Mode', kind: 'segs', values: [['encode','Encode'],['decode','Decode']], default: 'encode' },
    ],
    run: { label: 'Convert', icon: 'fa-link', via: 'browser', handler: 'urlEncode' },
  },
  'word-counter': {
    intake: { kind: 'textarea', placeholder: 'Paste text to count…' },
    options: [],
    run: { label: 'Recount', icon: 'fa-align-left', via: 'browser', handler: 'wordCount' },
  },
  'unix-date': {
    intake: { kind: 'text', placeholder: 'Unix epoch or ISO date…' },
    options: [],
    run: { label: 'Convert', icon: 'fa-clock', via: 'browser', handler: 'unixDate' },
  },
  'password-gen': {
    intake: null,
    options: [
      { id: 'length', label: 'Length', kind: 'slider', min: 8, max: 64, step: 1, default: 20, format: v => v+' chars' },
      { id: 'lower',  label: 'a-z',    kind: 'toggle', default: true },
      { id: 'upper',  label: 'A-Z',    kind: 'toggle', default: true },
      { id: 'digits', label: '0-9',    kind: 'toggle', default: true },
      { id: 'symbols',label: '!@#$',   kind: 'toggle', default: false },
    ],
    run: { label: 'Generate', icon: 'fa-key', via: 'browser', handler: 'passwordGen' },
  },
  'uuid-gen': {
    intake: null,
    options: [
      { id: 'kind', label: 'Type', kind: 'segs', values: [['v4','UUID v4'],['v7','UUID v7'],['ulid','ULID'],['nanoid','NanoID']], default: 'v4' },
      { id: 'count', label: 'Count', kind: 'segs', values: [['1','1'],['10','10'],['100','100']], default: '1' },
    ],
    run: { label: 'Generate', icon: 'fa-id-badge', via: 'browser', handler: 'uuidGen' },
  },
  'color-convert': {
    intake: { kind: 'text', placeholder: 'HEX, RGB, HSL, OKLCH…' },
    options: [],
    run: { label: 'Convert', icon: 'fa-palette', via: 'browser', handler: 'colorConvert' },
  },
  'diff-checker': {
    intake: { kind: 'dualTextarea', placeholderA: 'Original text…', placeholderB: 'Changed text…' },
    options: [
      { id: 'unit', label: 'Granularity', kind: 'segs', values: [['line','Line'],['word','Word'],['char','Char']], default: 'line' },
    ],
    run: { label: 'Diff', icon: 'fa-code-compare', via: 'browser', handler: 'diffCheck' },
  },
  'markdown-preview': {
    intake: { kind: 'textarea', placeholder: '# Live preview', mono: true },
    options: [],
    run: { label: 'Refresh', icon: 'fa-markdown', via: 'browser', handler: 'mdPreview' },
  },
  'code-beautify': {
    intake: { kind: 'textarea', placeholder: 'Paste code…', mono: true },
    options: [
      { id: 'lang', label: 'Language', kind: 'segs', values: [['js','JS'],['html','HTML'],['css','CSS'],['json','JSON'],['xml','XML'],['sql','SQL']], default: 'js' },
      { id: 'indent', label: 'Indent', kind: 'segs', values: [['2','2'],['4','4'],['tab','Tab']], default: '2' },
    ],
    run: { label: 'Beautify', icon: 'fa-indent', via: 'browser', handler: 'codeBeautify' },
  },
  'regex-tester': {
    intake: { kind: 'textarea', placeholder: 'Sample text…' },
    options: [
      { id: 'pattern', label: 'Pattern', kind: 'text', placeholder: '/pattern/g' },
    ],
    run: { label: 'Test', icon: 'fa-asterisk', via: 'browser', handler: 'regexTest' },
  },

  // ── Server-side utility ────────────────────────────────────────────────
  'hash-generate': {
    intake: { kind: 'file', accept: '*/*' },
    options: [],
    run: { label: 'Generate hashes', icon: 'fa-fingerprint', via: 'server', endpoint: '/api/tools/hash-generate', method: 'POST', formData: true, outputJson: true },
  },
  'archive-extract': {
    intake: { kind: 'file', accept: '.zip,.7z,.rar,.tar,.tar.gz,.tgz,.tar.bz2,.iso,.dmg,.cbz,.cbr' },
    options: [],
    run: { label: 'Extract', icon: 'fa-box-open', via: 'server', endpoint: '/api/tools/archive-extract', method: 'POST', formData: true, outputFile: true },
  },
  'csv-json': {
    intake: { kind: 'fileOrText', accept: '.csv,.tsv,.json', placeholder: 'Paste CSV or JSON…' },
    options: [
      { id: 'direction', label: 'Direction', kind: 'segs', values: [['auto','Auto'],['csv-json','CSV → JSON'],['json-csv','JSON → CSV']], default: 'auto' },
    ],
    run: { label: 'Convert', icon: 'fa-table', via: 'server', endpoint: '/api/tools/csv-json', method: 'POST', formData: true, outputText: true },
  },
  'citation-gen': {
    intake: { kind: 'url', placeholder: 'Source URL or DOI…' },
    options: [
      { id: 'style', label: 'Style', kind: 'segs', values: [['apa','APA'],['mla','MLA'],['chicago','Chicago'],['harvard','Harvard'],['ieee','IEEE']], default: 'apa' },
    ],
    run: { label: 'Generate citation', icon: 'fa-quote-left', via: 'server', endpoint: '/api/tools/citation-generate', method: 'POST', formData: true, outputText: true,
           buildFormData: (state) => { const f = new FormData(); f.append('source_type','url'); f.append('url', state.url||''); f.append('style', state.options.style||'apa'); return f; } },
  },

  'image-heic': {
    intake: { kind: 'file', accept: '.heic,.heif,image/heic,image/heif', multiple: true, maxMb: 100 },
    options: [
      { id: 'format',  label: 'Output',  kind: 'segs', values: [['jpg','JPG'],['png','PNG'],['webp','WebP']], default: 'jpg' },
      { id: 'quality', label: 'Quality', kind: 'slider', min: 60, max: 100, step: 5, default: 90, format: v => v + '%' },
    ],
    run: { label: 'Convert HEIC', icon: 'fa-mobile-alt', via: 'server',
           endpoint: '/api/tools/image-convert', method: 'POST', formData: true, outputFile: true },
  },

  // downloader: kept on its legacy panel — it has rich analyze/format/
  // quality detection that the simple spec form can't replicate. The
  // legacy #urlInput is what QuickBar's carry-over targets.
};
