# Learning Loop Stabilization Plan

This is the current reviewable work plan for the Biscuit learning loop.

## 1. Review Boundaries

Keep the major surfaces explicit:

- `agents/study-twin/`: course sources, pack generation, schemas, local web runtime.
- `agents/device-sync/`: paired host, HMAC transport, incoming logs, outgoing pack delivery.
- `src/study/`: firmware pack reader and review log writer.
- `src/activities/apps/StudyActivity.*`: M5Paper StudyPack runtime.
- `src/activities/apps/DeviceSyncActivity.*`: one-button device sync.
- `docs/`: architecture, device notes, cleanup plans, and daily work records.

## 2. Sync Packs Host To Device

Device sync v1 should be offline-first:

- M5Paper reports installed StudyPack ids and revisions.
- The Mac host compares them against curated local packs.
- The host returns only missing or stale packs.
- The device writes packs atomically under `/biscuit/study/packs`.
- Review logs flow back from device to host in the same sync.

This keeps the device usable without the host, while making "press sync" enough to refresh learning material.

## 3. M5Paper Study UX

Stabilize the M5 runtime before expanding content formats:

- pack selection must work by touch and physical controls;
- choice responses must show the selected answer on reveal;
- rating buttons need visible meanings, not only labels;
- progress and review logs must be written incrementally.

## 4. Local Web Runtime

The web viewer is the fast iteration runtime and phone alternative:

- validate review events before writing;
- show session progress and queued event state;
- support offline shell and queued logs;
- export review logs for manual inspection and recovery.

## 5. Firmware Cleanup

Start reducing firmware drift without a large rewrite:

- make menu feature boundaries explicit;
- keep reader, study, sync, and system surfaces first-class;
- isolate parked experiments behind flags or remove them after review;
- improve touch-first navigation after the menu surface is smaller.

## Verification

For this iteration, the expected verification set is:

- StudyPack validators for all generated packs.
- Course validators for checked-in course sources.
- device-sync smoke test.
- Python syntax check for sync and study tools.
- `pio run -e m5paper`.
- `pio run -e default`.
- `git diff --check`.
