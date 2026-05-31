# Learning System v2: Problem Framing

Status: active framing, 2026-05-25.

This document frames the problem before choosing product or implementation solutions. It uses FPF
discipline: signal before solution, scope before design, characteristic space before comparison, and
evidence before claims of progress.

For the deeper capability/evidence characterization, see `capability-evidence-model.md`.

## Framing Rule

Do not start from "build better StudyPacks", "change M5Paper UI", or "make an app". Those are
candidate enabling-system changes. The target system is the learner's capability to understand and
act with better method in real situations.

## Observation That Does Not Fit

The current StudyPack loop can work technically:

- packs sync to the device;
- M5Paper opens them offline;
- sessions emit ReviewEvents;
- logs come back to the Mac-side tooling.

But the learning experience can still feel low-value:

- a pack often carries one abstract idea without enough situated method;
- final "think about it" prompts are repetitive and unscaffolded;
- `good`, `easy`, and `again` do not express actual learner reactions;
- completion logs can look successful while the user does not feel more capable;
- M5Paper asks for thinking that often requires writing, but its input surface is not a writing
  surface.

The contradiction is the signal: runtime completion is not evidence of useful learning.

## Object, Description, Carrier

`Target object`
: The learner's practical capability: seeing distinctions, selecting methodical moves, acting in
  real project/life situations, and improving over time.

`Descriptions`
: Course notes, methodology documents, learner model hypotheses, pack manifests, rubrics, logs, and
  progress summaries.

`Carriers`
: M5Paper screens, JSONL StudyPacks, web/iOS UI, Mac agent state, PDF workbooks, printed paper,
  scans/photos, Obsidian/Brain notes, and project artifacts.

The carriers may look complete while the target object is not improving. Evidence must therefore
refer back to capability, not only to carrier success.

## Target And Enabling Systems

`Target system`
: A local-first adaptive learning loop that helps the learner grow practical capability.

`Enabling systems`
: M5Paper firmware, StudyPack schema, review logs, sync server, Mac study twin, web/iOS runtime,
  workbook generator, scanner/import pipeline, source-course repository, and personal-context
  retrieval.

The enabling systems are allowed to change if the target system is better served.

## Scope

In scope:

- methodology for useful adaptive learning;
- problem portfolio and acceptance criteria;
- activity modalities and learner-signal semantics;
- StudyPack source-to-runtime discipline;
- M5Paper as one runtime projection;
- Mac-side learner model and routing loop;
- printable/workbook and web/mobile projections as first-class options.

Out of scope for this framing document:

- choosing the next implementation task;
- committing to a specific mobile framework;
- final schema migration;
- BLE keyboard work;
- full course generation automation;
- judging success by number of packs generated.

## Problem Portfolio

### P1. Learning Usefulness

Type: diagnosis + synthesis.

Signal: packs can be completed but feel shallow, repetitive, or not worth returning to.

Solved when: a short session produces at least one useful outcome: a sharper distinction, a better
method choice, a project-relevant question, a new exploration branch, or a concrete artifact request.

Weakest link: confusing content exposure with capability growth.

### P2. Modality Fit

Type: synthesis.

Signal: M5Paper is good for low-friction reading and recognition, but poor for long-form writing.

Solved when: each activity is projected onto a suitable medium:

- M5Paper for propagation, recognition, calibration, and routing;
- web/iOS/Mac for structured input and interactive work;
- PDF/paper for writing-heavy thinking;
- future keyboard mode only where reliable text input is actually needed.

Weakest link: forcing one runtime to host every cognitive activity.

### P3. Learner Signal Semantics

Type: diagnosis.

Signal: `good`, `easy`, and `again` do not distinguish "known", "unclear", "new angle",
"relevant now", "dig deeper", or "make workbook".

Solved when: ReviewEvents preserve separate dimensions for understanding, novelty, relevance, and
next-routing intent.

Weakest link: collapsing diagnostic signals into a spaced-repetition ease score.

### P4. Goldilocks Boundary Selection

Type: optimization.

Signal: tasks can be too trivial, too vague, too contrived, or too writing-heavy for the current
runtime.

Solved when: items are realistically situated, slightly stretching, and answerable within the
session context and medium.

Weakest link: optimizing item difficulty without considering medium and personal relevance.

### P5. Transfer To Real Situations

Type: synthesis.

Signal: a learner can understand a card but still not apply the method to a project, conversation,
document, or decision.

Solved when: activities train transferable method moves, such as selecting the first methodological
move for a bad task description, separating roles from capabilities, identifying target system vs
carrier, or naming failure risk.

Weakest link: testing recall of terms instead of method use.

### P6. Open-Ended Routing

Type: search + synthesis.

Signal: a learner can encounter an interesting idea but cannot say "make this deeper", "connect this
to my project", or "turn this into a workbook".

Solved when: each runtime can emit low-friction routing signals and the Mac twin can convert them
into a follow-up branch.

Weakest link: treating every episode as locally complete rather than as a possible branch point.

### P7. Evidence Of Progress

Type: characterization.

Signal: completion rates and positive self-ratings are easy to collect but weak evidence of actual
capability growth.

Solved when: the system can show evidence chains: source material -> activity intent -> learner
response -> diagnostic interpretation -> next boundary -> later improved performance or artifact.

Weakest link: relying on logs without restoring what they are evidence of.

### P8. Source-To-Runtime Integrity

Type: diagnosis + optimization.

Signal: generated packs can drift from source material, personal context, or methodology.

Solved when: every generated runtime item records its source, target competency, activity modality,
diagnostic purpose, and expected follow-up branches.

Weakest link: generation that produces plausible learning copy without traceable methodological
intent.

## Characteristic Space

### Constraints

- Offline-first: already-synced material must work without network access.
- Local-first: sensitive learner context should stay on the Mac/owned machines unless explicitly
  exported.
- Device fit: M5Paper cannot be treated as a long-form writing device until keyboard input is
  reliable.
- Source durability: durable course material stays in Markdown/YAML-like source trees; generated
  JSONL packs are expendable runtime artifacts.
- Traceability: generated items should be traceable to sources, target competencies, and learner
  model assumptions.

### Targets

Optimize for these dimensions:

- practical usefulness: sessions create better distinctions, decisions, or next actions;
- low-friction return: 10-15 minute M5Paper sessions remain attractive enough to use repeatedly;
- evidence-rich adaptation: logs and artifacts are sufficient for the Mac twin to choose a better
  next boundary;
- realistic transfer: tasks use real or realistic situations rather than tiny invented prompts;
- open-ended growth: useful ideas can branch into deeper cases, workbooks, notes, or project work.

### Watch But Do Not Optimize

- number of generated packs;
- number of completed episodes;
- average `good`/`easy` rate;
- raw time spent;
- novelty for its own sake;
- UI feature count;
- breadth of course coverage before usefulness is proven.

These indicators are useful as observations, but dangerous as primary goals.

## Acceptance Criteria

### Level 0: Baseline Understood

- Existing logs and user feedback can explain why current pack completion does not imply usefulness.
- Current methodology documents state that M5Paper is a propagation/calibration runtime, not an Anki
  clone or primary writing surface.

### Level 1: Useful Pilot

- One case-first pilot pack avoids generic final reflection prompts.
- The pilot includes a bad description or realistic situation, a first-method-move question,
  diagnostic options, explanatory reveal, and learner signal collection.
- After the session, the learner can name what was useful, unclear, too basic, or worth exploring.

### Level 2: Closed Adaptive Loop

- ReviewEvents distinguish understanding, novelty, relevance, and routing.
- The Mac twin uses at least one routing signal to generate a follow-up artifact: deeper pack, case,
  workbook, note, or project-linked prompt.
- A later session tests whether the follow-up improved recognition, judgment, or transfer.

### Level 3: Multi-Modal Learning System

- M5Paper, web/mobile, and workbook projections share methodological intent while using different
  interaction forms.
- Writing-heavy activities produce artifacts that can be scanned/imported and interpreted.
- The learner model accumulates evidence about competencies, misconceptions, relevance, and open
  branches.

## Evidence Plan

Evidence should be attached to the target capability, not only to runtime mechanics.

Useful evidence:

- before/after answers on similar bad-description tasks;
- learner signal distribution: known, new angle, unclear, relevant now, dig deeper, make workbook;
- concrete project links created from a session;
- workbook artifacts and Mac-side interpretations;
- reduced "banal / vague / contrived" feedback over successive pilots;
- improved ability to explain why attractive wrong options are wrong or premature.

Weak evidence:

- pack installed successfully;
- pack completed;
- high `good`/`easy` count;
- many generated packs;
- polished UI without better diagnostic signal.

## Framing Questions Before Next Decisions

Before choosing the next implementation task, answer:

1. Which problem from the portfolio is the task meant to reduce?
2. Which target capability does it affect?
3. Which medium is appropriate for the cognitive work?
4. What signal will show whether the task helped?
5. What should not be optimized?
6. What would make us reopen the framing?

If those answers are unclear, the proposed task is probably premature.
