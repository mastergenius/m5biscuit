# Offline-First Study Sync

Date: 2026-05-17

This note captures the approved direction for using M5Paper as an offline-first study device,
with a Mac as the authoring, sync, and analytics companion.

## Product Shape

M5Paper should be useful without the Mac connected. The Mac prepares study packs, pushes them to the
device during explicit sync sessions, and later pulls review history back from the device.

The device owns:

- local deck storage on SD;
- e-ink rendering of cards and prompts;
- touch/button review flow;
- durable review event logging;
- enough scheduling metadata to continue a session offline.

The Mac owns:

- deck authoring and bulk editing;
- richer scheduling or analytics;
- importing review logs;
- generating new or updated study packs.

## Network Model

Default sync should use saved WiFi credentials already stored on the M5Paper. Hotspot mode is only a
bootstrap or fallback path.

Normal path:

1. User starts a study sync or file transfer session on M5Paper.
2. M5Paper joins the saved WiFi network.
3. M5Paper shows IP address and a QR pairing URL containing the short-lived session token.
4. The Mac connects over the local network.

Fallback path:

1. If no saved WiFi works, user chooses Create Hotspot.
2. M5Paper creates a WPA2 hotspot.
3. The QR code contains hotspot credentials plus the session pairing URL.

The hotspot password controls network access only. It does not replace the session token. HTTP,
WebDAV, and WebSocket access must require session authorization in both STA and AP modes.

## Security Model

Session auth should be explicit and short-lived:

- generate a random token when the transfer/sync session starts;
- keep the token in RAM only;
- expire it when the activity exits, deep sleep starts, or the device reboots;
- require the token for file access, write APIs, WebDAV, WebSocket upload, settings, WiFi credential
  changes, and study sync;
- allow only a minimal public surface such as device identity and a redacted status endpoint.

Recommended carrier:

- API requests: `Authorization: Bearer <token>`;
- browser pairing URL: `http://<ip>/?token=<token>`;
- WebDAV compatibility: Basic auth with user `biscuit` and password equal to the session token.

## Study Pack Layout

Proposed SD layout:

```text
/biscuit/study/
  decks/
    <deck_id>/
      manifest.json
      cards.jsonl
      assets/
        ...
  logs/
    reviews/
      reviews_000001.jsonl
      reviews_000002.jsonl
      ...
    sync_state.json
```

`manifest.json` should declare:

- deck id and title;
- schema version;
- content revision;
- renderer capabilities used by the deck;
- optional scheduler hints;
- asset list and hashes.

`cards.jsonl` should be line-oriented so updates can be streamed and partially validated. Card
records should be declarative: text, layout hints, answer/reveal content, review buttons, and any
small local rules needed to run without a Mac.

## Review Log Model

Do not keep one unbounded always-append log. Use rotated log segments.

Each review event should include:

- monotonic event sequence number;
- deck id;
- card id;
- action type: `shown`, `revealed`, `again`, `hard`, `good`, `easy`, `skipped`, `undo`, etc.;
- monotonic milliseconds since boot or session start;
- optional UTC timestamp when clock status is trusted;
- device id and firmware/schema version;
- optional scheduler state snapshot if needed for offline continuation.

The sequence number is the source of sync truth. Wall-clock time is useful but must not be required
for ordering, log rotation, or deduplication.

Suggested rotation policy:

- rotate by size first, for example 64 KB or 128 KB per segment;
- rotate by event count second, for example 1000 events;
- rotate by date only when the clock is trusted;
- name segments by monotonic sequence, not by date: `reviews_000001.jsonl`;
- keep a small `sync_state.json` with last event sequence, open segment id, and Mac ack state.

After the Mac imports review events, it sends an acknowledgement containing the highest imported
event sequence and segment ids. The device can then mark old segments as synced, compact them, or
delete them according to retention settings.

## Clock Trust

Clock state should be explicit:

- `unknown`: no trusted wall-clock time;
- `mac_sync`: time supplied by the Mac during sync;
- `ntp_sync`: time obtained over WiFi;
- `rtc_or_saved`: restored from device state, lower trust unless validated.

Event integrity must not depend on wall-clock correctness. If the clock is unknown, events still
sync by sequence number and monotonic time. When the Mac later imports them, it can annotate them
with import time or infer approximate wall-clock ranges.

## Sync API Shape

Initial HTTP endpoints can stay simple:

- `GET /api/study/decks` - list installed decks and revisions;
- `POST /api/study/decks/<deck_id>` - upload or replace a study pack;
- `GET /api/study/logs` - list review log segments and sequence ranges;
- `GET /api/study/logs/<segment>` - download a log segment;
- `POST /api/study/logs/ack` - acknowledge imported events;
- `POST /api/study/time` - set Mac-provided time with source metadata.

WebSocket is useful for live sync progress, preview, and diagnostics, but the device must not depend
on WebSocket for normal offline study.

## Implementation Order

1. Add session auth and WPA hotspot fallback to the existing web server.
2. Add a StudyPack v0 document schema and validation helpers.
3. Add rotated review log storage with sequence-based sync state.
4. Build a minimal M5Paper StudyActivity using local decks and local logs.
5. Add sync endpoints for deck upload, log download, and ack.
6. Build a Mac helper/app after the device-side format is stable enough to test manually.

