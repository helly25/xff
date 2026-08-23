#!/usr/bin/env python3

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import generate_mime_db


class GenerateMimeDbTest(unittest.TestCase):
    def test_resolves_sources_then_prefers_application_within_one_source(self):
        database = {
            "text/x-old": {"source": "apache", "extensions": ["x"]},
            "text/x-standard": {"source": "iana", "extensions": ["x", "y"]},
            "application/x-standard": {"source": "iana", "extensions": ["y"]},
        }
        generated = generate_mime_db.generate(database)
        self.assertNotIn("extensions", generated["text/x-old"])
        self.assertEqual(["x"], generated["text/x-standard"]["extensions"])
        self.assertEqual(["y"], generated["application/x-standard"]["extensions"])


if __name__ == "__main__":
    unittest.main()
