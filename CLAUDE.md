# CLAUDE.md

@AGENTS.md

## Claude Code specifics

- Do not add `Co-Authored-By` or other AI attribution trailers to commits.
- The primary dev machine is Windows; use `-A x64` / `-A Win32` when configuring, and build into `OUT/<target>` (e.g. `OUT/win64`) rather than creating new top-level build folders.
- Linux and macOS code paths usually cannot be compiled locally: when editing `src/Linux` or `src/MacOSX`, re-read the surrounding code carefully and state that the change is unverified on that platform (CI will build it).
- Prefer small, focused diffs in the hook code: these files are fragile (ref counting, render-state save/restore, threading) and regressions only show up when injected into real games.
