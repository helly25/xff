#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
# SPDX-License-Identifier: Apache-2.0
"""Select GitHub Actions caches that violate xff's per-entry size limit."""

from __future__ import annotations

import json
import sys
from collections.abc import Iterable
from typing import Any


MAX_CACHE_BYTES = 700_000_000


def oversized_cache_ids(caches: Iterable[dict[str, Any]]) -> list[int]:
  """Returns IDs of caches at or above the repository's size limit."""
  return [cache["id"] for cache in caches if cache["sizeInBytes"] >= MAX_CACHE_BYTES]


def main() -> int:
  caches = json.load(sys.stdin)
  if not isinstance(caches, list):
    raise ValueError("GitHub cache JSON must be an array")
  for cache_id in oversized_cache_ids(caches):
    print(cache_id)
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
