---
id: fr.pattern.slot-sentence-builder
type: pattern
domain: practical-french
course: fr-practical-service
related_runtime_concepts:
  - fr.builder.slot-sentence
  - fr.builder.operators
  - fr.builder.question
  - fr.builder.negation
---

# Slot Sentence Builder

Бытовая французская фраза собирается как конструктор:

```text
speaker + operator + action/object + time/place + constraint
```

Useful operators:

- `je voudrais` - polite request;
- `je peux` - can I / may I;
- `je dois` - I need to / I must;
- `j'ai` - I have;
- `Est-ce que` + statement - simple question frame.

## Examples

- `Je voudrais prendre rendez-vous demain.`
- `Est-ce que je peux payer par carte ?`
- `Je ne veux pas trop court, seulement un peu plus court.`
- `Vous pouvez repeter ?`

## Boundary

The learner is not expected to generate elegant French. The first useful boundary is a phrase that
works socially and operationally. The generator should prefer phrase transfer over word memorization.
