# Biscuit Learning PWA

Status: mobile-first prototype, 2026-05-24.

This runtime executes the same StudyPack v0 files as M5Paper and writes compatible ReviewEvents.
It is intentionally a thin client: course generation, learner modeling, and sync policy stay in the
Mac-side study twin.

## What Works

- mobile-friendly StudyPack session UI;
- web app manifest for Add to Home Screen flows;
- service worker shell cache;
- cached `/api/packs` and `/api/packs/<pack_id>` responses after first successful load;
- local ReviewEvent queue when `/api/review` is unavailable;
- manual and automatic queue flush when the server becomes reachable again.
- session event summary in the runtime status bar;
- manual ReviewEvent export from `/api/reviews/export`.

The local server validates ReviewEvents before appending them to `web_reviews_000001.jsonl`.
Malformed ids, unsupported actions, and out-of-range confidence/effort values are rejected with
HTTP 400 instead of being normalized into ambiguous logs.

## Run Locally

```bash
python3 agents/study-twin/tools/study-web
```

Open:

```text
http://127.0.0.1:8765/
```

To open from an iPhone on the same network:

```bash
python3 agents/study-twin/tools/study-web --host 0.0.0.0 --port 8765
```

Then browse to the Mac's LAN address.

## iPhone Caveat

iOS only enables service workers in a secure context. `http://127.0.0.1` works on the Mac, but
`http://<mac-lan-ip>:8765` from an iPhone is not a secure context, so the app may load but the
offline shell will not register. For a real phone test, use HTTPS through one of:

- a trusted local certificate setup;
- a temporary HTTPS tunnel to the Mac server;
- a static HTTPS deployment plus file-based pack import/export in a later iteration.

The runtime still remains useful over plain HTTP for online mobile UI testing.

## Personal Context Boundary

Drafts and Obsidian are not owned by this runtime. The PWA should emit ReviewEvents and explicit
Markdown captures. Mac-side tools can then route those artifacts into knowledge base, Obsidian, or
Drafts-derived intake with provenance and review.
