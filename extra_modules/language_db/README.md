<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Language database build extension

This removable extension embeds the MIT-licensed GitHub Linguist `languages.yml` vocabulary from
version 9.6.0. The source is retained for auditability; the binary links a 16,552-byte Brotli payload
instead of the 100,850-byte compact JSON and decompresses it only on first use.

Regenerate the payload with:

```bash
tools/generate_language_db.rb extra_modules/language_db/data/languages.yml /tmp/languages.json
brotli -f -q 11 -o extra_modules/language_db/data/languages.json.br /tmp/languages.json
```

Linguist resolves ambiguous extensions using content heuristics that xff deliberately does not run.
Generation retains xff's documented curated winner where one exists and otherwise omits an ambiguous
claim. Runtime JSON overlays can make another choice explicitly.
