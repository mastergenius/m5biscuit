# Study Twin Agent Workspace

Status: approved direction, 2026-05-17.

This directory is the repo-committed Mac-side agent workspace for Biscuit's offline-first learning
system. It is intentionally separate from the M5Paper firmware runtime.

## Role

The study twin is the generation and orchestration owner:

- maintain the learner model, learning goals, progress graph, and concept graph;
- track source material, provenance, generated StudyPacks, and review history;
- generate the next adaptive StudyPack from source material plus review evidence;
- sync StudyPacks to runtime clients and import ReviewEvent logs back from them;
- evolve the learning methodology without coupling it to one device UI.

The twin does not run on M5Paper. M5Paper stays a focused offline runtime that executes StudyPacks
and writes ReviewEvents. A local Mac app should use the same StudyPack and ReviewEvent contracts as
M5Paper, with richer input and visualization when useful.

## Architecture Boundary

```text
Mac study twin agent
  source corpus + learner model + concept graph + generator policy
        |
        v
StudyPack v0
  manifest.json + concepts.jsonl + episodes.jsonl + rubrics.jsonl + assets/
        |
        +--> M5Paper runtime
        +--> Mac runtime app
        |
        v
ReviewEvent logs
  sequence-based rotated JSONL segments
        |
        v
Mac study twin agent
  import, update model, generate next pack
```

Shared contracts, not shared UI, are the invariant. The Mac app and M5Paper may render differently,
but they must agree on StudyPack and ReviewEvent semantics.

## Workspace Shape

```text
agents/study-twin/
  README.md
  methodology/
    README.md
    ontology.md
    assessment.md
    goldilocks.md
  courses/
    README.md
  schemas/
    study-pack-v0.schema.json
    review-event-v0.schema.json
  examples/
    packs/
    review-logs/
  tools/
    validate-pack
    validate-course
    generate-pack
    device-sync
    study-web
    import-logs
    prepare-next
  web-viewer/
```

The durable learning source tree lives under `courses/`. StudyPacks are compiled, disposable runtime
artifacts generated from those sources plus learner-model evidence. The first implementation should
be CLI-first. A GUI can sit on top after the pack/log contracts are stable enough to test on both Mac
and M5Paper.

## Current v0 Contract Files

- `schemas/study-pack-v0.schema.json` - public StudyPack contract for manifests, concepts,
  episodes, and rubrics.
- `schemas/review-event-v0.schema.json` - public ReviewEvent contract emitted by runtimes.
- `methodology/` - learning-system model, ontology, assessment policy, and Goldilocks policy.
- `courses/README.md` - source tree convention for durable course material.
- `examples/packs/` - fixture, manual, and generated StudyPacks used for device smoke tests.
- `examples/review-logs/` - minimal ReviewEvent fixtures, including choice-response events.
- `tools/validate-pack` - local standard-library validator for pack structure, cross-file links,
  counts, action/rubric references, firmware JSONL line byte budgets, and M5Paper-oriented text/size
  budgets.
- `tools/validate-course` - local standard-library validator for course source trees, frontmatter
  IDs, and runtime pack references.
- `tools/generate-pack` - local standard-library compiler from course assessment seeds to a
  disposable StudyPack projection. With `--learner-state`, it deterministically reorders assessment
  seeds by imported boundary signals and records the learner-state import in manifest provenance.
- `tools/device-sync` - HTTP helper for pushing StudyPacks and pulling review logs from a paired
  device transfer session. It prefers `/api/study/*` for pack listing, log download, ack, and Mac
  time sync when the firmware supports those endpoints, while StudyPack uploads still use the generic
  file API.
- `tools/study-web` and `web-viewer/` - local browser/PWA runtime for the same StudyPack contract.
  It reads local packs, runs study sessions on the Mac or mobile browser, caches the shell and
  loaded pack API responses for offline use, queues ReviewEvents locally when the server is
  unreachable, and appends ReviewEvent v0 JSONL into imports after sync.
- `tools/import-logs` - local ReviewEvent JSONL summarizer for pulled device logs. With
  `--state-out`, it also builds a compact learner-state signal file from deduplicated events:
  pack/episode/concept action counts, choice selections, confidence/effort averages, and
  Goldilocks-style next-generation hints.
- `tools/prepare-next` - local orchestration helper for the normal Mac-side loop: import logs,
  update learner-state, regenerate generated StudyPacks from course sources, and validate them.

Validate the fixture pack:

```bash
python3 agents/study-twin/tools/validate-pack agents/study-twin/examples/packs/fr-a1-mini
```

Validate a course source tree:

```bash
python3 agents/study-twin/tools/validate-course agents/study-twin/courses/fr-practical-service
```

Generate a disposable StudyPack from course sources:

```bash
python3 agents/study-twin/tools/generate-pack \
  agents/study-twin/courses/fr-practical-service \
  --pack-id generated-fr-practical-service \
  --learner-state agents/study-twin/state/learner-state.json \
  --force
python3 agents/study-twin/tools/validate-pack agents/study-twin/examples/packs/generated-fr-practical-service
```

If no learner-state exists yet, either run `prepare-next` after pulling logs or omit `--learner-state`
for a deterministic seed-only pack.

Push generated packs to a paired device and pull review logs:

```bash
python3 agents/study-twin/tools/device-sync \
  --url 'http://192.168.1.149/?token=<session-token>' \
  --remember \
  sync-current

python3 agents/study-twin/tools/import-logs \
  agents/study-twin/imports \
  --state-out agents/study-twin/state/learner-state.json
```

After `--remember`, the same transfer session can be reused without pasting the URL again until the
device exits File Transfer / Study Sync.

Prepare the next generated packs locally after logs were pulled:

```bash
python3 agents/study-twin/tools/prepare-next agents/study-twin/imports
```

`prepare-next` now requires at least one ReviewEvent JSONL file unless `--allow-empty` is passed. This
prevents accidentally overwriting generated packs from an empty evidence set.

For a non-destructive local check, write generated packs to a temp directory:

```bash
python3 agents/study-twin/tools/prepare-next \
  agents/study-twin/examples/review-logs \
  --state-out /tmp/biscuit-learner-state.json \
  --summary-out /tmp/biscuit-review-summary.json \
  --packs-dir /tmp/biscuit-generated-packs
```

Useful sync commands:

```bash
# Upload the curated current set and pull logs back.
python3 agents/study-twin/tools/device-sync sync-current

# Upload every local example pack and pull logs back.
python3 agents/study-twin/tools/device-sync sync-all

# See what the device currently has under /biscuit/study/packs.
python3 agents/study-twin/tools/device-sync list-packs

# Preview cleanup of old remote packs. Omit --dry-run only after checking the list.
python3 agents/study-twin/tools/device-sync clean-packs --dry-run
```

Run the local web runtime:

```bash
python3 agents/study-twin/tools/study-web
```

Then open `http://127.0.0.1:8765/`. ReviewEvents from the browser runtime are written to
`agents/study-twin/imports/web_reviews_000001.jsonl` and can be imported by the same
`tools/import-logs` path as device logs.

For an iPhone LAN test, run with `--host 0.0.0.0`. Plain HTTP is enough to check the mobile UI, but
iOS service workers require a secure context, so real offline Add-to-Home-Screen testing needs HTTPS
or a tunnel. See `web-viewer/README.md`.

Firmware runtime path:

```text
/biscuit/study/packs/<pack_id>/manifest.json
/biscuit/study/packs/<pack_id>/concepts.jsonl
/biscuit/study/packs/<pack_id>/episodes.jsonl
/biscuit/study/packs/<pack_id>/rubrics.jsonl
/biscuit/study/logs/reviews/reviews_000001.jsonl
```

Current sync v0 uses a hybrid token-protected transport. StudyPack upload and cleanup still use the
generic file API exposed by File Transfer / Study Sync: `/api/files`, `/upload`, `/delete`, and
`/mkdir`. Pack listing, review-log download, review-log ack, and clock sync prefer the narrower
`/api/study/*` endpoints when available, with generic file API fallback for older firmware.

## Data Policy

This repository may contain agent instructions, schemas, test fixtures, examples, and intentional
learner-model data. Large source corpora, secrets, credentials, and private raw imports should be
kept out of commits unless they are deliberately curated into the project.

## Near-Term Acceptance

- A StudyPack v0 schema can be validated locally on the Mac.
- A generated StudyPack can be copied to M5Paper and executed offline.
- M5Paper StudyActivity emits compatible ReviewEvents. The legacy CSV Flashcards app writes rotated
  JSONL too, but still uses its older event shape and is not part of the learner-state import path.
- The twin can import rotated ReviewEvent segments and produce a next-step StudyPack.
