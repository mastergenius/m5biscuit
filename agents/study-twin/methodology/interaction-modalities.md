# Learning System v2: Interaction Modalities

Status: accepted direction, 2026-05-25.

This note records the shift from "StudyPacks as flashcards" to "StudyPacks as small learning
objects projected into different interaction modalities." The goal is open-ended capability growth:
the learner should start recognizing better distinctions, acting with better methods, and routing
interesting boundaries back to the Mac-side study twin.

## Core Decision

M5Paper is not an Anki clone and should not be the primary writing surface. Its best role is
episodic low-friction propagation: 10-15 minute sessions that expose useful ideas, train pattern
recognition, calibrate understanding, and collect routing signals for the next learning step.

Deep thinking by writing belongs to richer projections:

- Mac/web/iOS workbench for typed structured work;
- printable PDF workbooks for paper-first thinking and later scanning;
- future M5Paper keyboard mode for distraction-free notes, if Bluetooth input becomes reliable.

StudyPacks are therefore not one UI format. They are compiled runtime bundles whose episodes declare
the activity shape they need.

## Activity Modalities

`propagation`
: Short guided material for "soaking" in a method, concept, or distinction. It should feel like a
  concise practical guide with interaction, not like a trivia card. Best on M5Paper.

`calibration`
: A judgment task with immediate explanation. Example: "Here is a bad task description. Which
  methodological move should come first: clarify target system, split roles, locate supersystem
  constraint, or identify failure risk?" Best on M5Paper and mobile.

`recognition_drill`
: Fast pattern recognition across variants. It trains noticing, not long-form production. Best on
  M5Paper and mobile.

`worked_example`
: A model solution showing how a strong practitioner thinks through the case. Worked examples may
  later fade into guided practice as the learner gains fluency.

`guided_decomposition`
: A scaffolded task with explicit slots: target system, roles, interests, constraints, risk,
  admissibility, evidence, next action. Best on web/iOS/workbook; M5Paper can only use compressed
  versions.

`artifact_task`
: A task that changes a real carrier: rewrite a project task, make a claim/evidence note, sketch a
  system map, produce a short decision frame. Best on web/iOS/PDF.

`workbook`
: A printable or tablet-friendly deep-work sheet. It is for thinking by writing, not quick tapping.
  It should be scan/photo-friendly so the Mac twin can analyze the result later.

`exploration_request`
: A learner signal saying "this is worth digging into." It does not grade the current item. It routes
  the topic into the twin's exploration queue.

## M5Paper Session Grammar

M5Paper packs should normally use this grammar:

1. compact situation or idea;
2. one concrete example or bad description;
3. one recognition/calibration action;
4. concise reveal explaining the method and the tempting mistakes;
5. optional contrast case;
6. learner signal screen.

They should not end with generic open-ended prompts such as "think of a 24-hour experiment" unless
the prompt includes a scaffold and a concrete carrier.

## Learner Signal Matrix

Runtime actions must distinguish learning evidence from routing intent.

### Understanding

- `clear`: I understand the point.
- `unclear`: I do not understand the point or the item framing.
- `misleading`: I think the item is wrong, too compressed, or badly framed.

### Novelty

- `known`: this was already obvious or stable.
- `new_angle`: this added a useful new distinction.
- `too_basic`: this is below the useful boundary.

### Relevance

- `relevant_now`: this applies to a current project, decision, or recurring situation.
- `not_relevant`: understandable but not useful now.
- `needs_context`: I need a better situated example before judging relevance.

### Next Routing

- `dig_deeper`: generate a deeper branch.
- `make_case`: turn this into a case-first exercise.
- `make_workbook`: turn this into a writing task or printable worksheet.
- `make_note`: preserve this as a seed for the personal knowledge base.
- `use_in_project`: connect this to an active project or task.

These signals should be logged as structured ReviewEvents. A small runtime may expose only a subset
at a time, but the model must not collapse them into `good`, `easy`, or `again`.

## Multiple Choice Policy

Choice items are useful when they train judgment. Distractors should represent real failure modes:

- choosing a method too early;
- confusing role, capability, method, and work;
- optimizing a carrier instead of the target system;
- treating a missing boundary as a local implementation detail;
- selecting a plausible but premature action.

Good feedback explains why the selected option is strong, partial, premature, context-dependent, or
wrong. It should also explain why the attractive wrong options are attractive.

## Workbook Policy

Workbook tasks are the preferred projection for writing-heavy learning.

A workbook task should produce a scannable artifact:

- one page per task when possible;
- explicit boxes or headings for each required field;
- short source excerpt or case context;
- a model-answer area or comparison prompt;
- a machine-readable task ID or QR code when practical.

The Mac twin should treat a scanned workbook as evidence, not as a final grade. It should extract
claims, confusions, project references, and next-boundary signals.

## Open-Endedness

Open-ended learning does not mean unscaffolded prompts. It means the system keeps future branches
open:

- a quick M5Paper item can spawn a deeper case;
- a case can spawn a workbook;
- a workbook can spawn a project note;
- a project note can spawn a new pack;
- repeated boredom or clarity can retire a boundary;
- repeated relevance can promote a boundary into active project support.

The twin owns that routing. Runtime clients collect signals with minimal friction.

## Anti-Goals

- Do not optimize M5Paper around long text entry before the keyboard path is reliable.
- Do not use Anki-style actions as the main semantic model for methodology learning.
- Do not generate many similar packs with generic final reflection prompts.
- Do not treat completion rate as evidence of usefulness.
- Do not make every item personally "tiny" if the result feels contrived. Use realistic situations
  and compress the interaction, not the substance.

## Pilot Direction

The next MIM pilots should be rebuilt as case-first propagation/calibration packs:

1. one bad task or project description;
2. one first-method-move question;
3. diagnostic options with explanations;
4. one contrast case;
5. one learner signal screen with novelty, clarity, relevance, and routing.

The first richer projection should be a printable workbook for one MIM topic, because paper supports
thinking by writing without forcing M5Paper to become a text-entry device.
