#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Shared harness for expac's integration tests.

expac is a thin CLI wrapper around libalpm, so there's little value in unit
testing its internals. Instead, these tests build a throwaway pacman database
tree from the plain-text fixtures under tests/fixtures/ and run the compiled
expac binary against it via --config, asserting on stdout/stderr/exit code.
This mirrors the Python integration harnesses used by auracle and pkgfile.
"""

import glob
import os
import shutil
import subprocess
import tarfile
import tempfile
import time
import unittest

__scriptdir__ = os.path.dirname(os.path.abspath(__file__))
__fixturedir__ = os.path.join(__scriptdir__, 'fixtures')


def FindMesonBuildDir():
    # When run through meson or ninja, we're already in the build dir.
    if os.path.exists('.ninja_log'):
        return os.path.curdir

    # When run manually, we're probably in the repo root.
    paths = glob.glob('*/.ninja_log')
    if len(paths) > 1:
        raise ValueError('Multiple build directories found. Unable to proceed.')
    if len(paths) == 0:
        raise ValueError('No build directory found. Have you run "meson setup build" yet?')

    return os.path.dirname(paths[0])


class TimeLoggingTestResult(unittest.runner.TextTestResult):
    def startTest(self, test):
        self._started_at = time.time()
        super().startTest(test)

    def addSuccess(self, test):
        elapsed = time.time() - self._started_at
        super(unittest.runner.TextTestResult, self).addSuccess(test)
        if self.showAll:
            self.stream.writeln(f'ok ({elapsed:.03}s)')
        elif self.dots:
            self.stream.write('.')
            self.stream.flush()


class TestCase(unittest.TestCase):
    """Base class that builds a fixture pacman root and runs expac against it."""

    maxDiff = None

    def setUp(self):
        self.build_dir = FindMesonBuildDir()
        self._tempdir = tempfile.TemporaryDirectory()
        self.tempdir = self._tempdir.name

        self.dbroot = os.path.join(self.tempdir, 'pacman')
        self._BuildLocalDb()
        self.sync_repos = self._BuildSyncDbs()
        self.pkgfiles = self._BuildPackageFiles()
        self._WritePacmanConf()

    def tearDown(self):
        self._tempdir.cleanup()

    def _BuildLocalDb(self):
        # libalpm reads the local db straight off disk as a directory tree of
        # <pkg>-<ver>/{desc,files} -- no archive involved.
        dst = os.path.join(self.dbroot, 'local')
        shutil.copytree(os.path.join(__fixturedir__, 'local'), dst)
        with open(os.path.join(dst, 'ALPM_DB_VERSION'), 'w') as f:
            f.write('9\n')

    def _BuildSyncDbs(self):
        # Sync dbs are gzipped tarballs of the same <pkg>-<ver>/desc entries.
        # Fixtures are kept as plain directories under tests/fixtures/sync/
        # and packed at test time so nothing binary lives in git.
        src = os.path.join(__fixturedir__, 'sync')
        repos = []
        if not os.path.isdir(src):
            return repos

        syncdir = os.path.join(self.dbroot, 'sync')
        os.makedirs(syncdir, exist_ok=True)

        for repo in sorted(os.listdir(src)):
            repo_src = os.path.join(src, repo)
            if not os.path.isdir(repo_src):
                continue
            repos.append(repo)
            with tarfile.open(os.path.join(syncdir, f'{repo}.db'), 'w:gz') as tar:
                for pkg in sorted(os.listdir(repo_src)):
                    tar.add(os.path.join(repo_src, pkg), arcname=pkg)

        return repos

    def _BuildPackageFiles(self):
        # -p/--file loads a package archive directly via alpm_pkg_load, which
        # only needs a .PKGINFO member. Build one throwaway .pkg.tar.gz per
        # fixture package into the tempdir.
        src = os.path.join(__fixturedir__, 'pkgs')
        pkgfiles = {}
        if not os.path.isdir(src):
            return pkgfiles

        for pkg in sorted(os.listdir(src)):
            pkginfo = os.path.join(src, pkg, '.PKGINFO')
            if not os.path.exists(pkginfo):
                continue
            archive = os.path.join(self.tempdir, f'{pkg}.pkg.tar.gz')
            with tarfile.open(archive, 'w:gz') as tar:
                tar.add(pkginfo, arcname='.PKGINFO')
            pkgfiles[pkg] = archive

        return pkgfiles

    def _WritePacmanConf(self):
        self.pacmanconf = os.path.join(self.tempdir, 'pacman.conf')
        with open(self.pacmanconf, 'w') as f:
            f.write('[options]\n')
            f.write(f'DBPath = {self.dbroot}\n')
            f.write(f'RootDir = {self.dbroot}\n')
            for repo in self.sync_repos:
                f.write(f'\n[{repo}]\n')

    def Expac(self, args, stdin=None):
        # Pinned locale/timezone so time formatting and sorting are
        # deterministic regardless of the host environment; PATH is narrowed
        # so nothing from the host leaks into results.
        env = {
            'TZ': 'UTC',
            'LC_TIME': 'C.UTF-8',
            'LC_COLLATE': 'C.UTF-8',
            'PATH': '/bin:/usr/bin',
        }

        cmdline = [
            os.path.join(self.build_dir, 'expac'),
            f'--config={self.pacmanconf}',
        ] + args

        if stdin is not None:
            return subprocess.run(cmdline, env=env, capture_output=True, input=stdin)

        # Explicitly detach stdin (rather than inheriting the test runner's)
        # so behavior doesn't depend on whether tests happen to run under a
        # tty or a pipe.
        return subprocess.run(
            cmdline, env=env, capture_output=True, stdin=subprocess.DEVNULL
        )


def main():
    test_runner = unittest.TextTestRunner(resultclass=TimeLoggingTestResult)
    unittest.main(testRunner=test_runner)
