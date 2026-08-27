#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 M. Boerger, The helly25 authors
# SPDX-License-Identifier: Apache-2.0
"""Tests for cache_size.py."""

from __future__ import annotations

import unittest

import cache_size


class CacheSizeTest(unittest.TestCase):

  def test_selects_entries_at_or_above_limit(self):
    self.assertEqual(
        cache_size.oversized_cache_ids(
            [
                {"id": 1, "sizeInBytes": cache_size.MAX_CACHE_BYTES - 1},
                {"id": 2, "sizeInBytes": cache_size.MAX_CACHE_BYTES},
                {"id": 3, "sizeInBytes": cache_size.MAX_CACHE_BYTES + 1},
            ]
        ),
        [2, 3],
    )

  def test_empty_inventory_needs_no_cleanup(self):
    self.assertEqual(cache_size.oversized_cache_ids([]), [])


if __name__ == "__main__":
  unittest.main()
