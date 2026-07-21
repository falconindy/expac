#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Tests for target resolution: stdin '-' targets, missing format string,
and not-found exit codes across corpora."""

import expac_test


class TestTargets(expac_test.TestCase):
    def testMissingFormatStringIsAnError(self):
        r = self.Expac([])
        self.assertEqual(1, r.returncode)
        self.assertIn('missing format string', r.stderr.decode())

    def testStdinTargetsWhitespaceSeparated(self):
        r = self.Expac(['%n', '-'], stdin=b'foo   bar\nbaz\n')
        self.assertEqual(0, r.returncode)
        self.assertEqual({'foo', 'bar', 'baz'}, set(r.stdout.decode().split()))

    def testStdinDeduplicatesTargets(self):
        r = self.Expac(['%n', '-'], stdin=b'foo foo foo\n')
        self.assertEqual(0, r.returncode)
        self.assertEqual('foo\n', r.stdout.decode())

    def testStdinEmptyIsAnError(self):
        r = self.Expac(['%n', '-'], stdin=b'   \n  \n')
        self.assertEqual(1, r.returncode)
        self.assertIn("empty stdin", r.stderr.decode())

    def testStdinCombinedWithRegularTargets(self):
        r = self.Expac(['%n', 'qux', '-'], stdin=b'foo\n')
        self.assertEqual(0, r.returncode)
        self.assertEqual({'qux', 'foo'}, set(r.stdout.decode().split()))

    def testExitCodeOneWhenNothingFound(self):
        r = self.Expac(['%n', 'nope-nope-nope'])
        self.assertEqual(1, r.returncode)


if __name__ == '__main__':
    expac_test.main()
