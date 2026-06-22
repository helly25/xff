# xff - Configuration System Design

> Detailed spec for the layered config system. **Refines** `design.md`
> §"Security & safety (review #2)" and §"Flag / toggle conventions"; where the
> two differ, this document is authoritative for config specifically.
> Status: **Draft** · 2026-06-21 · Org: helly25

## Purpose

A find-compatible tool that can run `-exec`/`-capture` is, by construction, a
code-execution engine. A config system for it must make the common case
convenient **without** ever letting an untrusted, repo-local file silently run
commands. The design goal, in the user's words: *"by default there is no
security issue."*

The system has **four layers** with a strict trust order, and a single
root-owned **policy** that decides how much each lower (less-trusted) layer may
do. The policy is the keystone: absence of an admin policy yields the *safe*
default, never an unsafe one.

## The four layers

Lowest → highest precedence; each overrides values set by an earlier one, and
the **CLI always wins** (typing a flag is explicit consent).

| # | Layer       | Owner / trust            | Format                  |
|---|-------------|--------------------------|-------------------------|
| 1 | built-in    | the binary               | (none)                  |
| 2 | **system**  | root - the trust anchor  | INI (structured policy) |
| 3 | **user**    | the user - trusted as-is | `.xffrc` grammar        |
| 4 | **project** | repo - **untrusted**     | `.xffrc` grammar        |
| 5 | **CLI**     | explicit user consent    | flags                   |

(Locations are detailed under Discovery below.)

- **System** holds global defaults *and* the per-flag **policy** (below). Only
  this root-owned layer may grant capability; lower layers can never widen their
  own permissions.
- **User** is trusted as the user themselves - data and armed `[exec]`/named
  blocks both honoured.
- **Project** `.xffrc` cascades like `.gitignore` (each directory's file scopes
  to its subtree; cannot add roots, redirect output, or reach outside its
  subtree - per `design.md` §132) and is **capped by the policy**: sensitive
  flags are inert unless the system policy grants the project layer.
- Provenance: every setting records `enum{unset, system, user, project, cli}`
  (not `bool`), so resolution is last-non-unset-wins and *unset* ≠ *off*
  (`design.md` §102).

## Discovery & loading

- **System:** `/etc/xff.ini` then `/etc/xff.d/*.ini` (lexical). Never skipped by
  `--no-config` (a security control, not a convenience). Absent ⇒ built-in
  default policy applies.
- **User:** `$XFF_CONFIG` if set, else `$XDG_CONFIG_HOME/xff/config`, else
  `~/.config/xff/config`.
- **Project:** auto-discovered - from each search root, every `.xffrc` from the
  filesystem root down to the entry's directory contributes to that subtree
  (cascade). Ownership-gated even for data-only settings (`design.md` §132).
- **Explicit:** `--xffrc=FILE` loads a specific file and **arms** its
  sensitive/named blocks (ownership-gated). Naming the file *is* the
  authorization - no trust DB, no hashes. This replaces the working
  `--config <file>` spelling in `design.md` §130 (which now collides with
  `--config=NAME` below).
- **`--no-config`:** skip the user + project layers (and system *defaults*);
  pure CLI + built-ins. The system *policy* still bounds anything that would
  otherwise be loaded - moot when nothing is.

## Formats

### System policy - `/etc/xff.ini` (structured)

INI: a `[defaults]` section of plain flag lines, and a `[policy]` section of
per-layer, per-flag `allow`/`deny` lists. Structured because a policy wants
typed validation and must be unambiguous.

```ini
[defaults]
--color = auto

[policy]
# Per-flag overrides on top of the built-in safety classification.
# Built-in default already denies @sensitive/@destructive at the project layer.
project.allow = --sort, --color, --format       # redundant with built-ins; explicit is fine
project.deny  = --threads                        # tighten: forbid even a "safe" flag
user.allow    = @sensitive                       # user may arm -exec/-capture from ~/.config
```

- Class tokens `@safe` / `@sensitive` / `@destructive` expand to every flag with
  that registry `safety` classification (`design.md` §134), so a *new*
  sensitive flag is covered without editing the policy - **no gaps as the tool
  grows.**
- `deny` beats `allow` on conflict. A layer can be tightened (deny a safe flag)
  or loosened (allow a sensitive one) - only by this root-owned file.

### `.xffrc` (user & project) - bazel-rc grammar

Line-oriented flag bundles ("what you can type, you can save", `design.md`
§125), each optionally prefixed by a **two-axis selector** `base:config:`:

```
# <base>:<config>:  <flags…>     base ∈ {common, xff, find, <custom>, ∅(=common)}
common:        --color=auto                 # every style, always
xff:           --feature=long-paths         # only under the xff style
xff:debug:     --feature=trace --threads=1  # only when style=xff AND --config=debug
find:          --warn                        # only under the find style
```

- `base` gates by active **style** (`xff`/`find`/custom); `common`/empty applies
  always. `:config` gates by an active **named config** (`--config=NAME`). More
  expressive than bazel's single `command:config` - a custom config can differ
  per style.
- A line is inert (parsed to AST, never executed). Sensitive flags within it are
  subject to the policy of the *layer the file belongs to*.

## Capability policy (per-flag, safe-by-default)

The user-chosen model is **per-flag allow/deny**, anchored to the registry's
built-in `safety` classification so the safe default needs no admin file:

| Class         | Project | User  | System |
|---------------|---------|-------|--------|
| `safe`        | allow   | allow | allow  |
| `sensitive`   | deny    | allow | allow  |
| `destructive` | deny    | allow | allow  |

`safe` = output / traversal / matching (`--color`, `--sort`, `-name`); `sensitive` = the exec family (`-exec`, `-execdir`, `-ok`, `-okdir`, `-capture`, `-capturedir`); `destructive` = `-delete` and future built-in mutators.

- **No `/etc/xff.ini` ⇒ this built-in table is the policy.** A repo-local
  `.xffrc` can recolor output or set a default sort; it can **not** run a
  command or delete a file. That is "no security issue by default".
- The system policy's `[policy]` lists **override** this table per flag/class
  per layer (loosen or tighten). Lower layers cannot edit policy.
- Class membership is intrinsic to each descriptor, so newly added sensitive
  flags inherit the deny without a policy edit.

### Enforcement & self-documentation

- A disallowed setting from a config layer is **dropped, not fatal** (a hostile
  `.xffrc` must never abort or DoS the run), with a self-documenting stderr note
  - never silent (`design.md` §134):

  ```
  xff: ignoring `-exec` from ./.xffrc - denied for the project layer [security];
       arm it explicitly with `--xffrc=./.xffrc`, or allow in /etc/xff.ini.
  ```
- `--explain` lists every config source consulted, what each contributed, and
  every gate that fired with its *why*.
- Interacts with `--safe` (`design.md` §136): `--safe` is an orthogonal,
  user-facing hard-refuse of destructive/dangerous ops regardless of layer; the
  policy governs *where settings may originate*. Both can refuse; both explain.

## CLI selectors

- **`--config=NAME`** - repeatable, **stacks** (bazel-style; later wins on
  conflict, explicit flags beat config-expanded ones). `NAME` ∈ the built-in
  styles `find` / `xff` **plus** any `:NAME` block from loaded `.xffrc` files.
  Selecting a built-in style is how you pick the base behaviour - this
  **subsumes a separate `--style` flag.** Version-extensible: `--config=xff:2`
  pins a behavioural epoch (the part that scales to "even more modern", unlike a
  binary `--modern`).
- **`--feature=NAME` / `--feature=no-NAME`** - repeatable on/off capability
  gates; the granular dial within a style (a style is just a named bundle of
  feature defaults). Valued options (`--capture-override=no`,
  `--implicit-print=no`) stay dedicated flags, **not** `--feature`.
- **`--xffrc=FILE`** - load + arm a specific file (see Discovery).
- **`--no-config`** - see Discovery.
- **`argv[0]` dispatch** - invoked as `find` (alias/symlink) defaults to
  `--config=find` (strict: accept only find's own primaries/options, reject
  every xff extension incl. single-dash modern primitives like `-println`);
  invoked as `xff` defaults to `--config=xff` (modern). Explicit `--config=`
  overrides. (Subsumes the standalone argv[0]-dispatch task.)

## Worked examples

```sh
# Hostile repo, no admin policy → safe by default.
cd ./untrusted && xff .
#   ./.xffrc: common: --color=never     → applied (safe)
#   ./.xffrc: common: -exec rm {} ;      → dropped + warned (sensitive, project-denied)

# Personal exec recipe from your own config → trusted.
#   ~/.config/xff/config: xff:thumbs: -capture=W=… convert {} … ;
xff --config=thumbs ~/Pictures            # armed: user layer allows @sensitive

# CI host loosens the project layer for a vetted pipeline.
#   /etc/xff.ini: [policy] project.allow = -capture
cd $CI_WORKSPACE && xff .                 # repo .xffrc -capture now armed

# Strict drop-in via name.
ln -s xff find && ./find . -println        # → error: -println unknown (find style)
```

## Implementation roadmap (small PRs)

- **A** [#58]: this spec (reviewable design).
- **B** [#58]: config loader skeleton - parse INI + `.xffrc` grammar, layer precedence, provenance enums, `--no-config`, `--xffrc=FILE`, `--config=NAME` selection (no gating yet).
- **C** [#58]: policy gate - registry `safety` classification, built-in default table, `[policy]` overrides, drop-and-warn enforcement, **hostile-`.xffrc` rejection tests**.
- **D** [#54, #59]: wire `--config=find|xff` styles + `--feature`, tag every descriptor find-vs-xff, strict-style filter + conformance.
- **E** [#59]: `argv[0]` default style; cascading project discovery; `--explain` config trace.

## Open questions

- `/etc/xff.d/` drop-in dir now, or single `/etc/xff.ini` first?
- Class tokens (`@sensitive`) in the `[policy]` lists from day one, or per-flag
  names only until proven necessary?
- Should `--no-config` also drop system *defaults*, or only user + project?
- Windows: system path (`%PROGRAMDATA%\xff`) and user path mapping (deferred with
  the rest of Windows support - `design.md` §Non-Goals).

## Prior art

- **ripgrep:** config only via an explicit `RIPGREP_CONFIG_PATH` env var - no
  ambient discovery. (We add cascading project files, but gate them.)
- **fd:** ships *no* config file at all - the maximally safe stance.
- **sudoers / ssh:** a root-owned system file defines policy that user files
  cannot override - the model for our system-over-project trust anchor.
- **bazel:** `--config=NAME` named configs and rc layering - adopted, minus
  bazel's workspace auto-load (the exact hole we must not reproduce).
