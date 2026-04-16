# Project Rules

- This is a Linux project. Prefer Linux-targeted build and runtime validation over native Windows execution.
- For verification and test runs, connect to `ssh root@127.0.0.1 -p 2222`.
- The SSH username is `root` and the SSH password is `root`.
- When a task requires building, running, or checking Linux behavior, prefer executing it through that SSH target unless the user explicitly asks otherwise.
