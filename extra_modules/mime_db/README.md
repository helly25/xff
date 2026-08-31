<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger and the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# MIME database build extension

This removable extension embeds the MIT-licensed
[`mime-db`](https://github.com/jshttp/mime-db) vocabulary. The checked-in data is generated from
version `1.54.0`, commit `5207a32f76e77ed2f63421641449f8addeacb0a5`, using:

```bash
tools/generate_mime_db.py path/to/mime-db/db.json extra_modules/mime_db/data/mime-db.json
```

Where multiple media types claim one extension, generation uses the precedence published by
`mime-types`: IANA beats unspecified, Apache, then nginx; `application/*` wins a same-source tie;
`application/octet-stream` never displaces a more specific type. The resulting JSON is an ordinary
xff vocabulary layer and remains inspectable and overrideable at runtime.
