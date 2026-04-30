# Luma Vantage Tracking Audit

This project now routes website funnel analytics through `Luma Vantage` only.

## Required Funnel Events

- `landing_view`
- `tool_open`
- `signup_started`
- `signup_completed`
- `first_value_action`
- `return_visit`
- `share_clicked`

## Event Schema

Every tracked event includes these required properties:

- `session_id`
- `tool_id`
- `tool_category`
- `source_page`
- `utm_source`
- `utm_medium`
- `utm_campaign`
- `is_returning_user`

Optional context currently sent where available:

- `experiment_variant`
- `referrer_domain`
- `device_type`
- `behavior_segment`

## Current Implementation Map

- `landing_view`: session start in `public/js/utils.js`
- `return_visit`: session start for returning users in `public/js/utils.js`
- `tool_open`: navigation switch in `public/js/ui.js`
- `first_value_action`: successful output hooks in `public/js/file-tools.js` and `public/js/downloader.js`
- `share_clicked`: result send-to-tool, support/GitHub links in `public/js/file-tools.js` and `public/index.html`
- `signup_started`: planner CTA + PWA install prompt in `public/index.html` and `public/js/pwa.js`
- `signup_completed`: PWA install acceptance in `public/js/pwa.js`

## Data Quality Controls

- Event dedupe and debounce is enforced in `lvTrack(...)`.
- Required property validation is enforced before dispatch.
- Async SDK startup fallback uses the pre-SDK queue (`_lvProxy` + `LumaVantageQ`).

