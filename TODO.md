# TODO

Open, cross-cutting decisions to revisit. Code-level TODOs live in comments;
deferred features live in the CHANGELOG. This file is for design choices that are
shipped one way but not yet settled.

## Open decisions

- **Modern (non-`find`) default time format: `space` vs `rfc3339`.**
  Shipped as `space` (`2026-06-22 14:30:00 +0100`), the most human-readable form
  (it matches GNU `ls --time-style=long-iso`/`full-iso`), chosen for a human-first
  default. `rfc3339` (`2026-06-22T14:30:00+01:00`) is the alternative if the
  no-flag default should instead be a machine-interoperable standard out of the
  box. Revisit before v1, or when the mode system (#54) lands. `find`/strict mode
  formats times as `asctime` regardless of this choice.
