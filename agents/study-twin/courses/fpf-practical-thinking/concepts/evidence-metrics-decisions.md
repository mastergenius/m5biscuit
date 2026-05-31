---
id: fpf.concept.evidence-metrics-decisions
type: concept
domain: engineering-management-thinking
course: fpf-practical-thinking
related_runtime_concepts:
  - fpf.practical.evidence
  - fpf.practical.rivals
  - fpf.practical.metric
  - fpf.practical.decision
  - fpf.mgmt.metric
  - fpf.mgmt.decision
---

# Evidence, Metrics, Decisions

Three failure modes show up repeatedly in engineering-management work:

1. A plausible story becomes "the cause" before observations cut down rivals.
2. A metric silently becomes the goal.
3. A decision is made but no one records what must stay true or what should be checked later.

## Practical Rules

- Keep 2-3 rival explanations until observations break them.
- Ask what role a metric plays: constraint, optimization target, or risk sensor.
- Record decisions with the chosen option, rejected options, invariants, rollback path, and later
  verification.

## Biscuit Examples

- WDT spam is a risk sensor, not the product goal.
- File Transfer security has an invariant: without a valid token, no file read/write.
- StudyPacks have an offline-first invariant: loaded packs work without network.
- EPUB page failures need rival causes: SD write issue, cache path issue, reader state, or power
  mode interaction.
