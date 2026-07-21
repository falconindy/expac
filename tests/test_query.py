#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Tests for -Q/--query: reading the local database."""

import expac_test


class TestQuery(expac_test.TestCase):
    def testDefaultCorpusIsLocal(self):
        # -Q is the default; omitting it entirely should behave identically.
        with_flag = self.Expac(['-Q', '%n', 'foo'])
        without_flag = self.Expac(['%n', 'foo'])
        self.assertEqual(with_flag.stdout, without_flag.stdout)

    def testSimpleAttributes(self):
        r = self.Expac(['-Q', '%n|%v|%e|%d|%u|%p|%a|%r', 'foo'])
        self.assertEqual(0, r.returncode)
        self.assertEqual(
            'foo|1.0-1|foo|A foo package for testing|https://example.com/foo|'
            'Test Packager <test@example.com>|x86_64|local\n',
            r.stdout.decode(),
        )

    def testInstallReason(self):
        explicit = self.Expac(['-Q', '%w', 'foo'])
        dependency = self.Expac(['-Q', '%w', 'bar'])
        self.assertEqual('explicit\n', explicit.stdout.decode())
        self.assertEqual('dependency\n', dependency.stdout.decode())

    def testHasInstallScriptlet(self):
        # None of the fixture packages ship an install scriptlet.
        r = self.Expac(['-Q', '%i', 'foo'])
        self.assertEqual('no\n', r.stdout.decode())

    def testFiles(self):
        r = self.Expac(['-Q', '%F', 'bar'])
        self.assertEqual('usr/  usr/bin/  usr/bin/bar\n', r.stdout.decode())

    def testRequiredByAndOptionalFor(self):
        # foo depends on bar; baz optionally depends on bar.
        r = self.Expac(['-Q', '%n|%N|%W', 'bar'])
        self.assertEqual('bar|foo|baz\n', r.stdout.decode())

    def testNoTargetsReturnsAllPackagesSorted(self):
        r = self.Expac(['-Q', '%n'])
        self.assertEqual(0, r.returncode)
        self.assertEqual(['bar', 'baz', 'foo', 'modtest', 'qux'], r.stdout.decode().split())

    def testGroupSearch(self):
        r = self.Expac(['-Qg', '%n', 'testgroup'])
        self.assertEqual(0, r.returncode)
        self.assertEqual({'foo', 'qux'}, set(r.stdout.decode().split()))

    def testRegexSearch(self):
        r = self.Expac(['-Qs', '%n', '^ba'])
        self.assertEqual(0, r.returncode)
        self.assertEqual({'bar', 'baz'}, set(r.stdout.decode().split()))

    def testPackageNotFoundQuiet(self):
        r = self.Expac(['-Q', '%n', 'doesnotexist'])
        self.assertEqual(1, r.returncode)
        self.assertEqual('', r.stderr.decode())

    def testPackageNotFoundVerbose(self):
        r = self.Expac(['-Qv', '%n', 'doesnotexist'])
        self.assertEqual(1, r.returncode)
        self.assertIn("error: package `doesnotexist' not found", r.stderr.decode())

    def testEmptyListFieldsShowNoneWithVerbose(self):
        # bar has no groups, optdepends, conflicts, provides, or replaces.
        r = self.Expac(['-Qv', '%G|%O|%C|%P|%T', 'bar'])
        self.assertEqual(0, r.returncode)
        self.assertEqual('None|None|None|None|None\n', r.stdout.decode())

    def testEmptyListFieldsAreBlankWithoutVerbose(self):
        r = self.Expac(['-Q', '%G|%O|%C', 'bar'])
        self.assertEqual(0, r.returncode)
        self.assertEqual('||\n', r.stdout.decode())

    def testEmptySimpleAttributePrintsLiteralNull(self):
        # Unlike list fields, plain string attributes (%u, %p, ...) go
        # straight through printf("%s", NULL), which glibc renders as the
        # literal string "(null)" -- -v/--verbose has no effect on this path.
        quiet = self.Expac(['-Q', '%u|%p', 'bar'])
        verbose = self.Expac(['-Qv', '%u|%p', 'bar'])
        self.assertEqual('(null)|(null)\n', quiet.stdout.decode())
        self.assertEqual(quiet.stdout, verbose.stdout)


if __name__ == '__main__':
    expac_test.main()
