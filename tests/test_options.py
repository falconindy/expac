#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Tests for general CLI options: --version, --help, --config, and the
default delimiter/verbose behavior not already covered by test_format.py."""

import os

import expac_test


class TestOptions(expac_test.TestCase):
    def testVersion(self):
        for flag in ('-V', '--version'):
            r = self.Expac([flag])
            self.assertEqual(0, r.returncode)
            self.assertEqual('expac 10\n', r.stdout.decode())

    def testHelp(self):
        for flag in ('-h', '--help'):
            r = self.Expac([flag])
            self.assertEqual(0, r.returncode)
            self.assertIn('Usage: expac [options] <format> target...', r.stdout.decode())

    def testDefaultDelimiterIsNewline(self):
        r = self.Expac(['%n', 'foo', 'bar'])
        self.assertEqual(0, r.returncode)
        self.assertEqual('foo\nbar\n', r.stdout.decode())

    def testDefaultListDelimiterIsTwoSpaces(self):
        r = self.Expac(['%D', 'foo'])
        self.assertEqual('bar  glibc\n', r.stdout.decode())

    def testLastConfigFlagWins(self):
        # The harness prepends a working --config; a later occurrence on the
        # command line should override it via normal getopt_long semantics.
        r = self.Expac(['--config=/nonexistent/pacman.conf', '%n', 'foo'])
        self.assertEqual(1, r.returncode)

    def testConfigFileMissing(self):
        os.remove(self.pacmanconf)
        r = self.Expac(['%n', 'foo'])
        self.assertEqual(1, r.returncode)

    def testInvalidOptionExits(self):
        r = self.Expac(['--not-a-real-option', '%n'])
        self.assertEqual(1, r.returncode)


if __name__ == '__main__':
    expac_test.main()
