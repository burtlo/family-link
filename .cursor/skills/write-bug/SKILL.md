---
name: write-bug
description: >-
  Jira bug ticket from this chat: user gives one plain Issue sentence after
  /write-bug; delegate once to bug-writer with Evidence this chat.
disable-model-invocation: true
---

Write a Jira bug ticket from **this chat**.

The user’s product issue is whatever they typed **after** `/write-bug` on the same message. If they only sent `/write-bug`, ask once: “One plain sentence — what’s wrong for operators?”

Delegate **once** to `bug-writer` with exactly:

```
Issue: <one plain sentence from the user>
Evidence: this chat.
```

Return the subagent ticket **verbatim** (`## Title` through **Team Notes**). No lead-in, no recap, no offers to fix code.
