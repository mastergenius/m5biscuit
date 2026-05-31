---
id: fpf.assessment.management-seeds
type: assessment-seed
domain: engineering-management-thinking
course: fpf-practical-thinking
competencies:
  - fpf.competency.frame-work
  - fpf.competency.define-done
  - fpf.competency.handle-evidence
  - fpf.competency.write-decision-memory
---

# Management Assessment Seeds

```yaml
study_item:
  id: fpf.seed.study-pack-frame
  type: apply
  objective: frame a useful StudyPack task
  prompt_ru: "Сделайте рамку задачи: сделать StudyPack по французскому полезным."
  expected_parts:
    - signal
    - in_scope
    - out_of_scope
    - observable_done
  diagnoses:
    strong_fit:
      - names practical service communication as goal
      - excludes full French course
      - defines done as behavior, not count of cards
    common_misconception:
      - optimizes number of flash cards
```

```yaml
study_item:
  id: fpf.seed.metric-steals-goal
  type: diagnostic
  objective: detect metric-goal substitution
  prompt_ru: "Пользователь сделал 200 карточек, но не может записаться на стрижку. Где сбой?"
  diagnoses:
    strong_fit:
      - says card count became hidden goal
      - proposes behavioral check
    partial:
      - says cards were bad without naming metric substitution
```

```yaml
study_item:
  id: fpf.seed.file-transfer-decision
  type: apply
  objective: write compact decision memory
  prompt_ru: "Запишите решение по File Transfer: что выбрали, какой инвариант, что проверить потом."
  expected_parts:
    - chosen_option
    - security_invariant
    - follow_up_checks
  diagnoses:
    strong_fit:
      - token-gated explicit mode
      - no read/write without valid token
      - mentions TTL/logging/untrusted network checks
    under_specified:
      - says secure it without an invariant
```

```yaml
study_item:
  id: fpf.seed.sd-empty-rivals
  type: apply
  objective: keep rival explanations during debugging
  prompt_ru: "После EPUB file browser показал 0 файлов при mounted SD. Дайте 2 причины и дешевую проверку."
  diagnoses:
    strong_fit:
      - gives at least two plausible rivals
      - gives cheap observation for each
    common_misconception:
      - asserts one cause without test
```
