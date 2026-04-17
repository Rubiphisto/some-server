# Project Rules

- This is a Linux project. Prefer Linux-targeted build and runtime validation over native Windows execution.
- For verification and test runs, connect to `ssh root@127.0.0.1 -p 2222`.
- The SSH username is `root` and the SSH password is `root`.
- When a task requires building, running, or checking Linux behavior, prefer executing it through that SSH target unless the user explicitly asks otherwise.
- When the assistant creates Git commits in this repository, use the temporary commit author name `Sam Codex`.
- Do not change the repository or global Git `user.name` configuration; the temporary author override applies only to assistant-created commits.
- Keep the existing Git commit email unchanged unless the user explicitly asks to change it.
