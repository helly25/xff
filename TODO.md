# TODO

Open, cross-cutting decisions to revisit. Code-level TODOs live in comments;
deferred features live in the CHANGELOG. This file is for design choices that are
shipped one way but not yet settled.

## Open decisions

- **Modern (non-`find`) default time format: resolved to `space`.**
  `space` (`2026-06-22 14:30:00 +0100`) is the default: human-first (it matches
  GNU `ls --time-style=long-iso`/`full-iso` and `git log --date=iso`), still ISO-
  ordered so it sorts lexicographically, and parseable back by `ParseTimeString`.
  `--time-format` (config phase D4b) makes this a soft choice rather than a
  lock-in: `rfc3339` (`2026-06-22T14:30:00+01:00`) is one flag
  (`--time-format=rfc3339`) or one `.xffrc` line (`common: --time-format=rfc3339`)
  away for interchange-by-default, and machine consumers use `--format=jsonl`.
  (find's `-printf %t`, once implemented (#48), uses `asctime` per find.)

- **`--timezone` scope and spelling.**
  Shipped (config phase D4a) as `--timezone=ZONE`: overrides the zone used both
  to *interpret* time-string arguments (`-newerXt`) and to *format* time fields
  (`{atime}`/`{mtime}`/`{ctime}`/`{btime}`). Accepts `local`/empty,
  `utc`/`z`/`zulu`, and IANA names (`America/New_York`); an unknown zone is a
  usage error. The companion `--time-format=NAME` selector shipped alongside it
  (config phase D4b). Not yet settled / deferred: (a) a `--tz` short alias; (b)
  fixed-offset specs (`+01:00`), which `absl::LoadTimeZone` does not parse; (c)
  `-printf %t` / `-ls` time directives (a separate FormatPrintf path, not yet
  implemented - #48). Revisit with the datetime lib growth (#70).

- **Project `.xffrc` per-entry subtree scoping (deferred).**
  The cascade (config phase E2a) reads, for each search root, every `.xffrc` from
  the filesystem root down to the root's directory (ancestors), applied run-level.
  The design (design-config.md L41, L56-58) also wants gitignore-style *subtree*
  scoping: a `.xffrc` in a directory *below* a root should apply only to that
  subtree -- which means config resolution would vary per directory during the
  walk, an architectural change (per-entry layering on the traversal hot path).
  Deferred until a real need appears; the ancestor cascade already covers the
  common "repo + parents" case.
