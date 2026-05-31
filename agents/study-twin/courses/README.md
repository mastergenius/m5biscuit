# Course Source Tree

Status: working convention, 2026-05-18.

Course sources are durable Markdown/YAML material. StudyPacks are compiled from these sources and
may be regenerated, replaced, or deleted.

## Directory Shape

```text
agents/study-twin/courses/<course-id>/
  README.md
  course.yaml
  methodology.md
  tracks/
    <track-id>.md
  concepts/
    <concept-id>.md
  patterns/
    <pattern-id>.md
  scenarios/
    <scenario-id>.md
  assessments/
    <assessment-id>.md
  maps/
    <map-id>.md
  media/
    <asset-notes>.md
```

Use this shape when it helps. Small courses may start with fewer files and split later.

## Frontmatter

Each source file should use YAML frontmatter where useful:

```yaml
---
id: fr.service.haircut.booking
type: scenario
domain: fr-practical
course: fr-service-communication
competencies:
  - book-service-appointment
  - negotiate-simple-constraints
goldilocks:
  base: can-use-politeness-frame
  stretch: can-change-time-slot-and-service
provenance:
  sources:
    - user-goal: everyday French service communication, haircut appointment
---
```

The prose body should remain readable by humans. YAML blocks can add machine-readable seeds for
generation, but the course should not become opaque JSON disguised as Markdown.

## Assessment Seeds

Assessment files can include machine-readable blocks for future pack generation:

```yaml
study_item:
  type: choice
  objective: distinguish useful service-booking phrasing from literal translation
  prompt: Which answer best asks for a haircut appointment tomorrow afternoon?
  choices:
    - id: a
      text: Je voudrais prendre rendez-vous pour demain apres-midi.
      interpretation:
        verdict: strong_fit
        diagnoses:
          - uses polite service-booking frame
          - provides time window
    - id: b
      text: Je veux une coupe demain.
      interpretation:
        verdict: works_but
        diagnoses:
          - understandable but too abrupt for the setting
```

The generator may compile this into different projections: an M5Paper StudyPack item, a Mac app
exercise, an iPad card, a Stackchan dialogue, or a printable PDF exercise.

## Source-To-Pack Rule

Every generated item should be explainable from the source tree:

- which source file it came from;
- which competency it targets;
- which misconception or boundary it probes;
- which media projection constraints shaped the output;
- what ReviewEvent evidence should come back.

This keeps generated packs disposable while preserving the learning system's memory.

## Validation

Validate a course source tree before using it as generator input:

```bash
python3 agents/study-twin/tools/validate-course agents/study-twin/courses/fr-practical-service
python3 agents/study-twin/tools/validate-course agents/study-twin/courses/fpf-practical-thinking
```
