// SPDX-License-Identifier: MIT

#ifndef _PACKAGE_FORMATTER_H
#define _PACKAGE_FORMATTER_H

#include <alpm.h>

#include <ctime>
#include <string>
#include <vector>

#include "expac.hh"

// Formats packages per an Options' format string and prints them to
// stdout. Constructed once for a run and given each result package in
// turn via Print().
class PackageFormatter {
 public:
  explicit PackageFormatter(const Options& opts) : opts_(opts) {}

  void Print(alpm_pkg_t* pkg);

 private:
  int print_list(const std::vector<std::string>& items) const;
  int print_time(time_t timestamp) const;
  int print_filelist(alpm_filelist_t* filelist) const;

  const Options& opts_;
  int pkgcounter_ = 0;
};

#endif // _PACKAGE_FORMATTER_H

// vim: set et ts=2 sw=2:
