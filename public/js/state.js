// ═══════════════════════════════════════════════════════════════════════════
// STATE & CONFIG
// ═══════════════════════════════════════════════════════════════════════════

const state = {
    currentTool: 'landing',
    url: '',
    platform: null,
    mediaInfo: null,
    downloadId: null,
    selectedFormat: 'mp3',
    selectedQuality: 'best',
    isDownloading: false,
    playlistItems: [],
    batchResults: [],
    downloadLastProgress: 0,
    pollInterval: null,
    files: {},
    multiFiles: {},
    cropRect: null,
    outputFormats: {},
    presets: {},
    jobPolls: {},
    droppedFiles: [],
    droppedCategory: null,
    pendingBatch: {},
    resolvingTitles: false,
    resolveAborted: false,
    aspectLock: false,
};

// DOM helpers — $ accepts a plain id (no #) or querySelector string
const $ = id => document.getElementById(id) || document.querySelector(id);
const $$ = sel => document.querySelectorAll(sel);

// File → tool mappings for quick-action modal
const FILE_TOOL_MAP = {
    'image': [
        { id: 'image-compress',      label: 'Compress',        icon: 'fas fa-compress-alt' },
        { id: 'image-resize',        label: 'Resize',           icon: 'fas fa-expand-arrows-alt' },
        { id: 'image-convert',       label: 'Convert',          icon: 'fas fa-exchange-alt' },
        { id: 'image-crop',          label: 'Crop',             icon: 'fas fa-crop-alt' },
        { id: 'image-bg-remove',     label: 'Remove BG',        icon: 'fas fa-magic' },
        { id: 'image-watermark',     label: 'Watermark',        icon: 'fas fa-stamp' },
        { id: 'metadata-strip',      label: 'Strip Metadata',   icon: 'fas fa-eraser' },
        { id: 'color-palette',       label: 'Color Palette',    icon: 'fas fa-eye-dropper' },
        { id: 'favicon-generate',    label: 'Make Favicon',     icon: 'fas fa-icons' },
        { id: 'screenshot-annotate', label: 'Annotate',         icon: 'fas fa-pen-nib' },
        { id: 'redact',              label: 'Redact',           icon: 'fas fa-user-secret' },
        { id: 'hash-generate',       label: 'Hash File',        icon: 'fas fa-fingerprint' },
    ],
    'gif': [
        { id: 'gif-to-video',      label: 'To Video',       icon: 'fas fa-film' },
        { id: 'gif-frame-remove',  label: 'Remove Frames',  icon: 'fas fa-trash-alt' },
        { id: 'video-frame',       label: 'Extract Frame',  icon: 'fas fa-camera' },
        { id: 'image-compress',  label: 'Compress',      icon: 'fas fa-compress-alt' },
        { id: 'image-convert',   label: 'Convert',       icon: 'fas fa-exchange-alt' },
        { id: 'hash-generate',   label: 'Hash File',     icon: 'fas fa-fingerprint' },
    ],
    'video': [
        { id: 'video-compress',      label: 'Compress',        icon: 'fas fa-compress' },
        { id: 'video-convert',       label: 'Convert',         icon: 'fas fa-exchange-alt' },
        { id: 'video-trim',          label: 'Trim',            icon: 'fas fa-cut' },
        { id: 'video-extract-audio', label: 'Extract Audio',   icon: 'fas fa-music' },
        { id: 'video-to-gif',        label: 'To GIF',          icon: 'fas fa-magic' },
        { id: 'gif-frame-remove',    label: 'Remove Frames',   icon: 'fas fa-trash-alt' },
        { id: 'video-speed',         label: 'Change Speed',    icon: 'fas fa-tachometer-alt' },
        { id: 'video-remove-audio',  label: 'Remove Audio',    icon: 'fas fa-volume-mute' },
        { id: 'video-frame',         label: 'Extract Frame',   icon: 'fas fa-film' },
        { id: 'video-stabilize',     label: 'Stabilize',       icon: 'fas fa-crosshairs' },
        { id: 'subtitle-extract',    label: 'Extract Subtitles', icon: 'fas fa-closed-captioning' },
        { id: 'redact',              label: 'Redact',          icon: 'fas fa-user-secret' },
        { id: 'hash-generate',       label: 'Hash File',       icon: 'fas fa-fingerprint' },
    ],
    'audio': [
        { id: 'audio-convert',   label: 'Convert',      icon: 'fas fa-headphones' },
        { id: 'audio-normalize', label: 'Normalize',    icon: 'fas fa-balance-scale-right' },
        { id: 'audio-trim',      label: 'Trim',         icon: 'fas fa-cut' },
        { id: 'metadata-strip',  label: 'Strip Metadata', icon: 'fas fa-eraser' },
        { id: 'hash-generate',   label: 'Hash File',    icon: 'fas fa-fingerprint' },
    ],
    'pdf': [
        { id: 'pdf-compress',    label: 'Compress',     icon: 'fas fa-file-pdf' },
        { id: 'pdf-merge',       label: 'Merge',        icon: 'fas fa-layer-group' },
        { id: 'pdf-split',       label: 'Split',        icon: 'fas fa-scissors' },
        { id: 'pdf-to-images',   label: 'To Images',    icon: 'fas fa-images' },
        { id: 'ai-study-notes',  label: 'Study Notes',  icon: 'fas fa-brain' },
        { id: 'ai-flashcards',   label: 'Flashcards',   icon: 'fas fa-clone' },
        { id: 'ai-quiz',         label: 'Quiz Me',      icon: 'fas fa-question-circle' },
        { id: 'hash-generate',   label: 'Hash File',    icon: 'fas fa-fingerprint' },
    ],
    'markdown': [
        { id: 'markdown-to-pdf',  label: 'To PDF',    icon: 'fab fa-markdown' },
        { id: 'markdown-preview', label: 'Preview',   icon: 'fas fa-eye' },
        { id: 'hash-generate',    label: 'Hash File', icon: 'fas fa-fingerprint' },
    ],
    'csv': [
        { id: 'csv-json',       label: 'To JSON',   icon: 'fas fa-exchange-alt' },
        { id: 'hash-generate',  label: 'Hash File', icon: 'fas fa-fingerprint' },
    ],
    'json': [
        { id: 'csv-json',       label: 'To CSV',    icon: 'fas fa-exchange-alt' },
        { id: 'hash-generate',  label: 'Hash File', icon: 'fas fa-fingerprint' },
    ],
    'archive': [
        { id: 'archive-extract', label: 'Extract',   icon: 'fas fa-box-open' },
        { id: 'hash-generate',   label: 'Hash File', icon: 'fas fa-fingerprint' },
    ],
    'document': [
        { id: 'ai-study-notes', label: 'Study Notes', icon: 'fas fa-brain' },
        { id: 'ai-flashcards',  label: 'Flashcards',  icon: 'fas fa-clone' },
        { id: 'ai-quiz',        label: 'Quiz Me',     icon: 'fas fa-question-circle' },
        { id: 'hash-generate',  label: 'Hash File',   icon: 'fas fa-fingerprint' },
    ],
};
