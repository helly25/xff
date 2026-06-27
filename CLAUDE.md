# Claude Code - xff

This repository's agent rules ([`AGENTS.md`](AGENTS.md)) and C++ coding style
([`STYLE_CPP.md`](STYLE_CPP.md)) are **binding**. Read and follow both before
writing or reviewing code; `STYLE_CPP.md` is canonical for C++ (it covers, among
much else: `absl::Mutex` + full thread-safety annotations, `absl::StatusOr` and
the `MBO_*` status macros in production and tests, and gmock matchers).

The files below are imported so they are always in context:

@AGENTS.md
@STYLE_CPP.md
