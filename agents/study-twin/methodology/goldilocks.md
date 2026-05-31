# Goldilocks Policy

Status: working policy, 2026-05-25.

The goal is not to make every StudyPack harder. The goal is to keep the learner near the useful edge:
enough stability to consolidate, enough stretch to grow, enough probes to discover the next boundary.

## Pack Composition

A pack should usually mix these item classes:

- stabilization and retrieval: already-seen material that should become easier;
- near transfer: the same pattern in a slightly changed context;
- stretch: tasks at the expected boundary;
- probe: small experiments that test a hypothesis about the next boundary;
- reflection: prompts that expose the learner's model and confidence.

For short packs, a useful starting ratio is:

- 40-50% stabilization or retrieval;
- 25-35% near transfer or application;
- 10-20% boundary probes;
- 5-10% reflection or meta-calibration.

These are defaults, not a law. The twin may shift the mix based on fatigue, urgency, user goal, and
recent evidence.

## Boundary Signals

The twin should adjust future packs from observed signals:

- easy, fast, confident: increase context shift, compression, or production demand;
- correct but slow: keep difficulty, reduce friction, add one near-transfer item;
- acceptable with caveat: add contrastive examples and judgment prompts;
- partial: isolate the missing subpattern;
- repeated misconception: generate a repair sequence with counterexamples;
- skipped or high-friction: reduce cognitive load and split the task;
- confident but wrong: add calibration feedback and adversarial-but-fair probes;
- low confidence but strong result: add fluency practice and confidence calibration.

## Modality Fit

Goldilocks depends on the runtime surface.

M5Paper is in the useful zone when an item is relevant, compressed, and answerable through
recognition, calibration, or routing. It falls out of the zone when the item demands long writing,
generic reflection, or invented tiny situations that feel less real than the method they are meant to
teach.

Workbook, web, and iOS projections are in the useful zone when they support writing, decomposition,
artifact work, and later analysis by the Mac twin. They fall out of the zone when they behave like
long flashcards without preserving the learner's produced artifact.

The twin should choose modality before generating the episode, not after.

## Pack Acceptance

Before syncing a generated pack, the twin should be able to state:

- target competency;
- expected boundary;
- why this learner needs this pack now;
- which source material it comes from;
- what evidence it will collect;
- what follow-up branches are plausible after the session;
- which device/media constraints shaped the projection.

If those statements are unclear, the pack is probably an unprincipled content dump rather than a
methodological learning unit.
