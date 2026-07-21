#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Tests for -p/--file: loading package data directly from a package archive
via alpm_pkg_load(), bypassing the local/sync databases entirely."""

import expac_test


class TestFile(expac_test.TestCase):
    def testLoadsPkginfoFields(self):
        r = self.Expac(['-p', '%n|%v|%e|%d|%u|%a|%m', self.pkgfiles['filepkg']])
        self.assertEqual(0, r.returncode)
        self.assertEqual(
            'filepkg|3.0-1|filepkg|A file-loaded test package|'
            'https://example.com/filepkg|x86_64|4096\n',
            r.stdout.decode(),
        )

    def testDependsAndOptdepends(self):
        r = self.Expac(['-p', '%D|%O', self.pkgfiles['filepkg']])
        self.assertEqual(0, r.returncode)
        self.assertEqual('glibc|perl: for scripts\n', r.stdout.decode())

    def testMakeAndCheckDepends(self):
        r = self.Expac(['-p', '%J|%K', self.pkgfiles['filepkg']])
        self.assertEqual(0, r.returncode)
        self.assertEqual('git|python\n', r.stdout.decode())

    def testProvidesConflictsReplaces(self):
        r = self.Expac(['-p', '%P|%C|%T', self.pkgfiles['filepkg']])
        self.assertEqual(0, r.returncode)
        self.assertEqual('libfilepkg.so=1-64|oldfilepkg|ancientfilepkg\n', r.stdout.decode())

    def testTwoArchivesInOrder(self):
        # Multiple -p targets are all resolved and printed in argument order.
        r = self.Expac(['-p', '%n', self.pkgfiles['filepkg'], self.pkgfiles['filepkg']])
        self.assertEqual(0, r.returncode)
        self.assertEqual('filepkg\nfilepkg\n', r.stdout.decode())

    def testMissingArchiveIsAnError(self):
        r = self.Expac(['-p', '%n', '/nonexistent/path.pkg.tar.zst'])
        self.assertEqual(1, r.returncode)
        self.assertIn('/nonexistent/path.pkg.tar.zst', r.stderr.decode())


if __name__ == '__main__':
    expac_test.main()
