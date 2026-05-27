const CACHE_NAME = 'lumatools-v328';
const PRECACHE_ASSETS = [
    '/',
    '/styles.css',
    '/styles-v2.css',
    '/js/state.js',
    '/js/utils.js',
    '/js/ui.js',
    '/js/waveform.js',
    '/js/redact.js',
    '/js/crop.js',
    '/js/wasm.js',
    '/js/file-tools.js',
    '/js/batch.js',
    '/js/tools-misc.js',
    '/js/downloader.js',
    '/js/health.js',
    '/js/pwa.js',
    '/js/tools-catalog.js',
    '/js/tool-specs.js',
    '/js/tool-page.js',
    '/js/app-shell.js',
    '/manifest.json',
    'https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.5.1/css/all.min.css',
    'https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700;800;900&family=JetBrains+Mono:wght@400;600&display=swap',
    'https://cdn.jsdelivr.net/npm/qrcode-generator@1.4.4/qrcode.min.js',
    'https://cdn.jsdelivr.net/npm/jszip@3.10.1/dist/jszip.min.js',
];

self.addEventListener('install', (event) => {
    event.waitUntil(
        caches.open(CACHE_NAME).then((cache) => {
            return cache.addAll(PRECACHE_ASSETS).catch(() => {});
        })
    );
    self.skipWaiting();
});

self.addEventListener('activate', (event) => {
    event.waitUntil((async () => {
        // Wipe every cache that isn't the current one
        const names = await caches.keys();
        await Promise.all(names.filter(n => n !== CACHE_NAME).map(n => caches.delete(n)));
        // Take over any open tab immediately
        await self.clients.claim();
        // Force-reload any open tabs so they pick up the new shell/assets
        // (otherwise the controlling tab keeps showing the old JS/CSS
        // it loaded before this SW took over).
        const tabs = await self.clients.matchAll({ type: 'window' });
        for (const tab of tabs) {
            try { tab.navigate(tab.url); } catch {}
        }
    })());
});

self.addEventListener('fetch', (event) => {
    const url = new URL(event.request.url);

    // API requests: always network, offline fallback
    if (url.pathname.startsWith('/api/')) {
        event.respondWith(
            fetch(event.request).catch(() => {
                return new Response(
                    JSON.stringify({ error: 'Offline — server features require a connection' }),
                    { status: 503, headers: { 'Content-Type': 'application/json' } }
                );
            })
        );
        return;
    }

    // Navigation requests (HTML pages): ALWAYS go to network so that
    // Cross-Origin-Opener-Policy / Cross-Origin-Embedder-Policy headers from
    // the server are respected. Serving stale HTML from cache strips those
    // headers and breaks crossOriginIsolated (required for ffmpeg.wasm).
    if (event.request.mode === 'navigate') {
        event.respondWith(
            fetch(event.request).catch(() => caches.match('/') || new Response('Offline', { status: 503 }))
        );
        return;
    }

    event.respondWith(
        caches.match(event.request).then((cached) => {
            if (cached) {
                fetch(event.request).then((response) => {
                    if (response && response.status === 200) {
                        caches.open(CACHE_NAME).then((cache) => {
                            cache.put(event.request, response);
                        });
                    }
                }).catch(() => {});
                return cached;
            }

            return fetch(event.request).then((response) => {
                if (!response || response.status !== 200 || response.type === 'opaque') {
                    return response;
                }
                const responseToCache = response.clone();
                caches.open(CACHE_NAME).then((cache) => {
                    cache.put(event.request, responseToCache);
                });
                return response;
            }).catch(() => {
                return new Response('Offline', { status: 503 });
            });
        })
    );
});
