---
name: write-ticket
description: >-
  Product work ticket from this chat: User Story, Acceptance Criteria, and
  General Information. Optional focus after /write-ticket; delegate once to
  ticket-writer with Evidence this chat.
disable-model-invocation: true
---

Write a product work ticket from **this chat**.

Optional focus: whatever the user typed **after** `/write-ticket` on the same message. If they only sent `/write-ticket`, synthesize a one-sentence goal from the chat. If there is no discernible product goal, ask once: “One plain sentence — who benefits and what should change?”

Delegate **once** to `ticket-writer` with exactly:

```
Goal: <user’s focus sentence, or your one-sentence synthesis from the chat>
Evidence: this chat.
```

Return the subagent ticket **verbatim** (`## Title` through **General Information**). No lead-in, no recap, no offers to implement.
