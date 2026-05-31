# Assessment Model

Status: working policy, 2026-05-25.

Assessment is diagnostic. A response is not primarily a grade; it is evidence about the learner's
current model, transfer ability, effort, and calibration.

## Response Dimensions

The twin should interpret responses across multiple dimensions:

- `task_success`: did the response solve the local task?
- `context_fit`: would this response be appropriate in this situation?
- `precision`: is the response specific enough?
- `transfer`: did the learner apply the pattern outside the exact example?
- `fluency`: how much cognitive load or friction appeared?
- `calibration`: did confidence match performance?
- `risk`: could the response create misunderstanding, social friction, or wrong action?
- `repairability`: what feedback or next item would most improve the learner's model?

For methodology learning, self-report needs additional routing dimensions:

- `understanding`: clear, unclear, or misleading;
- `novelty`: known, new angle, or too basic;
- `relevance`: relevant now, not relevant, or needs better context;
- `next_routing`: dig deeper, make a case, make a workbook, make a note, or connect to a project.

These dimensions answer different questions and should not be averaged into a single ease score.

## Verdict Vocabulary

Runtime rubrics should support richer verdicts than `right` and `wrong`.

- `strong_fit`: cleanly solves the task in context.
- `acceptable`: works, but is not the strongest version.
- `works_but`: locally usable with an important caveat.
- `context_dependent`: can be good or bad depending on missing context.
- `partial`: captures one useful piece but misses a needed part.
- `overformal`: correct form but socially or operationally too formal.
- `under_specified`: lacks enough detail to be useful.
- `literal_translation`: mirrors another language instead of using the target pattern.
- `common_misconception`: matches a known wrong model.
- `off_goal`: does not address the requested goal.
- `inadmissible`: violates a hard rule for the task.
- `unsafe`: could cause meaningful harm or unacceptable risk.
- `skipped`: learner chose not to answer.
- `confused`: response indicates the item itself, the source model, or the learner model needs
  repair.

The exact runtime schema may use compact action IDs, but generator and import tools should map those
actions back to this diagnostic vocabulary.

## Runtime Actions

Anki-like actions such as `again`, `good`, and `easy` are acceptable only for memory-shaped episodes.
They are not expressive enough for propagation, calibration, or case-first methodology work.

For M5Paper, the preferred interaction is a short learner signal screen after a reveal:

- understood or unclear;
- known, new angle, or too basic;
- relevant now or not relevant;
- optional routing: dig deeper, make case, make workbook, make note, use in project.

Richer clients may collect several dimensions in one episode. M5Paper may collect one or two per
episode to keep the interaction low-friction.

## Multiple Choice Items

Choice items are allowed, but distractors must not be random. Each option should be a diagnostic
probe:

- why a learner might select it;
- what selecting it reveals;
- why it works, fails, or depends on context;
- what follow-up material it should trigger.

Good feedback explains both selected and unselected options. This is especially important for "yes,
but" answers where the learner needs judgment rather than memorization.

## Open Responses

Open responses should be captured as raw text or structured notes when the runtime supports it. A
small device may collect only coarse self-rating, but richer clients should preserve the answer for
Mac-side interpretation.

The learner model should not overfit one response. It should update confidence only when several
signals agree: outcome, effort, latency, repeated errors, self-rating, and transfer behavior.

## Review Log Implications

ReviewEvent logs should evolve toward carrying:

- selected action or choice ID;
- optional raw answer;
- learner self-rating;
- latency and retry/skip signals;
- friction signals;
- runtime context;
- pack/item provenance;
- optional diagnostic verdict assigned by the runtime;
- Mac-side diagnostic verdict assigned later by the twin.
