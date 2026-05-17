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

## Planned Workspace Shape

```text
agents/study-twin/
  README.md
  schemas/
    study-pack-v0.schema.json
    review-event-v0.schema.json
  prompts/
    generate-pack.md
    import-review-log.md
  examples/
    packs/
    review-logs/
  tools/
    validate-pack
    push-pack
    pull-logs
    import-logs
```

The first implementation should be CLI-first. A GUI can sit on top after the pack/log contracts are
stable enough to test on both Mac and M5Paper.

## Current v0 Contract Files

- `schemas/study-pack-v0.schema.json` - public StudyPack contract for manifests, concepts,
  episodes, and rubrics.
- `schemas/review-event-v0.schema.json` - public ReviewEvent contract emitted by runtimes.
- `examples/packs/fr-a1-mini/` - minimal French A1 fixture pack.
- `examples/review-logs/reviews_000001.jsonl` - minimal ReviewEvent fixture.
- `tools/validate-pack` - local standard-library validator for pack structure, cross-file links,
  counts, action/rubric references, and M5Paper-oriented text/size budgets.

Validate the fixture pack:

```bash
python3 agents/study-twin/tools/validate-pack agents/study-twin/examples/packs/fr-a1-mini
```

## Data Policy

This repository may contain agent instructions, schemas, test fixtures, examples, and intentional
learner-model data. Large source corpora, secrets, credentials, and private raw imports should be
kept out of commits unless they are deliberately curated into the project.

## Near-Term Acceptance

- A StudyPack v0 schema can be validated locally on the Mac.
- A generated StudyPack can be copied to M5Paper and executed offline.
- M5Paper and the Mac runtime app can both emit compatible ReviewEvents.
- The twin can import rotated ReviewEvent segments and produce a next-step StudyPack.
