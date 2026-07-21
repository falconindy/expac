#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Tests for -S/--sync: reading sync (repo) databases."""

import expac_test


class TestSync(expac_test.TestCase):
    def testSyncOnlyAttributes(self):
        r = self.Expac(['-S', '%n|%r|%f|%k|%s|%h', 'extra/foo'])
        self.assertEqual(0, r.returncode)
        self.assertEqual(
            'foo|extra|foo-1.1-1-x86_64.pkg.tar.zst|1048576|'
            'd41d8cd98f00b204e9800998ecf8427e|'
            'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n',
            r.stdout.decode(),
        )

    def testBase64Signature(self):
        r = self.Expac(['-S', '%g', 'extra/foo'])
        self.assertEqual(0, r.returncode)
        self.assertTrue(r.stdout.decode().startswith('iQEzBAABCAAd'))

    def testMakeAndCheckDepends(self):
        r = self.Expac(['-S', '%J|%K', 'extra/foo'])
        self.assertEqual(0, r.returncode)
        self.assertEqual('git|python\n', r.stdout.decode())

    def testRepoSlashPackageSyntax(self):
        extra = self.Expac(['-S', '%n|%v|%r', 'extra/foo'])
        core = self.Expac(['-S', '%n|%v|%r', 'core/foo'])
        self.assertEqual('foo|1.1-1|extra\n', extra.stdout.decode())
        self.assertEqual('foo|1.0.5-1|core\n', core.stdout.decode())

    def testUnqualifiedTargetMatchesAllRepos(self):
        # 'foo' exists in both extra and core; without -1 both are returned.
        r = self.Expac(['-S', '%r/%n', 'foo'])
        self.assertEqual(0, r.returncode)
        self.assertEqual({'extra/foo', 'core/foo'}, set(r.stdout.decode().split()))

    def testReadOneStopsAtFirstMatch(self):
        r = self.Expac(['-S1', '%r/%n', 'foo'])
        self.assertEqual(0, r.returncode)
        lines = r.stdout.decode().split()
        self.assertEqual(1, len(lines))
        self.assertIn(lines[0], {'extra/foo', 'core/foo'})

    def testGroupSearch(self):
        r = self.Expac(['-Sg', '%n', 'synctest'])
        self.assertEqual(0, r.returncode)
        self.assertEqual({'foo', 'bar'}, set(r.stdout.decode().split()))

    def testRegexSearchMatchesSubstring(self):
        # foo-tools' name/desc both contain "foo".
        r = self.Expac(['-Ss', '%n', '^foo'])
        self.assertEqual(0, r.returncode)
        self.assertEqual({'foo', 'foo-tools'}, set(r.stdout.decode().split()))

    def testNoTargetsReturnsAllPackagesAcrossRepos(self):
        r = self.Expac(['-S', '%r/%n'])
        self.assertEqual(0, r.returncode)
        self.assertEqual(
            {'core/foo', 'extra/bar', 'extra/foo', 'extra/foo-tools'},
            set(r.stdout.decode().split()),
        )

    def testPackageNotFound(self):
        r = self.Expac(['-Sv', '%n', 'doesnotexist'])
        self.assertEqual(1, r.returncode)
        self.assertIn("error: package `doesnotexist' not found", r.stderr.decode())


if __name__ == '__main__':
    expac_test.main()
