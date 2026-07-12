<!-- SPDX-FileCopyrightText: Copyright (c) The helly25 authors (helly25.com) -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# 0.1.0

First release of xff (eXtended File Find): a `find(1)`-compatible file finder
with modern extensions. This is the basic tool - the core is implemented,
tested, and verified in CI on Linux and macOS.

Everything `find` does works the same way. On top of that, xff adds content and
type matching, structured output, per-run summaries and histograms, file
hashing, diffing, and safe deletes, under the find / xff / xfd / rg flavors. The
complete, always-current list of what is supported is the generated reference in
[XFF.md](XFF.md); the roadmap and open design questions are in [TODO.md](TODO.md).

Notable features not yet built (see [TODO.md](TODO.md)):

- archive diving (`--archive`)
- sharded-file handling
- hash verification (files can be hashed; checking a tree against a manifest is
  not done yet)
