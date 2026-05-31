---
id: fr.assessment.service-seeds
type: assessment-seed
domain: practical-french
course: fr-practical-service
competencies:
  - fr.competency.book-service-appointment
  - fr.competency.compose-slot-sentence
  - fr.competency.repair-understanding
---

# Service Assessment Seeds

These seeds should compile into short M5Paper items, richer Mac app exercises, or printable drills.

```yaml
study_item:
  id: fr.seed.book-haircut-tomorrow
  type: retrieve
  objective: build a polite service appointment request
  prompt_ru: "Соберите по-французски: я хотел бы записаться на стрижку завтра днем."
  expected_patterns:
    - "Je voudrais prendre rendez-vous pour une coupe."
    - "C'est possible demain apres-midi ?"
  diagnoses:
    strong_fit:
      - uses je voudrais
      - names the service
      - adds time as a separate slot
    partial:
      - service or time missing
    literal_translation:
      - translates Russian word order instead of using rendez-vous frame
```

```yaml
study_item:
  id: fr.seed.haircut-boundary
  type: apply
  objective: state a practical haircut preference and boundary
  prompt_ru: "Скажите мастеру: чуть короче по бокам, но не слишком коротко сверху."
  expected_patterns:
    - "Un peu plus court sur les cotes, mais pas trop court dessus."
    - "Pas trop court ici."
  diagnoses:
    strong_fit:
      - expresses degree
      - sets a negative boundary
    acceptable:
      - uses pointing/deictic ici when vocabulary is missing
    under_specified:
      - says only court without boundary
```

```yaml
study_item:
  id: fr.seed.repair-understanding
  type: choice
  objective: choose repair over fake agreement
  prompt_ru: "Мастер быстро спрашивает что-то непонятное. Что лучше сделать?"
  choices:
    - id: repeat
      text: "Vous pouvez repeter ?"
      verdict: strong_fit
      explanation_ru: "Ремонтирует понимание и снижает риск."
    - id: nod
      text: "Молча кивнуть."
      verdict: common_misconception
      explanation_ru: "Социально легко, но операционно опасно."
    - id: yes
      text: "Oui."
      verdict: under_specified
      explanation_ru: "Может сработать только если вы действительно поняли вопрос."
```
