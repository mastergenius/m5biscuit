# Learning Ontology

Status: working ontology, 2026-05-25.

The ontology separates what is being learned, how it is taught, how it is rendered, and what evidence
comes back from a session.

## Durable Objects

`KnowledgeDomain`
: A broad area of knowledge or practice, such as practical French or FPF-style practical thinking.

`Course`
: A learner-specific path through a domain. A course has goals, scope, tracks, prerequisites, and a
  policy for selecting the next boundary.

`Methodology`
: The transformation rules that turn source material and learner evidence into runtime activities.
  Methodology is reusable across courses and media.

`ActivityModality`
: The interaction shape a runtime item needs, such as propagation, calibration, recognition drill,
  worked example, guided decomposition, artifact task, workbook, or exploration request. Modality is
  separate from device: M5Paper, web, iOS, and PDF can project overlapping but not identical
  modalities.

`LearningMaterial`
: Durable authored source material. It may be a short article, scenario, pattern, worked example,
  drill seed, conceptual map, or assessment seed.

`Concept`
: An explanatory unit the learner should be able to recognize, explain, or use.

`Pattern`
: A reusable action or construction schema. For French this can be a sentence-building pattern; for
  FPF this can be a framing or boundary-setting move.

`Competency`
: An observable capability. It should be phrased as work the learner can perform, not as content
  they have seen.

`Scenario`
: A situated context that forces the learner to apply concepts and patterns under constraints.

`AssessmentSeed`
: A durable specification for a diagnostic task. It is not necessarily a runtime item yet.

`LearnerModel`
: The Mac-side evolving model of goals, progress, strengths, weak points, misconceptions, preferred
  contexts, fatigue/friction signals, and next-boundary hypotheses.

## Runtime Objects

`StudyPack`
: A compiled, disposable runtime bundle. It is generated for a specific learner state, device
  constraints, and session goal.

`Episode`
: A runtime activity inside a StudyPack: article, recall prompt, choice item, applied scenario,
  reflection, map step, or drill.

`LearnerSignal`
: Optional item-level metadata attached to an Episode by the Mac twin. It records why the item was
  selected or moved, such as `repair`, `near_transfer`, or `stabilize_then_stretch`, plus compact
  evidence counts. It is not learner-facing copy. Small runtimes may ignore it.

`LearnerFeedback`
: Runtime evidence emitted by the learner after or during an episode. It should preserve separate
  dimensions when possible: understanding, novelty, relevance, and next-routing intent. It should not
  be collapsed into spaced-repetition ease unless the activity is actually a memory item.

`Rubric`
: The runtime interpretation policy for an action. Rubrics should explain what a response means, not
  only whether it is correct.

`Session`
: A concrete execution of a StudyPack on a runtime client.

`ReviewEvent`
: A runtime observation emitted during a session: item action, rating, selected choice, answer text,
  timing, skip, friction, note, or completion state.

`MediaProjection`
: A compiled representation for a client or channel: M5Paper, Mac app, iPad/iPhone app, Stackchan
  activity, web page, or printable PDF.

## Relationships

- A domain contains courses, concepts, patterns, and source material.
- A course targets competencies and orders boundaries.
- Learning material introduces concepts and patterns.
- Scenarios exercise competencies.
- Assessment seeds diagnose concepts, patterns, misconceptions, and transfer ability.
- Methodology transforms source material plus learner evidence into StudyPacks.
- StudyPacks instantiate selected material for one learner state, one runtime context, and one or
  more activity modalities.
- Sessions produce ReviewEvents.
- ReviewEvents and LearnerFeedback update the LearnerModel.
- The LearnerModel selects the next boundary and personalization policy.
- Media projections render the same methodological intent through different interfaces.

## Statement Discipline

Source material should distinguish these statement types:

- `explanation`: helps the learner build a model;
- `rule`: a compact operational rule;
- `promise`: what using the rule should achieve;
- `boundary`: where the rule stops applying;
- `example`: a concrete instance;
- `counterexample`: a tempting but wrong or limited instance;
- `diagnostic`: what a response reveals about the learner;
- `evidence`: observation used to update the LearnerModel.

This matters because a runtime item generated from an explanation should not be evaluated like a
simple fact recall item. The twin needs to know whether it is testing recognition, application,
transfer, judgment, or calibration.
