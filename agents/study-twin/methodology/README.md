# Study Twin Methodology

Status: working methodology, 2026-05-25.

This directory defines how Biscuit turns durable learning sources and learner evidence into
personalized runtime material. It is deliberately device-neutral. M5Paper is one projection of the
methodology, not the owner of the methodology.

## Core Thesis

Course sources are durable. StudyPacks are expendable.

StudyPacks are not flashcard decks. They are small learning objects compiled for a runtime modality.
M5Paper is the low-friction propagation and calibration surface; writing-heavy thinking belongs to a
web/iOS/Mac workbench, printable workbook, or future keyboard-capable device mode.

The Mac-side study twin owns the learner model, source corpus, concept graph, generation policy, and
review-log interpretation. Runtime clients execute a pack offline, collect ReviewEvents, and sync
those events back. The next pack is compiled from course sources plus the updated learner model.

```text
course sources + learner model + methodology
              |
              v
     personalized StudyPack
              |
              v
offline session on M5Paper / Mac / iPad / another runtime
              |
              v
 ReviewEvents: answers, ratings, timing, friction, notes
              |
              v
 Mac study twin updates model and chooses the next boundary
```

## Invariants

- Offline-first: an already-synced pack must remain usable without network access.
- Shared contracts: runtime clients share StudyPack and ReviewEvent semantics, not UI code.
- Device simplicity: the M5Paper firmware should not own course logic, long-term learner modeling,
  or generation policy.
- Source durability: long-lived course material is Markdown/YAML in the repo; JSONL packs are
  compiled artifacts for runtime sync.
- Evidence over grades: every answer is diagnostic input to the learner model, not merely
  `right`/`wrong`.
- Signals over Anki actions: `good`, `easy`, and `again` are too lossy for methodology learning.
  Runtime feedback should preserve understanding, novelty, relevance, and routing intent whenever
  the client can support it.
- Goldilocks pressure: packs should stabilize known material while probing the learner's current
  boundary.
- Provenance: generated items should be traceable to source material, target competencies, and the
  learner-model assumptions they test.
- Separation of copy and diagnostics: learner-facing text should stay clean; adaptive metadata such
  as `learner_signal` belongs in structured fields that richer runtimes and the Mac twin can read.
- Media neutrality: the same source material may project to M5Paper, a Mac app, iPad/iPhone,
  Stackchan activity, or printable PDF.

## Method Loop

1. Select a course boundary from the learner model.
2. Compile a small StudyPack that targets that boundary.
3. Execute offline on a runtime client.
4. Emit rotated ReviewEvent JSONL logs during the session.
5. Sync logs to the Mac twin.
6. Interpret responses as evidence about concepts, competencies, misconceptions, effort, and
   confidence calibration.
7. Update the learner model.
8. Generate the next expendable pack.

## What A Pack Must Say

A generated StudyPack should carry enough intent for the twin to learn from the session:

- target domain, course, and competency;
- expected Goldilocks boundary;
- item-level provenance;
- why each item is included;
- what each response option diagnoses;
- feedback that explains acceptable, partial, and context-dependent answers;
- review actions that map to learner-model updates.

## Related Documents

- `problem-framing.md` defines the Learning System v2 problem portfolio, acceptance criteria, and
  evidence plan before implementation choices.
- `capability-evidence-model.md` defines the target capability stack, evidence types, baseline
  probes, indicator roles, and reopen triggers.
- `baseline-probes.md` defines the first manual probe set used to test usefulness before heavier
  runtime or generator changes.
- `ontology.md` defines the domain objects and their relationships.
- `interaction-modalities.md` defines M5Paper propagation, richer writing surfaces, learner signal
  dimensions, and workbook routing.
- `assessment.md` defines response interpretation beyond binary grading.
- `goldilocks.md` defines boundary selection and pack composition policy.
- `mim-qualification-packs.md` defines how MIM packs should train trust-with-complexity rather than
  term recall.
- `mim-guide1-selfdev-packs.md` defines how Guide 1 self-development excerpts are projected into
  denser M5Paper-friendly propagation/calibration packs.
- `mim-workdev-modeling-packs.md` defines how MIM work-development modeling and systems-thinking
  material is projected into dense propagation/calibration packs.
- `../courses/README.md` defines the durable Markdown/YAML source layout.
