#!/usr/bin/env python3
"""Convert pinned mime-db JSON into xff's deterministic vocabulary layer."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

_PREFERENCE = {"nginx": 0, "apache": 1, None: 2, "iana": 3}


def generate(database: dict) -> dict:
    winners: dict[str, str] = {}
    for media_type, metadata in database.items():
        for extension in metadata.get("extensions", ()):
            current = winners.get(extension)
            if current is not None:
                old = database[current]
                old_preference = _PREFERENCE.get(old.get("source"), -1)
                new_preference = _PREFERENCE.get(metadata.get("source"), -1)
                if current != "application/octet-stream" and (
                    old_preference > new_preference
                    or (old_preference == new_preference and current.startswith("application/"))
                ):
                    continue
            winners[extension] = media_type

    result = {}
    for media_type, metadata in database.items():
        entry = {key: metadata[key] for key in ("source", "compressible", "charset") if key in metadata}
        extensions = [extension for extension in metadata.get("extensions", ()) if winners[extension] == media_type]
        if extensions:
            entry["extensions"] = extensions
        result[media_type] = entry
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    database = json.loads(args.input.read_text(encoding="utf-8"))
    args.output.write_text(json.dumps(generate(database), separators=(",", ":"), sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
