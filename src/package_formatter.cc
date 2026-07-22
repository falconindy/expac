// SPDX-License-Identifier: MIT

#include "package_formatter.hh"

#include <alpm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <string>
#include <utility>
#include <vector>

#include "alpm_list.hh"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static char const size_tokens[] = "BKMGTPEZYRQ";
static char const digits[] = "0123456789";
static char const printf_flags[] = "'-+ #0I";

static std::string alpm_backup_get_name(alpm_backup_t* bkup) {
  return bkup->name;
}

static std::pair<double, std::string> humanize_size(off_t bytes,
                                                    const char target_unit) {
  static const int unitcount = sizeof(size_tokens) - 1;

  double val = (double)bytes;
  int index;

  for (index = 0; index < unitcount; index++) {
    if (target_unit != '\0' && size_tokens[index] == target_unit) {
      break;
    } else if (target_unit == '\0' && val <= 2048.0 && val >= -2048.0) {
      break;
    }
    val /= 1024.0;
  }

  std::string label(1, size_tokens[index]);
  if (size_tokens[index] != 'B') {
    label += "iB";
  }

  return {val, label};
}

static std::string size_to_string(off_t pkgsize, char humansize) {
  char out[64];

  if (humansize == 'B') {
    snprintf(out, sizeof(out), "%jd", (intmax_t)pkgsize);
  } else {
    const auto [n, unit] = humanize_size(pkgsize, humansize);
    snprintf(out, sizeof(out), "%.2f %s", n, unit.c_str());
  }

  return out;
}

static std::string format_optdep(alpm_depend_t* optdep) {
  return std::string(optdep->name) + ": " +
         (optdep->desc ? optdep->desc : "(null)");
}

static std::string alpm_dep_get_name(alpm_depend_t* dep) { return dep->name; }

static std::string dep_to_string(alpm_depend_t* dep) {
  char* s = alpm_dep_compute_string(dep);
  std::string result(s != NULL ? s : "");
  free(s);
  return result;
}

static int print_escaped(const char* delim) {
  const char* f;
  int out = 0;

  for (f = delim; *f != '\0'; f++) {
    if (*f == '\\') {
      switch (*++f) {
        case '\\':
          fputc('\\', stdout);
          break;
        case '"':
          fputc('\"', stdout);
          break;
        case 'a':
          fputc('\a', stdout);
          break;
        case 'b':
          fputc('\b', stdout);
          break;
        case 'e': // \e is nonstandard
          fputc('\033', stdout);
          break;
        case 'n':
          fputc('\n', stdout);
          break;
        case 'r':
          fputc('\r', stdout);
          break;
        case 't':
          fputc('\t', stdout);
          break;
        case 'v':
          fputc('\v', stdout);
          break;
        case '0':
          fputc('\0', stdout);
          break;
        default:
          fputc(*f, stdout);
          break;
      }
      ++out;
    } else {
      fputc(*f, stdout);
      ++out;
    }
  }

  return out;
}

// Converts an alpm_list_t of T* into strings via fn(T*), e.g. turning a
// list of alpm_depend_t* into their formatted name/version strings.
template <typename T, typename Fn>
static std::vector<std::string> alpm_list_to_strings(alpm_list_t* list, Fn fn) {
  std::vector<std::string> result;

  for (T* item : AlpmList<T*>(list)) {
    result.push_back(fn(item));
  }

  return result;
}

// Same, for lists whose items are themselves plain C strings owned by
// libalpm (e.g. licenses, groups) -- no per-item extraction needed.
static std::vector<std::string> alpm_list_to_strings(alpm_list_t* list) {
  std::vector<std::string> result;

  for (const char* item : AlpmList<const char*>(list)) {
    result.emplace_back(item);
  }

  return result;
}

// Same, for the handful of alpm_pkg_compute_*() calls that return a list
// we own outright (both the nodes and the strdup'd strings).
static std::vector<std::string> alpm_owned_list_to_strings(alpm_list_t* list) {
  std::vector<std::string> result;

  for (char* item : AlpmList<char*>(list)) {
    result.emplace_back(item);
  }

  alpm_list_free_inner(list, free);
  alpm_list_free(list);

  return result;
}

static bool backup_file_is_modified(const alpm_backup_t* backup_file) {
  char fullpath[PATH_MAX];

  // TODO: respect expac->dbroot
  snprintf(fullpath, sizeof(fullpath), "/%s", backup_file->name);

  char* md5sum = alpm_compute_md5sum(fullpath);
  if (md5sum == NULL) {
    return false;
  }

  bool modified = strcmp(md5sum, backup_file->hash) != 0;

  free(md5sum);

  return modified;
}

static std::vector<std::string> get_modified_files(alpm_pkg_t* pkg) {
  std::vector<std::string> modified_files;

  for (const alpm_backup_t* backup :
       AlpmList<const alpm_backup_t*>(alpm_pkg_get_backup(pkg))) {
    if (backup->hash && backup_file_is_modified(backup)) {
      modified_files.emplace_back(backup->name);
    }
  }

  return modified_files;
}

static std::vector<std::string> get_validation_method(alpm_pkg_t* pkg) {
  alpm_pkgvalidation_t v =
      static_cast<alpm_pkgvalidation_t>(alpm_pkg_get_validation(pkg));

  if (v == ALPM_PKG_VALIDATION_UNKNOWN) {
    return {"Unknown"};
  }

  if (v & ALPM_PKG_VALIDATION_NONE) {
    return {"None"};
  }

  std::vector<std::string> validation;

  if (v & ALPM_PKG_VALIDATION_MD5SUM) {
    validation.emplace_back("MD5 Sum");
  }
  if (v & ALPM_PKG_VALIDATION_SHA256SUM) {
    validation.emplace_back("SHA256 Sum");
  }
  if (v & ALPM_PKG_VALIDATION_SIGNATURE) {
    validation.emplace_back("Signature");
  }

  return validation;
}

int PackageFormatter::print_list(const std::vector<std::string>& items) const {
  int out = 0;

  if (items.empty()) {
    if (opts_.verbose) {
      out += printf("None");
    }
    return out;
  }

  for (size_t i = 0; i < items.size(); ++i) {
    out += printf("%s", items[i].c_str());
    if (i + 1 < items.size()) {
      out += print_escaped(opts_.listdelim);
    }
  }

  return out;
}

int PackageFormatter::print_time(time_t timestamp) const {
  char buffer[64];
  int out = 0;

  if (!timestamp) {
    if (opts_.verbose) {
      out += printf("None");
    }
    return out;
  }

  // no overflow here, strftime prints a max of 64 including null
  strftime(&buffer[0], 64, opts_.timefmt, localtime(&timestamp));
  out += printf("%s", buffer);

  return out;
}

int PackageFormatter::print_filelist(alpm_filelist_t* filelist) const {
  int out = 0;
  size_t i;

  for (i = 0; i < filelist->count; i++) {
    out += printf("%s", (filelist->files + i)->name);
    if (i < filelist->count - 1) {
      out += print_escaped(opts_.listdelim);
    }
  }

  return out;
}

void PackageFormatter::Print(alpm_pkg_t* pkg) {
  const char *f, *end;
  int out = 0;

  end = opts_.format + strlen(opts_.format);

  for (f = opts_.format; f < end; f++) {
    if (*f == '%') {
      char fmt[64] = {0};
      int l = 1;

      l += strspn(f + l, printf_flags);
      l += strspn(f + l, digits);
      memcpy(fmt, f, l);
      fmt[l] = 's';

      f += l;
      switch (*f) {
        // simple attributes
        case 'f': // filename
          out += printf(fmt, alpm_pkg_get_filename(pkg));
          break;
        case 'e': // package base
          out += printf(fmt, alpm_pkg_get_base(pkg));
          break;
        case 'n': // package name
          out += printf(fmt, alpm_pkg_get_name(pkg));
          break;
        case 'v': // version
          out += printf(fmt, alpm_pkg_get_version(pkg));
          break;
        case 'd': // description
          out += printf(fmt, alpm_pkg_get_desc(pkg));
          break;
        case 'u': // project url
          out += printf(fmt, alpm_pkg_get_url(pkg));
          break;
        case 'p': // packager name
          out += printf(fmt, alpm_pkg_get_packager(pkg));
          break;
        case 's': // md5sum
          out += printf(fmt, alpm_pkg_get_md5sum(pkg));
          break;
        case 'a': // architecture
          out += printf(fmt, alpm_pkg_get_arch(pkg));
          break;
        case 'i': // has install scriptlet?
          out += printf(fmt, alpm_pkg_has_scriptlet(pkg) ? "yes" : "no");
          break;
        case 'r': // repo
          out += printf(fmt, alpm_db_get_name(alpm_pkg_get_db(pkg)));
          break;
        case 'w': // install reason
          out +=
              printf(fmt, alpm_pkg_get_reason(pkg) ? "dependency" : "explicit");
          break;
        case '!': // result number
          fmt[strlen(fmt) - 1] = 'd';
          out += printf(fmt, pkgcounter_++);
          break;
        case 'g': // base64 gpg sig
          out += printf(fmt, alpm_pkg_get_base64_sig(pkg));
          break;
        case 'h': // sha256sum
          out += printf(fmt, alpm_pkg_get_sha256sum(pkg));
          break;

        // times
        case 'b': // build date
          out += print_time(alpm_pkg_get_builddate(pkg));
          break;
        case 'l': // install date
          out += print_time(alpm_pkg_get_installdate(pkg));
          break;

        // sizes
        case 'k': // download size
          out += printf(
              fmt,
              size_to_string(alpm_pkg_get_size(pkg), opts_.humansize).c_str());
          break;
        case 'm': // install size
          out += printf(
              fmt, size_to_string(alpm_pkg_get_isize(pkg), opts_.humansize)
                       .c_str());
          break;

        // lists
        case 'F': // files
          out += print_filelist(alpm_pkg_get_files(pkg));
          break;
        case 'N': // requiredby
          out += print_list(
              alpm_owned_list_to_strings(alpm_pkg_compute_requiredby(pkg)));
          break;
        case 'W': // optionalfor
          out += print_list(
              alpm_owned_list_to_strings(alpm_pkg_compute_optionalfor(pkg)));
          break;
        case 'L': // licenses
          out += print_list(alpm_list_to_strings(alpm_pkg_get_licenses(pkg)));
          break;
        case 'G': // groups
          out += print_list(alpm_list_to_strings(alpm_pkg_get_groups(pkg)));
          break;
        case 'E': // depends (shortdeps)
          out += print_list(alpm_list_to_strings<alpm_depend_t>(
              alpm_pkg_get_depends(pkg), alpm_dep_get_name));
          break;
        case 'J': // makedepends
          out += print_list(alpm_list_to_strings<alpm_depend_t>(
              alpm_pkg_get_makedepends(pkg), dep_to_string));
          break;
        case 'K': // checkdepends
          out += print_list(alpm_list_to_strings<alpm_depend_t>(
              alpm_pkg_get_checkdepends(pkg), dep_to_string));
          break;
        case 'D': // depends
          out += print_list(alpm_list_to_strings<alpm_depend_t>(
              alpm_pkg_get_depends(pkg), dep_to_string));
          break;
        case 'O': // optdepends
          out += print_list(alpm_list_to_strings<alpm_depend_t>(
              alpm_pkg_get_optdepends(pkg), format_optdep));
          break;
        case 'o': // optdepends (shortdeps)
          out += print_list(alpm_list_to_strings<alpm_depend_t>(
              alpm_pkg_get_optdepends(pkg), alpm_dep_get_name));
          break;
        case 'H': // conflicts
          out += print_list(alpm_list_to_strings<alpm_depend_t>(
              alpm_pkg_get_conflicts(pkg), dep_to_string));
          break;
        case 'C': // conflicts (shortdeps)
          out += print_list(alpm_list_to_strings<alpm_depend_t>(
              alpm_pkg_get_conflicts(pkg), alpm_dep_get_name));
          break;
        case 'S': // provides (shortdeps)
          out += print_list(alpm_list_to_strings<alpm_depend_t>(
              alpm_pkg_get_provides(pkg), alpm_dep_get_name));
          break;
        case 'P': // provides
          out += print_list(alpm_list_to_strings<alpm_depend_t>(
              alpm_pkg_get_provides(pkg), dep_to_string));
          break;
        case 'R': // replaces (shortdeps)
          out += print_list(alpm_list_to_strings<alpm_depend_t>(
              alpm_pkg_get_replaces(pkg), alpm_dep_get_name));
          break;
        case 'T': // replaces
          out += print_list(alpm_list_to_strings<alpm_depend_t>(
              alpm_pkg_get_replaces(pkg), dep_to_string));
          break;
        case 'B': // backup
          out += print_list(alpm_list_to_strings<alpm_backup_t>(
              alpm_pkg_get_backup(pkg), alpm_backup_get_name));
          break;
        case 'V': // package validation
          out += print_list(get_validation_method(pkg));
          break;
        case 'M': // modified
          out += print_list(get_modified_files(pkg));
          break;
        case '%':
          fputc('%', stdout);
          out++;
          break;
        default:
          fputc('?', stdout);
          out++;
          break;
      }
    } else if (*f == '\\') {
      char esc[3] = {f[0], f[1], '\0'};
      out += print_escaped(esc);
      ++f;
    } else {
      fputc(*f, stdout);
      out++;
    }
  }

  // only print a delimeter if any package data was outputted
  if (out > 0) {
    print_escaped(opts_.delim);
  }
}

// vim: set et ts=2 sw=2:
