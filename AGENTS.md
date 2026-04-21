# Project Rules

- This is a Linux project. Prefer Linux-targeted build and runtime validation over native Windows execution.
- When a task requires building, running, or checking Linux behavior, prefer executing it through that SSH target unless the user explicitly asks otherwise.
- Unless the user asks otherwise, run verification and tests with the executable `bin/` directory as the working directory.
- When the assistant creates Git commits in this repository, use the temporary commit author name `Sam Codex`.
- Do not change the repository or global Git `user.name` configuration; the temporary author override applies only to assistant-created commits.
- Keep the existing Git commit email unchanged unless the user explicitly asks to change it.
- When the user proposes adjusting a feature or design, after presenting a solution the assistant must step back and review it from a more global perspective.
- That review must verify the solution still follows the KISS principle, does not add unnecessary complexity or logic, and preserves low coupling across the system.
