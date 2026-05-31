# Offline-First Study Sync

Date: 2026-05-17

This note captures the approved direction for using M5Paper as an offline-first study device, with
a repo-committed Mac agent environment as the digital twin, generation, sync, and analytics owner.

## Product Shape

M5Paper should be useful without the Mac connected. The Mac-side digital twin agent prepares study
packs, pushes them to the device during explicit sync sessions, and later pulls review history back
from the device.

The digital twin is not a device feature. It lives in the local Mac agent environment, committed in
this repository as project-owned generation/orchestration code and data contracts. It owns the
learner model, concept graph, source corpus links, progress history, and hypotheses about the next
best learning step. Runtime clients consume its StudyPacks and produce ReviewEvents.

The device runtime owns:

- local StudyPack storage on SD;
- e-ink rendering of learning episodes;
- touch/button review flow;
- durable review event logging;
- enough scheduling metadata to continue a session offline.

The Mac digital twin owns:

- source ingestion and provenance;
- learner model and progress graph;
- concept graph and prerequisite structure;
- StudyPack generation and adaptation;
- richer local Mac runtime, authoring, and debugging UI;
- richer scheduling or analytics;
- importing review logs;
- generating new or updated study packs.

The planned Mac app is a runtime client over the same StudyPack and ReviewEvent contracts, not a
separate learning model. It may offer richer input, graph views, editing, and AI-assisted assessment,
but it must remain protocol-compatible with the M5Paper runtime.

The same trust and transport substrate should also support non-study payloads, especially local
notes from the distraction-free thinking-machine mode. Study sync remains a domain-specific payload;
pairing, host discovery, short-lived session derivation, acknowledgements, and failure handling
should be shared where practical. See [Distraction-Free Thinking Machine](distraction-free-thinking-machine.md).

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

## Untrusted Network Hardening

The current security model is acceptable for an explicit short-lived transfer session on a trusted
home LAN. It should not be treated as safe for a cafe, hotel, guest WiFi, or any network where other
clients may sniff or actively attack local traffic. HTTP is not encrypted, and compatibility modes
such as WebDAV Basic auth expose the session token to anyone who can observe the network.

Before using Biscuit sync as a normal workflow on untrusted networks, add a separate hardened mode:

- make the transfer screen distinguish `Trusted LAN` from `Untrusted / Guest Network`;
- prefer device-created WPA2 hotspot for untrusted networks, even when saved WiFi credentials exist;
- reduce public discovery in hardened mode to a redacted identity response, or require the QR token
  for every endpoint including `/api/device`;
- stop carrying the long-lived bearer secret in browser-visible URLs after pairing: use the QR URL
  only to perform a one-time exchange, then immediately replace it with a RAM-only session cookie or
  bearer token and remove the query token from browser history;
- bind a session to the first authenticated client where practical, and reject or require re-pairing
  for concurrent clients;
- rate-limit failed token, Basic auth, and WebSocket auth attempts;
- add a short idle timeout for transfer sessions, plus an explicit on-device "Stop sharing" action;
- gate WebDAV behind an explicit compatibility toggle in hardened mode because Basic auth over HTTP
  is easy to capture;
- enforce one path normalization and protected-path policy for HTTP upload/download/delete,
  WebSocket upload, WebDAV, and future Study APIs;
- narrow Study sync endpoints to `/biscuit/study/...` instead of giving the Mac helper arbitrary SD
  write access when only study content is being synced;
- add CSRF protection for browser-cookie write requests, or require bearer headers for writes even
  after browser pairing;
- sign generated StudyPacks or at least verify manifest hashes before activation, so interrupted or
  tampered uploads cannot become the active learning material;
- keep saved WiFi credentials write-only/redacted through the web API and never return plaintext
  credentials after provisioning;
- optionally investigate HTTPS or a small Noise/PAKE-style encrypted pairing channel, but treat it
  as a measured feature because TLS memory and certificate UX may be costly on ESP32.

Acceptance test for the current short-lived model:

1. Start File Transfer and confirm old unauthenticated requests return `401`.
2. Confirm the QR/session token works for authorized requests.
3. Exit File Transfer on the device.
4. Confirm the old IP/token no longer reaches protected endpoints.

## Study Pack Layout

Proposed SD layout:

```text
/biscuit/study/
  packs/
    <pack_id>/
      manifest.json
      concepts.jsonl
      episodes.jsonl
      rubrics.jsonl
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

- pack id and title;
- schema version;
- content revision;
- generator id/version and source provenance;
- renderer capabilities used by the pack;
- optional scheduler hints;
- asset list and hashes.

`episodes.jsonl` should be line-oriented so updates can be streamed and partially validated.
Episode records should be declarative: prompt, reveal/rubric content, concept ids, expected action
type, review buttons, and any small local rules needed to run without the Mac connected. A
traditional flashcard is only one possible episode type.

## Review Log Model

Do not keep one unbounded always-append log. Use rotated log segments.

Each review event should include:

- monotonic event sequence number;
- pack id;
- episode id;
- concept ids, when known;
- action type: `shown`, `revealed`, `answered`, `again`, `hard`, `good`, `easy`, `confused`,
  `skipped`, `undo`, etc.;
- optional confidence/effort/confusion self-report;
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

Current v0 uses a hybrid session-token protected API. The Mac helper still uploads StudyPack files
with the generic `/mkdir` and `/upload` endpoints, because that path is already proven for directory
trees. For readback and sync state it now prefers narrower Study endpoints:

- `GET /api/study/packs`;
- `GET /api/study/logs`;
- `GET /api/study/logs/<segment>`;
- `POST /api/study/logs/ack`;
- `POST /api/study/time`.

If these endpoints are missing on older firmware, the helper falls back to the generic file API for
pack listing and log download. The remaining hardening pass is to replace generic StudyPack writes
with a dedicated pack upload/replace endpoint so study sync no longer needs arbitrary SD-card write
access.

Target HTTP endpoints can stay simple:

- `GET /api/study/packs` - list installed StudyPacks and revisions;
- `POST /api/study/packs/<pack_id>` - upload or replace a StudyPack;
- `GET /api/study/logs` - list review log segments and sequence ranges;
- `GET /api/study/logs/<segment>` - download a log segment;
- `POST /api/study/logs/ack` - acknowledge imported events;
- `POST /api/study/time` - set Mac-provided time with source metadata.

WebSocket is useful for live sync progress, preview, and diagnostics, but the device must not depend
on WebSocket for normal offline study.

Longer-term seamless sync should move the normal path from QR-driven Mac pull to one-button,
device-initiated sync with a paired Mac host. The existing File Transfer token model should remain
as a fallback and recovery path, not the daily capture workflow.

## Implementation Order

1. Done: add session auth and WPA hotspot fallback to the existing web server.
2. Done: add a StudyPack v0 document schema and validation helpers.
3. Done: add rotated review log storage with sequence-based sync state.
4. Done: build a minimal M5Paper StudyActivity using local StudyPacks and local logs.
5. Done for v0: add a CLI helper over the generic file API for StudyPack upload and log download.
6. Done for v0: add dedicated readback/state endpoints for StudyPack listing, log download, ack,
   and Mac-provided time sync.
7. In progress: add the repo-committed Mac digital twin agent workspace as the reference generator/sync owner.
8. Next: add dedicated StudyPack upload/replace and retention endpoints, then build the Mac app after
   the file/API contracts are stable enough to
   test manually.

## Implementation Status

- Session-token protected WiFi transfer is implemented for HTTP, WebDAV, and WebSocket upload.
- Saved-WiFi is the normal transfer path; WPA2 hotspot mode is available as bootstrap/fallback.
- M5Paper has a local StudyPack runtime that lists packs from `/biscuit/study/packs`, executes
  brief/retrieve/choice episodes offline, saves per-pack progress, and appends ReviewEvent v0 records.
- The legacy CSV `Flashcards` app writes rotated JSONL segments under `/biscuit/study/logs/reviews/`,
  but still uses its older event shape; current Mac import tooling intentionally ignores those records.
- Review ordering is sequence-based. Segment names are monotonic (`reviews_000001.jsonl`) and state
  is tracked in `/biscuit/study/logs/sync_state.json`. Wall-clock time is optional and can be supplied
  by the Mac through `/api/study/time`; event ordering still depends on sequence numbers.
- The Study sync API now exposes pack listing, log segment listing/download, `acked_seq`, and Mac
  time sync. StudyPack upload still uses generic file-transfer endpoints until the dedicated upload
  path is added.
- The approved target architecture is no longer "flashcards on M5Paper". It is a local-first
  adaptive learning loop: Mac digital twin agent generates StudyPacks, M5Paper/Mac app execute
  episodes, review logs return to the twin, and the twin generates the next step.
- StudyPack v0 and ReviewEvent v0 schemas now live under `agents/study-twin/schemas/`, with local
  course sources, manual/generated packs, a standard-library validator, a log importer, a deterministic
  pack compiler, and a `prepare-next` orchestration helper under `agents/study-twin/tools/`.
