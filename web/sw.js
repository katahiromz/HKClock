const CACHE_NAME = 'hkclock-cache-v1';

// App-shell files to precache. Missing optional assets (e.g. sound files)
// are added individually so one 404 doesn't fail the whole install.
const CORE_ASSETS = [
  './',
  './index.html',
  './manifest.json',
  './icon-192.png',
  './icon-512.png',
  './icon-192-maskable.png',
  './icon-512-maskable.png'
];

const OPTIONAL_ASSETS = [
  './1.mp3',
  './2.mp3',
  './3.mp3'
];

self.addEventListener('install', (event) => {
  event.waitUntil(
    caches.open(CACHE_NAME).then(async (cache) => {
      await cache.addAll(CORE_ASSETS);
      await Promise.all(
        OPTIONAL_ASSETS.map((url) =>
          cache.add(url).catch(() => {
            // Optional asset not present (e.g. sound file not deployed yet); ignore.
          })
        )
      );
    })
  );
  self.skipWaiting();
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys().then((names) =>
      Promise.all(
        names
          .filter((name) => name !== CACHE_NAME)
          .map((name) => caches.delete(name))
      )
    )
  );
  self.clients.claim();
});

self.addEventListener('fetch', (event) => {
  const req = event.request;

  // Only handle same-origin GET requests. Let cross-origin requests
  // (internet time sync sources, etc.) go straight to the network
  // untouched by the service worker.
  if (req.method !== 'GET' || new URL(req.url).origin !== self.location.origin) {
    return;
  }

  event.respondWith(
    caches.match(req).then((cached) => {
      const fetchPromise = fetch(req)
        .then((networkRes) => {
          if (networkRes && networkRes.ok) {
            const clone = networkRes.clone();
            caches.open(CACHE_NAME).then((cache) => cache.put(req, clone));
          }
          return networkRes;
        })
        .catch(() => cached);

      // Cache-first for instant load, but refresh cache in background.
      return cached || fetchPromise;
    })
  );
});
