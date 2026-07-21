#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Tests for the format-string engine: every %-token, printf field specs on
lowercase tokens, backslash escapes, and the special %! / %% tokens."""

import expac_test


class TestFormat(expac_test.TestCase):
    def testTimeTokens(self):
        r = self.Expac(['-Q', '%b|%l', 'foo'])
        self.assertEqual(0, r.returncode)
        # BUILDDATE=1577836800, INSTALLDATE=1577840400, both UTC; default
        # --timefmt is "%c" under the pinned C.UTF-8/UTC test environment.
        self.assertEqual(
            'Wed Jan  1 00:00:00 2020|Wed Jan  1 01:00:00 2020\n', r.stdout.decode()
        )

    def testCustomTimefmt(self):
        r = self.Expac(['-Q', '--timefmt=%Y-%m-%d', '%b', 'foo'])
        self.assertEqual(0, r.returncode)
        self.assertEqual('2020-01-01\n', r.stdout.decode())

    def testTimefmtEpochRoundTrips(self):
        r = self.Expac(['-Q', '--timefmt=%s', '%b', 'foo'])
        self.assertEqual(0, r.returncode)
        self.assertEqual('1577836800\n', r.stdout.decode())

    def testDownloadAndInstallSize(self):
        r = self.Expac(['-S', '%k', 'extra/foo'])
        self.assertEqual('1048576\n', r.stdout.decode())
        r = self.Expac(['-Q', '%m', 'foo'])
        self.assertEqual('2097152\n', r.stdout.decode())

    def testHumanSizeExplicitUnit(self):
        r = self.Expac(['-Q', '-H', 'M', '%m', 'foo'])
        self.assertEqual(0, r.returncode)
        self.assertEqual('2.00 MiB\n', r.stdout.decode())

    def testHumanSizeAuto(self):
        r = self.Expac(['-Q', '-H', 'auto', '%m', 'foo'])
        self.assertEqual(0, r.returncode)
        self.assertEqual('2048.00 KiB\n', r.stdout.decode())

    def testHumanSizeInvalidUnit(self):
        r = self.Expac(['-Q', '-H', 'Q9', '%m', 'foo'])
        self.assertEqual(1, r.returncode)
        self.assertIn('invalid SI size formatter', r.stderr.decode())

    def testListTokens(self):
        r = self.Expac(['-Q', '%D|%E|%O|%o|%C|%H|%P|%S|%T|%R|%L|%G', 'foo'])
        self.assertEqual(0, r.returncode)
        self.assertEqual(
            'bar  glibc|bar  glibc|baz: optional baz support|baz|'
            'oldfoo|oldfoo|libfoo.so=1-64|libfoo.so|ancientfoo|ancientfoo|'
            'MIT|testgroup\n',
            r.stdout.decode(),
        )

    def testPackageValidation(self):
        # %VALIDATION% "none" in the fixture maps to ALPM_PKG_VALIDATION_NONE.
        r = self.Expac(['-Q', '%V', 'foo'])
        self.assertEqual(0, r.returncode)
        self.assertEqual('None\n', r.stdout.decode())

    def testModifiedBackupFiles(self):
        # backup_file_is_modified() always resolves against the real host
        # root ("/"), not RootDir (see the TODO in src/expac.c), so the only
        # behavior we can assert deterministically in a test environment is
        # the "not modified" path for a file that will never exist on disk.
        r = self.Expac(['-Qv', '%M', 'modtest'])
        self.assertEqual(0, r.returncode)
        self.assertEqual('None\n', r.stdout.decode())

    def testResultCounter(self):
        r = self.Expac(['-Q', '%!:%n'])
        self.assertEqual(0, r.returncode)
        self.assertEqual(
            ['0:bar', '1:baz', '2:foo', '3:modtest', '4:qux'],
            r.stdout.decode().split(),
        )

    def testLiteralPercent(self):
        r = self.Expac(['-Q', '%%%n%%', 'foo'])
        self.assertEqual('%foo%\n', r.stdout.decode())

    def testUnknownTokenPrintsQuestionMark(self):
        r = self.Expac(['-Q', '%Z', 'foo'])
        self.assertEqual(0, r.returncode)
        self.assertEqual('?\n', r.stdout.decode())

    def testPrintfFieldSpecOnSimpleToken(self):
        r = self.Expac(['-Q', '[%-10n]', 'foo'])
        self.assertEqual('[foo       ]\n', r.stdout.decode())
        r = self.Expac(['-Q', '[%10n]', 'foo'])
        self.assertEqual('[       foo]\n', r.stdout.decode())

    def testPrintfFieldSpecDoesNotApplyToListToken(self):
        # Per the docs, printf specs only apply to lowercase, non-list,
        # non-%m/%k tokens; a width spec before %D is not honored specially,
        # and libalpm renders it as a literal-ish '?' since 'D' capitalised
        # deps aren't parsed with a numeric prefix at all -- confirm the
        # numeric prefix is simply consumed as part of the (unused) fmt buffer
        # and %D still prints the full dependency list untouched.
        r = self.Expac(['-Q', '%10D', 'foo'])
        self.assertEqual('bar  glibc\n', r.stdout.decode())

    def testBackslashEscapesInFormatString(self):
        r = self.Expac(['-Q', r'%n\t%v\n', 'foo'])
        self.assertEqual('foo\t1.0-1\n\n', r.stdout.decode())

    def testDelimiterEscapes(self):
        r = self.Expac(['-Q', '-d', r'\t', '%n'])
        self.assertEqual('bar\tbaz\tfoo\tmodtest\tqux\t', r.stdout.decode())

    def testListDelimiterEscapes(self):
        r = self.Expac(['-Q', '-l', r'\n', '%D', 'foo'])
        self.assertEqual('bar\nglibc\n', r.stdout.decode())

    def testCustomDelimiterLiteralString(self):
        r = self.Expac(['-Q', '-d', ', ', '%n'])
        self.assertEqual('bar, baz, foo, modtest, qux, ', r.stdout.decode())


if __name__ == '__main__':
    expac_test.main()
