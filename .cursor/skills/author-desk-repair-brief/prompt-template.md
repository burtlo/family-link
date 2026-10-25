# Deprecated — use session plan in chat

This skill no longer emits a **copy-paste repair prompt** for a second chat.

Use [session-plan.md](session-plan.md) and present the plan **in the same
conversation**, then invite the user to say **go** to start investigation and
fixes here. See [SKILL.md](SKILL.md) §5.

The block below is kept only as a structural reference for migrations.

<details>
<summary>Legacy repair-agent prompt shape (reference only)</summary>

```markdown
Mode: Review, implement, and verify
Target: client/device/
…
```

</details>
