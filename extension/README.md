# Luma Tools — Browser Extension

Right-click any image, video, or audio on the web → process it with Luma Tools.

## Load locally (unpacked, for dev or self-install)

1. Open `chrome://extensions` (or `edge://extensions`, or `about:debugging` for Firefox)
2. Toggle **Developer mode** (top-right)
3. Click **Load unpacked**, pick this `extension/` folder
4. Click the extension icon → paste your API key from
   https://tools.lumaplayground.com/account → **Save**

Now right-click any image / video / audio element on any site → menu item appears.

## Icons

This repo ships placeholder icon stubs (`icons/16.png`, `48.png`, `128.png`).
Replace them with real PNGs (transparent background, the Luma bolt mark looks fine
re-cropped from `public/icon-512.png`) before submitting to the Chrome Web Store.

## Publish to the Chrome Web Store

1. https://chrome.google.com/webstore/devconsole — pay the one-off $5 dev fee
2. Zip the contents of this folder (`zip -r luma-tools.zip *`)
3. Upload, fill in the listing (description, screenshots, privacy disclosures)
4. Submit for review

## Firefox

Manifest v3 works in Firefox 109+. To publish, sign at
https://addons.mozilla.org/en-US/developers/. No upfront fee.

## What it does (technical)

- Service worker registers 6 context-menu items on image/video/audio elements
- On click: fetches the source URL, builds a FormData, POSTs to
  `/api/tools/<tool>` with `Authorization: Bearer <stored key>`
- Saves the response binary via `chrome.downloads.download`
- Notifications via `chrome.notifications` keep the user informed
- API key is stored in `chrome.storage.sync` (syncs across the user's signed-in
  Chrome profiles)

## Permissions

- `contextMenus`  — to add the right-click items
- `downloads`     — to save the processed file
- `notifications` — to show progress / errors
- `storage`       — to remember the API key
- `host_permissions: tools.lumaplayground.com` — to call the API
