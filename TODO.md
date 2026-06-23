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

- **`--timezone` scope and spelling.**
  Shipped (config phase D4a) as `--timezone=ZONE` overriding the zone used to
  *interpret* time-string arguments (`-newerXt`): accepts `local`/empty,
  `utc`/`z`/`zulu`, and IANA names (`America/New_York`); an unknown zone is a
  usage error. Not yet settled / deferred: (a) a `--tz` short alias; (b)
  fixed-offset specs (`+01:00`), which `absl::LoadTimeZone` does not parse; (c)
  wiring the same zone into time-field *formatting* (`{mtime}` / `-printf %t` /
  jsonl, currently always local) - the display half of the flag; (d) the
  companion `--time-format=NAME` selector (also where the `space` vs `rfc3339`
  default above is chosen). Revisit with the datetime lib growth (#70).
