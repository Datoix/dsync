---
name: suggest-commit-message
description: Suggest one short scoped conventional-commit message for the pending git changes, without committing. Use after finishing code or documentation edits when the user has not asked you to commit.
---

# Suggest commit message

After code/doc changes (unless the user asked to commit or said not to):

1. Check what is pending:

   ```bash
   git status
   git log -5 --oneline
   ```

2. Match the wording (type + scope) of recent commits, then suggest **one** short message for the
   **pending** changes only.
3. **Do not commit** — just propose the message.

Use **scoped** conventional commits: `type(scope): summary` (e.g. `feat(wifi): …`, `fix(i2c): …`,
`docs(readme): …`). Common types: `feat` / `fix` / `refactor` / `docs` / `build` / `chore`.

Match scopes already used in this repo (`audio`, `bt`, `board`, `readme`, `log`, …). Examples from
the project history:

- `fix(audio): make I2S start safe when A2DP starts twice`
- `refactor(bt): split sink modules and flatten audio/led helpers`
- `docs(readme): usable build/pair guide, drop pin table`
- `chore(log): stream metrics for A2DP phone vs PC compare`
