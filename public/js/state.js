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
    pollInterval: null,
    files: {},
    multiFiles: {},
    cropRect: null,
    outputFormats: {},
    presets: {},
    jobPolls: {},
    droppedFiles: [],
    droppedCategory: null,
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
        { id: 'image-compress',  label: 'Compress',        icon: 'fas fa-compress-alt' },
        { id: 'image-resize',    label: 'Resize',           icon: 'fas fa-expand-arrows-alt' },
        { id: 'image-convert',   label: 'Convert',          icon: 'fas fa-exchange-alt' },
        { id: 'image-crop',      label: 'Crop',             icon: 'fas fa-crop-alt' },
        { id: 'image-bg-remove', label: 'Remove BG',        icon: 'fas fa-magic' },
        { id: 'metadata-strip',  label: 'Strip Metadata',   icon: 'fas fa-eraser' },
    ],
    'video': [
        { id: 'video-compress',      label: 'Compress',       icon: 'fas fa-compress' },
        { id: 'video-convert',       label: 'Convert',        icon: 'fas fa-exchange-alt' },
        { id: 'video-trim',          label: 'Trim',           icon: 'fas fa-cut' },
        { id: 'video-extract-audio', label: 'Extract Audio',  icon: 'fas fa-music' },
        { id: 'video-to-gif',        label: 'To GIF',         icon: 'fas fa-magic' },
        { id: 'video-speed',         label: 'Speed',          icon: 'fas fa-tachometer-alt' },
        { id: 'video-remove-audio',  label: 'Remove Audio',   icon: 'fas fa-volume-mute' },
    ],
    'audio': [
        { id: 'audio-convert',   label: 'Convert',    icon: 'fas fa-headphones' },
        { id: 'audio-normalize', label: 'Normalize',  icon: 'fas fa-balance-scale-right' },
    ],
    'pdf': [
        { id: 'pdf-compress',  label: 'Compress',   icon: 'fas fa-file-pdf' },
        { id: 'pdf-to-images', label: 'To Images',  icon: 'fas fa-images' },
    ],
};
