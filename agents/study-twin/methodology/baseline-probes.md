# Learning System v2: Baseline Probes

Status: active probe set, 2026-05-25.

These probes execute the first step of the approved portfolio: characterize whether the learning
system can create useful evidence before investing in UI, firmware, or generator changes.

The probes target the capability stack from `capability-evidence-model.md`, especially:

- C1 situated reading;
- C2 distinction control;
- C3 first-method-move selection;
- C4 contrastive judgment;
- C7 calibration;
- C8 routing.

## Probe Protocol

For each probe, collect:

- selected answer or produced artifact;
- confidence/friction if available;
- learner signal: known, new angle, unclear, relevant now, dig deeper, make case, make workbook,
  make note, or use in project;
- short analyst note: what capability this did or did not exercise.

Do not count completion as success. Completion only means the carrier worked.

## Probe A: First Method Move

Situation:

> In Biscuit, "sync does not sync everything" and sometimes reports zero logs. A tempting reaction is
> to patch one endpoint or add another button.

Question:

Which first methodological move is strongest?

1. Clarify the target system: what counts as synchronized and why.
2. Add more retries to the HTTP client.
3. Add a new "sync all" button.
4. Generate fewer StudyPacks.

Expected analysis:

The first move is target-system clarification. Retries, buttons, and fewer packs are enabling-system
changes before the target is stated. The useful artifact is a sync contract: local dirty state,
remote accepted state, ack semantics, retry/resume policy, and evidence of completion.

Capability evidence:

- C1 situated reading;
- C2 target/enabling distinction;
- C3 first-method-move selection.

## Probe B: Modality Fit

Situation:

> A M5Paper StudyPack asks the learner to "write a rule" and "invent a 24-hour experiment." The user
> is in a 10-minute on-the-go session.

Question:

Which repair is strongest?

1. Keep the prompt but add more explanation.
2. Turn the task into recognition/calibration on M5Paper and route writing to a workbook.
3. Make the M5Paper keyboard mandatory.
4. Remove reflection from all packs.

Expected analysis:

The strongest repair is modality routing. M5Paper can train recognition, judgment, and routing; a
workbook or web/mobile surface should handle writing-heavy work.

Capability evidence:

- C2 medium/object distinction;
- C3 first repair move;
- C8 routing.

## Probe C: Distractor Explanation

Situation:

> A pack item asks: "How do we make learning useful?" One option says "generate more packs from the
> MIM sources."

Task:

Explain why "generate more packs" is attractive but premature.

Expected analysis:

It is attractive because source coverage feels like progress. It is premature because P1/P7 are not
solved: we have not shown that packs improve capability or produce useful evidence. It optimizes a
carrier metric.

Capability evidence:

- C4 contrastive judgment;
- anti-Goodhart awareness;
- source/carrier restoration.

## Probe D: Transfer Case

Situation:

> In practical French, the learner can read phrases for a haircut appointment but cannot adapt when
> the stylist asks a follow-up question.

Question:

Which first move best transfers from the MIM framing?

1. Add more isolated vocabulary.
2. Identify the service scenario states and repair moves.
3. Memorize one perfect script.
4. Skip service situations until grammar is complete.

Expected analysis:

The first move is scenario-state modeling: appointment, arrival, preference negotiation,
misunderstanding, repair, payment, next appointment. This transfers the "method before content dump"
principle across domains.

Capability evidence:

- C5 transfer;
- scenario modeling;
- avoiding content-volume optimization.

## Probe E: Open Branch

Situation:

> After a M5Paper calibration item, the learner thinks: "This is relevant to my current StudyPack
> generator problem, but I need to dig deeper."

Question:

What should the runtime collect?

1. `good`
2. `easy`
3. `dig_deeper` + optional `use_in_project`
4. no signal; the learner can remember it later

Expected analysis:

The useful signal is routing evidence. It should create a branch the Mac twin can use: deeper case,
workbook, project note, or next pack.

Capability evidence:

- C7 calibration;
- C8 routing;
- follow-up evidence if a branch is later generated.

## Probe Pass Criteria

The baseline probe set is useful if it produces at least:

- one case where the learner marks an item as too basic or already known;
- one case where the learner marks an item as unclear or badly framed;
- one case where the learner requests a deeper branch;
- one case where the learner requests workbook/artifact work;
- one analyst note that changes the next pack design.

If all probes merely complete with positive ratings, the probe set failed to collect enough
diagnostic evidence.
