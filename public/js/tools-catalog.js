// ════════════════════════════════════════════════════════════════════════
// LUMA TOOLS — full catalog for the v2 shell
// Every entry's `id` matches an existing switchTool(id) target + a real
// <div class="tool-panel" id="tool-{id}"> in index.html, so the existing
// per-tool JS continues to drive each form.
// ════════════════════════════════════════════════════════════════════════

window.LUMA_CATEGORIES = [
  {
    id: 'ai',
    label: 'AI & Study',
    short: 'AI',
    icon: 'sparkles',
    blurb: 'For class, not for cheating.',
    tools: [
      { id: 'ai-study-notes',  name: 'Study Notes',     where: 'server', desc: '3-pass pipeline, full coverage.', fa: 'fa-brain' },
      { id: 'ai-coverage',     name: 'Coverage Check',  where: 'server', desc: 'Score notes against source. Find gaps.', fa: 'fa-chart-bar' },
      { id: 'ai-flashcards',   name: 'Flashcards',      where: 'server', desc: 'Q&A pairs from any text.',         fa: 'fa-clone' },
      { id: 'ai-quiz',         name: 'Practice Quiz',   where: 'server', desc: 'Multiple choice generator.',       fa: 'fa-question-circle' },
      { id: 'ai-paraphrase',   name: 'Paraphraser',     where: 'server', desc: 'Rewrite in different tones.',      fa: 'fa-sync-alt' },
      { id: 'citation-gen',    name: 'Citation Gen',    where: 'server', desc: 'APA, MLA, Chicago.',               fa: 'fa-quote-left' },
      { id: 'mind-map',        name: 'Mind Map',        where: 'server', desc: 'Structured concept map.',          fa: 'fa-project-diagram' },
      { id: 'youtube-summary', name: 'YouTube Summary', where: 'server', desc: 'Summarize by transcript.',         fa: 'fa-youtube' },
    ],
  },
  {
    id: 'download',
    label: 'Download',
    short: 'DLD',
    icon: 'download',
    blurb: 'Paste a link. Get the media.',
    tools: [
      { id: 'downloader',   name: 'Media Downloader', where: 'server',  desc: 'YouTube, TikTok, Instagram + 10 more.', fa: 'fa-download' },
      { id: 'bulk-install', name: 'Bulk Installer',   where: 'browser', desc: 'Queue many URLs at once.',              fa: 'fa-boxes-stacked' },
    ],
  },
  {
    id: 'image',
    label: 'Image',
    short: 'IMG',
    icon: 'image',
    blurb: 'Edit photos without uploading.',
    tools: [
      { id: 'image-compress',     name: 'Compress',           where: 'browser', desc: 'Shrink JPG, PNG, WebP.',          fa: 'fa-compress-alt' },
      { id: 'image-heic',         name: 'HEIC → JPG',         where: 'server',  desc: 'Convert iPhone HEIC photos to JPG.', fa: 'fa-mobile-alt' },
      { id: 'image-resize',       name: 'Resize',             where: 'browser', desc: 'Scale by width or height.',       fa: 'fa-expand-arrows-alt' },
      { id: 'image-convert',      name: 'Convert',            where: 'browser', desc: 'PNG, JPG, WebP, BMP, TIFF.',      fa: 'fa-exchange-alt' },
      { id: 'image-crop',         name: 'Crop',               where: 'browser', desc: 'Aspect-ratio presets.',           fa: 'fa-crop-alt' },
      { id: 'image-watermark',    name: 'Watermark',          where: 'server',  desc: 'Burn-in text watermark.',         fa: 'fa-stamp' },
      { id: 'image-bg-remove',    name: 'Remove background',  where: 'server',  desc: 'AI background removal.',          fa: 'fa-user-alt' },
      { id: 'image-upscale',      name: 'Upscale',            where: 'server',  desc: 'AI 2–4× upscaling.',              fa: 'fa-expand' },
      { id: 'ocr',                name: 'Extract Text (OCR)', where: 'server',  desc: 'Pull text from an image.',        fa: 'fa-font' },
      { id: 'metadata-strip',     name: 'Strip metadata',     where: 'server',  desc: 'Remove all EXIF data.',           fa: 'fa-eraser' },
      { id: 'favicon-generate',   name: 'Favicon Generator',  where: 'server',  desc: 'Multi-size favicon set.',         fa: 'fa-star' },
      { id: 'redact',             name: 'Privacy Redaction',  where: 'browser', desc: 'Blur sensitive regions.',         fa: 'fa-user-secret' },
      { id: 'screenshot-annotate',name: 'Screenshot Annotator',where: 'browser',desc: 'Draw, text, arrows on a shot.',   fa: 'fa-pencil-alt' },
      { id: 'color-palette',      name: 'Color Palette',      where: 'browser', desc: 'Extract dominant colors.',        fa: 'fa-eye-dropper' },
    ],
  },
  {
    id: 'video',
    label: 'Video',
    short: 'VID',
    icon: 'video',
    blurb: 'Trim, convert, stabilize.',
    tools: [
      { id: 'video-compress',     name: 'Compress',       where: 'server', desc: 'Light, medium, heavy.',          fa: 'fa-file-video' },
      { id: 'video-trim',         name: 'Trim / Cut',     where: 'server', desc: 'Cut by time range.',             fa: 'fa-cut' },
      { id: 'video-convert',      name: 'Convert',        where: 'server', desc: 'MP4, WebM, MKV, AVI, MOV.',      fa: 'fa-film' },
      { id: 'video-extract-audio',name: 'Extract Audio',  where: 'server', desc: 'Rip the audio track.',           fa: 'fa-volume-up' },
      { id: 'video-to-gif',       name: 'To GIF',         where: 'server', desc: 'Make an animated GIF.',          fa: 'fa-magic' },
      { id: 'gif-to-video',       name: 'GIF to Video',   where: 'server', desc: 'Convert GIF to MP4.',            fa: 'fa-photo-video' },
      { id: 'gif-frame-remove',   name: 'Remove Frames',  where: 'server', desc: 'Drop specific frames.',          fa: 'fa-trash-alt' },
      { id: 'video-remove-audio', name: 'Remove Audio',   where: 'server', desc: 'Mute the file.',                 fa: 'fa-volume-mute' },
      { id: 'video-speed',        name: 'Speed Change',   where: 'server', desc: 'Slow-mo or fast-forward.',       fa: 'fa-tachometer-alt' },
      { id: 'video-frame',        name: 'Frame Extract',  where: 'server', desc: 'Save a single frame.',           fa: 'fa-camera' },
      { id: 'video-stabilize',    name: 'Stabilize',      where: 'server', desc: 'Reduce camera shake.',           fa: 'fa-hand-paper' },
      { id: 'subtitle-extract',   name: 'Subtitles',      where: 'server', desc: 'Extract subtitle tracks.',       fa: 'fa-closed-captioning' },
    ],
  },
  {
    id: 'audio',
    label: 'Audio',
    short: 'AUD',
    icon: 'audio',
    blurb: 'Convert, trim, normalize.',
    tools: [
      { id: 'audio-convert',   name: 'Convert',        where: 'browser', desc: 'MP3, AAC, WAV, FLAC, OGG.',          fa: 'fa-headphones' },
      { id: 'audio-normalize', name: 'Normalize',      where: 'server',  desc: 'Even out loudness.',                 fa: 'fa-balance-scale-right' },
      { id: 'audio-trim',      name: 'Trim',           where: 'server',  desc: 'Cut by time range.',                 fa: 'fa-cut' },
      { id: 'audio-separate',  name: 'Vocal Separator',where: 'server',  desc: 'Split vocals from instrumental.',    fa: 'fa-guitar' },
    ],
  },
  {
    id: 'document',
    label: 'Document',
    short: 'DOC',
    icon: 'doc',
    blurb: 'PDFs, Word docs, Markdown.',
    tools: [
      { id: 'pdf-compress',     name: 'PDF Compress',    where: 'server', desc: 'Shrink via Ghostscript.',           fa: 'fa-file-pdf' },
      { id: 'pdf-merge',        name: 'PDF Merge',       where: 'server', desc: 'Combine many into one.',            fa: 'fa-object-group' },
      { id: 'pdf-split',        name: 'PDF Split',       where: 'server', desc: 'Extract a page range.',             fa: 'fa-scissors' },
      { id: 'pdf-to-images',    name: 'PDF to Images',   where: 'server', desc: 'Pages as PNG, JPG, TIFF.',          fa: 'fa-images' },
      { id: 'pdf-to-word',      name: 'PDF to Word',     where: 'server', desc: 'Convert to editable .docx.',        fa: 'fa-file-word' },
      { id: 'word-to-pdf',      name: 'Word to PDF',     where: 'server', desc: 'Render .docx as PDF.',              fa: 'fa-file-pdf' },
      { id: 'images-to-pdf',    name: 'Images to PDF',   where: 'server', desc: 'Combine images into one PDF.',      fa: 'fa-file-image' },
      { id: 'markdown-to-pdf',  name: 'Markdown to PDF', where: 'server', desc: 'Pandoc, Obsidian-compatible.',      fa: 'fa-markdown' },
    ],
  },
  {
    id: 'business',
    label: 'Business',
    short: 'BIZ',
    icon: 'doc',
    blurb: 'Resumes, invoices, forms.',
    tools: [
      { id: 'resume-builder', name: 'Resume Builder',     where: 'browser', desc: 'Modern templates, PDF export.', fa: 'fa-file-alt' },
      { id: 'invoice-gen',    name: 'Invoice Generator',  where: 'browser', desc: 'Branded PDF invoices.',         fa: 'fa-file-invoice-dollar' },
    ],
  },
  {
    id: 'utility',
    label: 'Utility',
    short: 'UTL',
    icon: 'wrench',
    blurb: 'Quick everyday helpers.',
    tools: [
      { id: 'qr-generate',      name: 'QR Code',           where: 'browser', desc: 'Make a QR from text or URL.',       fa: 'fa-qrcode' },
      { id: 'hash-generate',    name: 'File Hash',         where: 'server',  desc: 'MD5, SHA-1, SHA-256, SHA-512.',     fa: 'fa-fingerprint' },
      { id: 'archive-extract',  name: 'Extract Archive',   where: 'server',  desc: 'ZIP, 7Z, RAR, TAR, ISO…',          fa: 'fa-box-open' },
      { id: 'base64',           name: 'Base64',            where: 'browser', desc: 'Encode or decode text.',            fa: 'fa-code' },
      { id: 'json-format',      name: 'JSON Formatter',    where: 'browser', desc: 'Format, minify, validate.',         fa: 'fa-file-code' },
      { id: 'color-convert',    name: 'Color Converter',   where: 'browser', desc: 'HEX, RGB, HSL, HSV, CMYK.',         fa: 'fa-palette' },
      { id: 'markdown-preview', name: 'Markdown Preview',  where: 'browser', desc: 'Live preview + HTML export.',       fa: 'fa-markdown' },
      { id: 'diff-checker',     name: 'Diff Checker',      where: 'browser', desc: 'Compare two blocks of text.',       fa: 'fa-code-compare' },
      { id: 'word-counter',     name: 'Word Counter',      where: 'browser', desc: 'Words, characters, sentences.',     fa: 'fa-align-left' },
      { id: 'csv-json',         name: 'CSV ↔ JSON',        where: 'server',  desc: 'Convert spreadsheet to JSON.',      fa: 'fa-table' },
      { id: 'unix-date',        name: 'Unix ↔ Date',       where: 'browser', desc: 'Epoch <-> human conversion.',       fa: 'fa-clock' },
      { id: 'regex-tester',     name: 'Regex Tester',      where: 'browser', desc: 'Test patterns live.',               fa: 'fa-asterisk' },
      { id: 'code-beautify',    name: 'Code Beautify',     where: 'browser', desc: 'Format JS, HTML, CSS, JSON.',       fa: 'fa-indent' },
      { id: 'uuid-gen',         name: 'UUID Generator',    where: 'browser', desc: 'v1, v4, ULID, NanoID.',             fa: 'fa-id-badge' },
      { id: 'url-encode',       name: 'URL Encode',        where: 'browser', desc: 'Encode / decode URL params.',       fa: 'fa-link' },
      { id: 'password-gen',     name: 'Password Generator',where: 'browser', desc: 'Strong, configurable passwords.',   fa: 'fa-key' },
      { id: 'jwt-decode',       name: 'JWT Generator + Decoder', where: 'browser', desc: 'Sign and inspect JWTs.',     fa: 'fa-shield-alt' },
    ],
  },
];

// Flat list for search + display counts
window.LUMA_ALL_TOOLS = window.LUMA_CATEGORIES.flatMap(cat =>
  cat.tools.map(t => ({ ...t, cat: cat.id, catLabel: cat.label, catShort: cat.short, catIcon: cat.icon }))
);

// Quick lookup by id
window.LUMA_TOOL_BY_ID = Object.fromEntries(window.LUMA_ALL_TOOLS.map(t => [t.id, t]));

// Featured tools surfaced on Home
window.LUMA_FEATURED = ['downloader', 'ai-study-notes', 'image-compress'];
