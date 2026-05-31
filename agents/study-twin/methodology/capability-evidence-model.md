# Learning System v2: Capability And Evidence Model

Status: active characterization, 2026-05-25.

This document deepens `problem-framing.md`. It characterizes what must improve, what evidence can
support that claim, and what indicators must be watched without becoming the goal.

FPF stance:

- FRAME-09: keep role, capability, method, and work separate.
- CHR-01: mark each indicator as constraint, target, or observation.
- CHR-02: do not average unlike indicators into one score.
- X-STATEMENT-TYPE: distinguish rule, explanation, gate, evidence, and promise.
- X-TRANSFORMER: the learner acts; the system proposes, records, and adapts.
- X-SOURCE-RESTORATION: logs and generated summaries are not evidence until we restore what target
  capability they are evidence for.

## Target Capability

The target is not "knows MIM/FPF terms". The target is practical methodological capability:

> In a real or realistic situation, the learner can read what is going on, choose a suitable first
> methodical move, explain why alternatives are premature or weaker, and route the result into an
> action, artifact, or deeper inquiry.

This capability is plural. It has components, and each component needs different activities and
evidence.

## Capability Stack

`C1. Situated reading`
: Recognize what kind of situation this is before applying a method. Evidence: the learner can say
  whether the issue is a missing boundary, role confusion, target-system confusion, evidence gap,
  overloaded carrier, or premature solution.

`C2. Distinction control`
: Preserve load-bearing distinctions. Evidence: the learner does not collapse object/description/
  carrier, role/capability/method/work, target/enabling system, plan/reality, or rule/evidence.

`C3. First-method-move selection`
: Choose the next methodological move appropriate to the situation. Evidence: on a bad task
  description, the learner can choose whether to clarify target system, split roles, find
  supersystem constraints, identify failure risk, ask for evidence, or produce a carrier.

`C4. Contrastive judgment`
: Explain why attractive alternatives are wrong, partial, premature, or context-dependent. Evidence:
  the learner can reject a plausible distractor with a reason, not just select the right option.

`C5. Transfer`
: Apply the method outside the exact example. Evidence: the learner can handle a near-transfer and a
  far-transfer variant, or connect the method to an active project without forcing the analogy.

`C6. Artifact production`
: Produce or improve a carrier that changes work: task rewrite, claim/evidence note, system map,
  decision frame, checklist, or workbook answer. Evidence: the artifact is specific enough for a
  future self/agent to use.

`C7. Calibration`
: Know whether the idea is understood, banal, unclear, relevant, disputed, or worth deeper work.
  Evidence: self-signals align with later performance and do not merely express mood.

`C8. Routing`
: Turn a learning moment into the next useful branch. Evidence: `dig_deeper`, `make_case`,
  `make_workbook`, `make_note`, or `use_in_project` signals lead to a real follow-up artifact.

## Problem-To-Capability Map

| Problem | Capability Blocked | Main Failure Mode |
|---|---|---|
| P1 Learning usefulness | C1-C5 | Content exposure is mistaken for capability growth. |
| P2 Modality fit | C6-C8 | A runtime asks for cognitive work it cannot support. |
| P3 Signal semantics | C7-C8 | Learner feedback is collapsed into `good/easy/again`. |
| P4 Goldilocks | C1-C5 | Task difficulty ignores relevance, realism, and medium. |
| P5 Transfer | C3-C6 | Terms are recognized but methods are not used in situations. |
| P6 Open-ended routing | C8 | Interesting branches disappear after the episode. |
| P7 Evidence of progress | all | Logs measure carrier completion, not capability movement. |
| P8 Source-to-runtime integrity | all | Generated items lose source, method, or diagnostic purpose. |

This map prevents solution drift: a proposed task must name which capability it improves and which
problem it reduces.

## Evidence Types

Evidence should be stored and interpreted by type. A single episode may emit several evidence types.

`recognition_evidence`
: The learner recognized the situation type, distinction, or method move.

`judgment_evidence`
: The learner selected among plausible options and the distractors diagnose known failure modes.

`explanation_evidence`
: The learner explained why an answer is strong, partial, premature, or context-dependent.

`transfer_evidence`
: The learner applied the method to a changed context or personal project.

`artifact_evidence`
: The learner produced a carrier that can be inspected later.

`calibration_evidence`
: The learner reported clarity, novelty, relevance, uncertainty, dispute, or boredom.

`routing_evidence`
: The learner requested or accepted a next branch.

`followup_evidence`
: A later generated artifact/session actually used the routing signal.

## Indicator Roles

### Constraints

- Offline-first runtime execution.
- Local-first personal context.
- Traceable source-to-runtime provenance.
- Medium fit: do not require long writing on a device that cannot support it.
- No claim of progress without capability-linked evidence.

### Targets

- Increased frequency of useful `new_angle`, `relevant_now`, `dig_deeper`, `make_case`,
  `make_workbook`, and `use_in_project` signals.
- Reduced frequency of "banal", "vague", "contrived", and "unclear because item is badly framed".
- Better first-method-move selection on realistic bad descriptions.
- Better explanations of why tempting alternatives are premature or weaker.
- More follow-up artifacts that connect learning to projects or personal knowledge work.
- More source-restorable evidence chains from course source to runtime item to learner response to
  next boundary.

### Observations / Anti-Goodhart

Watch but do not optimize:

- number of generated packs;
- number of completed items;
- total time spent;
- streaks;
- high self-ratings;
- average difficulty;
- UI polish;
- source coverage percentage.

These can correlate with progress, but optimizing them directly can destroy usefulness.

## Baseline Probes

Before heavy implementation, use small probes to characterize the current boundary.

The first concrete probe set is recorded in `baseline-probes.md`.

### Probe A: First Method Move

Input: a bad task description from Biscuit or another real project.

Task: choose the first methodical move:

- clarify target system;
- split roles;
- identify supersystem constraint;
- identify failure risk;
- ask for evidence;
- produce a carrier.

Evidence: selected option, confidence, explanation after reveal, and whether the learner marks it
known/new/unclear/relevant.

### Probe B: Distractor Explanation

Input: the same case with plausible wrong options.

Task: explain why one tempting option is premature.

Evidence: can the learner identify the specific collapsed distinction or boundary error?

### Probe C: Real Transfer

Input: a second case from a different domain, such as French service interaction, project planning,
or personal development.

Task: choose whether the same method applies or a different first move is needed.

Evidence: transfer without overfitting the first example.

### Probe D: Artifact Rewrite

Input: a short weak task statement.

Task: produce a better version with explicit target, constraints, evidence, or next action.

Best medium: web/iOS/workbook, not M5Paper.

Evidence: resulting artifact and Mac-side diagnostic interpretation.

### Probe E: Open Branch

Input: any propagation/calibration episode.

Task: choose whether to retire, deepen, make case, make workbook, make note, or connect to a project.

Evidence: the next generated artifact uses the routing signal.

## What Solved Looks Like

The system is improving when these claims become supportable:

1. The learner can name why a session was useful or not useful in diagnostic terms.
2. The twin can explain why it chose the next boundary using recent evidence.
3. M5Paper sessions create low-friction recognition/calibration evidence, not fake writing tasks.
4. Writing-heavy tasks move to workbook/web/mobile projections.
5. Generated packs stop ending with generic unscaffolded reflection prompts.
6. The learner increasingly catches method mistakes in realistic situations.
7. The learner can request deeper exploration and later receives a relevant follow-up.
8. Evidence chains are source-restorable: source -> intent -> runtime -> response -> interpretation
   -> next artifact.

## What Does Not Count As Solved

- StudyPacks sync reliably but remain shallow.
- Logs show high completion but no useful routing.
- The UI offers more buttons but signals still collapse distinct reactions.
- More packs are generated from the same weak template.
- M5Paper gains features that belong to web/workbook modalities.
- The Mac twin summarizes progress without linking it to concrete evidence.

## Reopen Triggers

Reopen this characterization if:

- M5Paper keyboard input becomes reliable enough for writing-heavy work;
- workbook scanning becomes too high-friction for real use;
- user feedback shows the signal vocabulary is too large or awkward;
- generated follow-ups do not feel connected to the original routing signals;
- external source material changes the target competencies;
- evidence shows the current capability stack misses an important dimension.
