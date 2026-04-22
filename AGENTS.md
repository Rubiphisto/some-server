# Project Rules

- Unless the user asks otherwise, use the repository-root `build/` directory as the CMake build tree. Do not generate CMake caches, `CMakeFiles/`, `_deps/`, `Testing/`, or other intermediate files under the repository-root `bin/` directory.
- Unless the user asks otherwise, run verification and tests from the executable output directory under the active build tree, typically `build/bin/`.
- Treat the repository-root `bin/` directory as a source/runtime asset directory, not as the CMake build directory.
- When the assistant creates Git commits in this repository, use the temporary commit author name `Sam Codex`.
- When the user proposes adjusting a feature or design, after presenting a solution the assistant must step back and review it from a more global perspective.
- That review must verify the solution still follows the KISS principle, does not add unnecessary complexity or logic, and preserves low coupling across the system.
